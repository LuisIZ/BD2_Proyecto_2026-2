# Índice IVF (Inverted File Index)

Índice para buscar los k vectores más parecidos (k-NN) sin compararlos todos. Se usa en la
Parte 4 para los histogramas de palabras visuales de las imágenes (`VECTOR(256)`). No se ve
en clase; este documento resume cómo funciona y qué significan sus parámetros.

El profesor aprobó usar el `IndexIVFFlat` de FAISS en lugar de implementarlo (ver
`docs/contratos_p3_p4.md`). El adaptador está en `api/multimedia/indices.py`:

```python
from multimedia.indices import IndiceVectorial
indice = IndiceVectorial("IVF", "cosine", nlist=316, nprobe=4)
indice.construir(vectores, ids)    # numpy float32 (n, 256) e ids = PK de productos
indice.knn(consulta, 10)           # [(id, distancia), ...] de menor a mayor distancia
indice.guardar("productos.ivf")
```

## Idea

Es el mismo truco que el índice invertido de texto, pero con vectores. En texto, cada
término apunta a la lista de documentos que lo contienen, y una consulta solo recorre las
listas de sus términos. En el IVF, el espacio se parte en `nlist` regiones (celdas de
Voronoi), cada una con su centroide, y cada región tiene la lista de vectores que caen en
ella. Una consulta solo recorre las listas de las `nprobe` regiones más cercanas.

Sivic y Zisserman (2003) aplicaron el archivo invertido a las imágenes. Jégou, Douze y
Schmid (2011) lo usaron como "cuantizador grueso" delante de la búsqueda exacta: el IVF
descarta la mayoría de los vectores y solo los de las listas elegidas se comparan de verdad.

```mermaid
flowchart LR
    Q["consulta q"] --> C["1. comparar q con los<br/>nlist centroides"]
    C --> P["2. elegir los nprobe<br/>centroides más cercanos"]
    P --> L["3. recorrer solo esas listas<br/>(id, vector)"]
    L --> H["4. heap de tamaño k<br/>con las distancias exactas"]
    H --> R["top-k de menor a mayor"]
```

## Construcción

1. **Entrenamiento (k-means de Lloyd).** Se agrupan los vectores en `nlist` grupos:
   se eligen `nlist` centroides al azar, cada vector se asigna al centroide más cercano y
   cada centroide se mueve al promedio de sus vectores, y así durante 10 iteraciones
   (`cp.niter`, el valor que usa `IndexIVFFlat`).
   FAISS entrena con a lo más 256 vectores por centroide (submuestra el resto) y avisa si
   hay menos de 39 por centroide. La semilla es fija (`cp.seed = 2026`), así que el índice
   sale igual en cada corrida.
2. **Cuantizador grueso.** Los `nlist` centroides se guardan en un índice plano
   (`IndexFlatL2` o `IndexFlatIP`, según la métrica) que decide a qué lista va cada vector.
3. **Llenado de las listas.** Cada vector se agrega a la lista de su centroide más cercano,
   junto con su id (la PK de la tabla, no su posición).

Insertar después de construir es lo mismo que el paso 3: el vector va a la lista de su
centroide más cercano, sin reentrenar. Si se insertan muchos vectores de una zona nueva,
las listas se desbalancean y conviene reconstruir. FAISS también permite borrar por id
(`remove_ids`), que recorre las listas.

## Búsqueda

1. Se compara la consulta con los `nlist` centroides.
2. Se toman los `nprobe` más cercanos.
3. Se recorren solo esas listas y se calcula la distancia exacta a cada vector (`Flat`:
   el vector se guarda completo, sin comprimir).
4. Un heap de tamaño k mantiene los k mejores.

Vectores comparados por consulta ≈ `nlist + nprobe · n / nlist`, contra los `n` de la
fuerza bruta.

**Por qué se pierde recall.** Si el vecino verdadero de la consulta cae en una celda que
no está entre las `nprobe` revisadas (por ejemplo, la consulta está cerca del borde entre
dos celdas), el IVF no lo ve. Con `nprobe = nlist` se revisan todas las listas y el
resultado es exactamente el de la fuerza bruta; lo comprueba
`api/multimedia/test_indices.py`.

## Métricas

En las tres, menor distancia es más parecido, igual que en `motor/vectorial/vector.h`.
FAISS devuelve otras escalas y el adaptador las convierte:

| Métrica | Índice de FAISS | FAISS devuelve | El adaptador devuelve |
|---|---|---|---|
| Euclidiana | `METRIC_L2` | ‖a − b‖² | √ de eso |
| Producto punto | `METRIC_INNER_PRODUCT` | a · b (mayor es mejor) | −a · b |
| Coseno | `METRIC_INNER_PRODUCT` sobre vectores normalizados con `faiss.normalize_L2` | cos(a, b) | 1 − cos |

