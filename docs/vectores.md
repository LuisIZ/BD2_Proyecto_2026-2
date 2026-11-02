# Tipos FLOAT y VECTOR, métricas y k-NN secuencial

Extensión del catálogo, el parser y el ejecutor para la Parte 4 (sección 2.4.2, issue #59).
Permite guardar números decimales y vectores de características en las tres organizaciones
(`HEAP`, `SEQUENTIAL`, `BPLUS`) y buscar los vectores más parecidos sin índice. Ese k-NN
exacto es la base contra la que se comparan IVF y HNSW (#60, #61, #63).

## 1. Tipo FLOAT

| Aspecto | Decisión |
|---|---|
| Nombre | `FLOAT` (alias `REAL`) |
| Representación | `float32`, 4 bytes, en el formato variable y en el fijo |
| Literal | `19.99`, `-3`, `1e-3`; un entero se acepta y se guarda como FLOAT |
| Operaciones | `=`, `!=`, `<`, `<=`, `>`, `>=`, `BETWEEN`, `ORDER BY`, `SUM`, `AVG`, `MIN`, `MAX` |
| Restricciones | no puede ser clave primaria ni llevar índice |

Se eligió `float32` y no `double` porque es el mismo tipo que usan los vectores (y FAISS), y
así un literal como `0.1` se redondea igual en la consulta y en la columna: `WHERE precio = 0.1`
encuentra la fila que se insertó con `0.1`. Al mostrarlo se usa la representación más corta
que vuelve al mismo `float` (`0.1` y no `0.100000001`).

## 2. Tipo VECTOR(k)

| Aspecto | Decisión |
|---|---|
| Representación | k valores `float32` seguidos, sin cabecera (la dimensión está en el catálogo) |
| Tamaño en disco | 4·k bytes; con k = 256, 1 KB por vector |
| Dimensión máxima | 512 (2 KB), para que el registro quepa en una página de 4 KB |
| Catálogo | la dimensión va en el mismo campo que el largo de `VARCHAR` (`catalogo.txt`) |
| Restricciones | no puede ser clave primaria ni llevar índice en el motor; no se compara con `=` ni se ordena directamente: solo con `distancia(...)` |

- Se guarda en `float32` porque es lo que producen `extraer.py` (#57) y lo que usa FAISS: los
  dos lados leen los mismos números sin conversión.
- El B+ agrupado usa registros de tamaño fijo de hasta 2040 bytes. `VECTOR(256)` con la clave
  ocupa 1028 bytes y cabe; `VECTOR(512)` (2052 bytes) solo se acepta en `HEAP` y `SEQUENTIAL`,
  y en `BPLUS` da un error claro al crear la tabla.
- `DESCRIBE` muestra la dimensión (`VECTOR(256)`) y la salida JSON del motor devuelve el vector
  como texto `VECTOR(...)`, que la API puede reusar tal cual como literal SQL (#62).

## 3. Sintaxis

```sql
CREATE TABLE imagenes (id INT PRIMARY KEY, nombre VARCHAR(40), v VECTOR(256));
INSERT INTO imagenes VALUES (1, 'silla', VECTOR(0.12, 0, 3.5e-05, ...));
COPY imagenes FROM FILE 'vectores_10000.csv';

-- k vecinos: las 10 imágenes más parecidas
SELECT id, nombre FROM imagenes ORDER BY distancia(v, VECTOR(...), 'coseno') LIMIT 10;

-- rango: todas las que están a menos de 0.3 de coseno
SELECT id FROM imagenes WHERE distancia(v, VECTOR(...), 'coseno') < 0.3;
```

- `distancia(col, VECTOR(x1, ..., xk) [, 'euclidiana' | 'producto_punto' | 'coseno'])`. Sin
  tercer argumento usa la euclidiana. También se aceptan los nombres de FAISS (`l2`,
  `inner_product`, `cosine`).
- Es la misma función de la Parte 2: el tipo de la columna decide si mide metros entre `POINT`
  o la métrica de vectores. La dimensión del vector de consulta tiene que ser la de la columna.
- En el `WHERE` se compara con `<`, `<=`, `>` o `>=` contra un número (entero o decimal) y se
  combina con otras condiciones con `AND`. También sirve en `DELETE`.
- En el `ORDER BY` admite `ASC` y `DESC`; con `LIMIT k` da los k vecinos. No se combina con
  `GROUP BY` ni con agregados.
- Los números aceptan notación científica (`1.25e-06`), porque `extraer.py` escribe los vectores
  con `%.8g`. Dentro de `POINT(...)` sigue sin aceptarse.

### Formato en el CSV de COPY

`extraer.py` escribe un CSV `id,vector` con cada vector entre comillas, y `COPY` lo lee
directamente:

```text
id,v
1,"VECTOR(0.5,1.25e-06,-3,4)"
```

También acepta `"[0.5, 1.25e-06, -3, 4]"` o los números separados por espacios. Si una fila trae
otra dimensión o un valor que no es número, el error dice la fila y la columna (sin repetir los
cientos de números del campo). `CREATE TABLE ... FROM FILE` no infiere `VECTOR`: para vectores
se crea la tabla con su esquema y se carga con `COPY`.

## 4. Métricas

Están en `motor/vectorial/vector.h` (contrato de #50), compartido con las pruebas:

| Métrica | Valor que devuelve | Rango |
|---|---|---|
| `euclidiana` (por defecto) | ‖a − b‖ = √Σ(aᵢ − bᵢ)² | [0, ∞) |
| `producto_punto` | −(a · b) | (−∞, ∞) |
| `coseno` | 1 − (a · b) / (‖a‖ ‖b‖) | [0, 2] |

En las tres, **menor es más parecido**. El producto punto y el coseno son similitudes (mayor es
mejor), así que se cambian de signo o se restan de 1. Con una sola convención, el mismo k-NN
con heap sirve para las tres métricas, `ORDER BY distancia ASC` siempre da los más parecidos
primero, y el adaptador de FAISS (`api/multimedia/indices.py`) convierte sus resultados a esta
misma escala para que la comparación de Recall@10 sea justa.

- El coseno de un vector cero no está definido; se toma como 1 (ni parecido ni opuesto).
- Si los vectores se normalizan a norma 1 (`normalizar` en C++, `faiss.normalize_L2` en Python),
  las tres métricas ordenan los vecinos igual: ‖a − b‖² = 2 − 2(a · b) y el coseno es el producto
  punto. Lo verifica `motor/pruebas/vectorial_test.cpp` con 500 vectores.
- `extraer.py` ya entrega los histogramas TF-IDF con norma 1, así que con los vectores de las
  imágenes las tres métricas dan los mismos vecinos. Se usa el coseno porque es la métrica
  pensada para histogramas (compara la proporción de palabras visuales y no cuántas tiene cada
  imagen) y porque sigue siendo correcta si algún vector llega sin normalizar.

## 5. k-NN secuencial

Sin índice, la consulta exacta tiene que medir la distancia a todas las filas. Lo que se puede
elegir es cuánta memoria y cuánto ordenamiento cuesta. El ejecutor tiene dos caminos:

| Consulta | Vía | Qué hace |
|---|---|---|
| `ORDER BY distancia(v, ...) LIMIT k`, sin `WHERE`, ascendente | **Seq Vector Scan** (`Via::VECTORIAL`) | una pasada por la tabla con un heap de k filas |
| con `WHERE`, `DESC` o sin `LIMIT` | Seq Scan + Filter + Sort | lee todo, filtra y ordena con el external merge sort |

### Seq Vector Scan

1. Se recorre la tabla fila por fila (`Almacen::recorrer`), sin cargarla entera en memoria.
2. Para cada fila se calcula la distancia al vector de consulta.
3. Se mantiene un **max-heap** con las k mejores filas vistas: arriba queda la peor de las k. Si
   la fila nueva está más cerca que esa, la reemplaza; si no, se descarta.
4. Al final se ordena el heap y las filas salen de menor a mayor distancia, así que no hace falta
   un nodo `Sort` aparte.

Costo con N filas de dimensión d: N·d operaciones para las distancias y O(N log k) para el heap,
con k filas en memoria en vez de N. Las páginas leídas son las de toda la tabla, igual que un
`Seq Scan`; es justo lo que IVF y HNSW buscan evitar.

Con `WHERE` no se usa el heap porque habría que aplicar el filtro antes de elegir los k (si no,
el heap podría quedarse con filas que el filtro después descarta y devolver menos de k). En ese
caso el plan es el general, que da el mismo resultado.

### EXPLAIN

```text
EXPLAIN ANALYZE SELECT id FROM img ORDER BY distancia(v, VECTOR(...), 'coseno') LIMIT 10;

Projection on img (actual rows=10)
Output: id
  -> Seq Vector Scan on img  (cost=6667.00 rows=10) (actual time=77.502 rows=10 pages=6667)
     Order By: distancia(v, VECTOR(256 dimensiones)) [coseno] ASC LIMIT 10
```

- El plan no imprime los 256 números del vector de consulta: solo su dimensión.
- El costo estimado es el número de páginas de la tabla, como en un `Seq Scan`.
- Los detalles del nodo incluyen la métrica, k y las distancias calculadas, que siempre son N. Ese número es el que se compara con los nodos
  visitados de HNSW y las listas recorridas de IVF.

### Medición

Con 20 000 vectores aleatorios de dimensión 256 en un heap file (3 registros de 1 KB por página
de 4 KB, 6 667 páginas), la consulta de los 10 vecinos por coseno tarda unos 78 ms, y el vecino
más cercano de una fila de la tabla es ella misma. Con los vectores reales de los 100 000
productos de ABO, el k-NN secuencial tarda unos 488 ms por consulta (lee las 33 334 páginas de
la tabla), mientras que IVF con `nprobe = 8` y HNSW con `ef_busqueda = 64` responden en menos
de 1 ms con un Recall@10 de entre 0,89 y 0,98
([comparacion_vectorial/README.md](comparacion_vectorial/README.md)).

## 6. Relación con IVF, HNSW y SIMILAR_TO

- Los índices IVF y HNSW son los de FAISS y viven en la API (decisión del 26/10, ver
  `docs/contratos_p3_p4.md`). El motor no enlaza FAISS: comparten los vectores y las claves
  primarias.
- Este k-NN secuencial es el resultado exacto: el Recall@10 de IVF y HNSW se mide contra él (o
  contra `IndexFlat` de FAISS, que también recorre todos los vectores y da los mismos vecinos).
- `SIMILAR_TO('foto.jpg', k=10)` (#62) se traduce a esta misma consulta: la API extrae el vector
  de la imagen, lo escribe como literal `VECTOR(...)` y, sin `USING IVF | HNSW`, el motor lo
  resuelve con el Seq Vector Scan.

## 7. Pruebas

`motor/pruebas/sql_test.cpp` (incluido en `make test`):

- **FLOAT** (`prueba_float`): float32 en las tres organizaciones, `WHERE`, `ORDER BY`,
  agregados, `COPY` y errores de tipo.
- **VECTOR** (`prueba_vector`): guardar y leer sin perder precisión en las tres organizaciones,
  `DESCRIBE`, `COPY` con los tres formatos, dimensión equivocada, `VECTOR(513)`, `VECTOR(512)`
  en B+ agrupado, clave primaria e índice rechazados, y reapertura del catálogo.
- **k-NN** (`prueba_knn_vectorial`): 300 vectores de dimensión 16 y tres consultas, con las
  tres métricas y en las tres organizaciones. El resultado del Seq Vector Scan, el de
  `WHERE ... ORDER BY ... LIMIT`, el de `DESC` y el conteo de un rango por distancia coinciden
  con la fuerza bruta calculada en la prueba; además, una fila es su propio vecino más cercano y
  `DELETE ... WHERE distancia(...) < x` borra solo esa fila.

`motor/pruebas/vectorial_test.cpp` verifica las métricas con valores hechos a mano
(euclidiana 5, producto punto −25), la simetría y la equivalencia con vectores normalizados.