Con el coseno se normalizan los vectores al construir y la consulta al buscar, así que el
k-means también agrupa por dirección y no por magnitud.

## Parámetros

| Parámetro | Por defecto | Qué controla | Si sube |
|---|---|---|---|
| `nlist` | 100 (el benchmark usa ≈ √n) | cantidad de listas (celdas) | listas más cortas y búsquedas más rápidas, pero más celdas vecinas donde puede caer un vecino, más tiempo de entrenamiento y más centroides que comparar |
| `nprobe` | 8 | listas revisadas por consulta | más recall y más tiempo, lineal en `nprobe`; con `nprobe = nlist` es fuerza bruta |

Regla práctica: `nlist` entre √n y 4√n, y `nprobe` lo más chico que dé el recall que se
necesite (se elige midiendo, como abajo).

## Almacenamiento

`guardar` usa `faiss.write_index`: una cabecera (dimensión, métrica, `nlist`), los
centroides del cuantizador y luego las listas, una tras otra, cada una con sus ids
(`int64`) y sus vectores (`float32`). A diferencia de las estructuras del motor no usa
páginas de 4 KB: FAISS carga el índice completo en memoria y busca ahí.

Tamaño ≈ `n · (4 · K + 8) + nlist · 4 · K` bytes. Con 100 000 vectores de 256 dimensiones
y 316 listas: 103,5 MB, solo un 1 % más que los vectores solos (102,4 MB). El IVF no
ahorra espacio, ahorra comparaciones.

## Resultados

`make bench-ivf` (o `python api/multimedia/bench_ivf.py`) escribe
`datos/resultados/ivf_bench.csv`: 100 consultas con k = 10, métrica coseno, un hilo.
La fuerza bruta es un recorrido completo con NumPy. Mientras no estén los vectores de #57
se usan vectores sintéticos de dimensión 256 agrupados alrededor de 100 centros; con los
reales se corre `--vectores vectores.npy` y el recall será menor, porque las imágenes no
forman grupos tan separados.

| n | nlist | Construcción | Fuerza bruta | nprobe | IVF | Recall@10 | Comparados |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 000 | 32 | 12 ms | 0,51 ms | 1 | 0,033 ms | 0,94 | 71 |
| | | | | 4 | 0,030 ms | 0,97 | 243 |
| | | | | 32 | 0,059 ms | 1,00 | 1 032 |
| 10 000 | 100 | 103 ms | 5,1 ms | 1 | 0,032 ms | 0,94 | 225 |
| | | | | 2 | 0,043 ms | 1,00 | 487 |
| | | | | 8 | 0,085 ms | 1,00 | 1 687 |
| 100 000 | 316 | 1 077 ms | 47,6 ms | 1 | 0,065 ms | 0,63 | 815 |
| | | | | 2 | 0,095 ms | 0,84 | 1 148 |
| | | | | 4 | 0,131 ms | 0,99 | 1 780 |
| | | | | 8 | 0,218 ms | 1,00 | 3 128 |

Lo que se ve:

- **`nprobe` cambia recall por tiempo.** Con 100k, pasar de 1 a 4 listas sube el recall de
  0,63 a 0,99 y el tiempo de 0,065 a 0,131 ms. Más allá de 8 el recall ya no mejora y el
  tiempo sigue creciendo.
- **El IVF escala mejor que la fuerza bruta.** De 1k a 100k la fuerza bruta crece unas 94
  veces (crece con n), y el IVF con `nprobe = 4` unas 4 veces (crece con √n, porque las
  listas miden ≈ n / nlist = √n).
- **La construcción es el costo.** Entrenar el k-means con 100k vectores toma ~1 s; la
  fuerza bruta no necesita construir nada. Para una sola consulta no conviene el IVF.
- **El mismo `nprobe` rinde distinto según n.** Con `nlist ≈ √n`, cada celda tiene más
  vecinas cuando n crece y hacen falta más listas para el mismo recall (con 1k basta
  `nprobe = 1`; con 100k hacen falta 4).

## Fuentes

- J. Sivic y A. Zisserman (2003). *Video Google: A Text Retrieval Approach to Object
  Matching in Videos*. ICCV. Archivo invertido sobre palabras visuales.
- H. Jégou, M. Douze y C. Schmid (2011). *Product Quantization for Nearest Neighbor
  Search*. IEEE TPAMI 33(1). Sección IV: IVF con cuantizador grueso (IVFADC).
- Wiki de FAISS: *Faster search* e *Index IO, cloning and hyper parameter tuning*
  (<https://github.com/facebookresearch/faiss/wiki>), y la documentación de
  `IndexIVFFlat`.
