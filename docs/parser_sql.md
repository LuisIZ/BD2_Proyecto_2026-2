# Parser SQL, ejecutor y cliente

## 1. Archivos

| Archivo | Qué es |
|---|---|
| `motor/consultas/valor.h` | `Valor` (entero o texto) y `Fila`. |
| `motor/consultas/parser_sql.{h,cpp}` | Lexer + parser de descenso recursivo. Devuelve una `Sentencia`. |
| `motor/consultas/catalogo.{h,cpp}` | Esquema de tablas, índices, serialización de filas a bytes y persistencia en `catalogo.txt`. |
| `motor/consultas/ejecutor.{h,cpp}` | Planificador de acceso y ejecución sobre Heap, Sequential File, B+ agrupado, B+ no agrupado y hash extensible. ORDER BY y GROUP BY usan `external_algorithms.h`. |
| `motor/consultas/motor_sql.cpp` | CLI: recibe SQL, responde JSON. |
| `motor/pruebas/sql_test.cpp` | Parser + ejecutor end-to-end con las tres organizaciones e índice secundario. |
| `api/motor_cli.py` | Envuelve el binario desde Python (compila si hace falta). Base para la API REST. |
| `api/ui_sql.py` | Cliente Tkinter con los 4 paneles: tablas, consulta, resultados, plan. |

## 2. Cómo correr

```bash
make gui                                   # compila .build/motor_sql y abre la interfaz
make test                                  # pruebas, incluida sql_test
python3 api/ui_sql.py                      # sin make: la UI compila el motor por su cuenta
python3 api/motor_cli.py "SHOW TABLES"     # consola
```

En Windows: `mingw32-make gui` (o `python api/ui_sql.py`).

Requisitos: `g++` en el PATH y Python 3 con Tkinter (viene con el instalador oficial de
Python en Windows; en Debian/Ubuntu `apt install python3-tk`). La base vive en
`datos/db/` (ignorada por git): `catalogo.txt` más un archivo por tabla e índice.

El binario también se usa directo:

```bash
.build/motor_sql --db datos/db --sql "SELECT * FROM t WHERE Index = 5; SHOW TABLES"
.build/motor_sql --db datos/db --catalogo
echo "SELECT COUNT(*) FROM t" | .build/motor_sql --db datos/db
```

## 3. Gramática

Palabras clave sin distinguir mayúsculas. Varias sentencias se separan con `;`.

```sql
CREATE TABLE t (col INT [PRIMARY KEY], col VARCHAR(n), col POINT, ...) [USING HEAP | SEQUENTIAL | BPLUS]
CREATE TABLE t FROM FILE 'ruta.csv' [USING ...] [PRIMARY KEY col] [INDEX (col, ...)]
COPY t FROM FILE 'ruta.csv'
CREATE INDEX nombre ON t (col) [USING BPLUS | HASH | RTREE]
DROP TABLE t
INSERT INTO t VALUES (v1, v2, ...)
DELETE FROM t [WHERE cond]
SELECT * | col, COUNT(*), SUM(col), AVG(col), MIN(col), MAX(col)
  FROM t [WHERE cond [AND cond]...] [GROUP BY col]
  [ORDER BY col | distancia(col, POINT(lat, lon) [, metrica]) [ASC|DESC]] [LIMIT n]
SHOW TABLES
DESCRIBE t
EXPLAIN [ANALYZE] <sentencia>
BEGIN [TRANSACTION] | END [TRANSACTION] | COMMIT | ROLLBACK

cond  := col (= | != | <> | < | <= | > | >=) valor
       | col BETWEEN a AND b
       | distancia(col, POINT(lat, lon) [, 'haversine' | 'euclidiana']) (< | <= | > | >=) metros
       | dentro(col, POLYGON((lat lon, lat lon, lat lon, ...)))
valor := entero | 'texto' | POINT(lat, lon)
```

Tipos: `INT` (int32), `VARCHAR(n)` y `POINT` (latitud y longitud; ver
[sql_espacial.md](sql_espacial.md)). La clave primaria debe ser `INT` y puede ser
cualquier columna, no solo la primera (`codigo INT PRIMARY KEY` en su definición, o
`PRIMARY KEY codigo` al final); si no se indica se toma la primera columna `INT`.
`FROM FILE` infiere el esquema del CSV: `INT` si toda la columna son enteros, si no
`VARCHAR` del largo máximo; los nombres de columna se
normalizan a identificadores (`Organization Id` → `Organization_Id`). `INDEX (a, b)`
construye un B+ no agrupado por columna cuando la carga termina, y equivale a lanzar un
`CREATE INDEX` por cada una; solo vale sobre tablas `HEAP`.

`COPY t FROM FILE 'ruta.csv'` carga datos en una tabla que **ya existe**, y así separa la
definición del esquema de la carga. Los encabezados del CSV deben coincidir en nombre y
orden con las columnas de la tabla. Si la tabla está vacía y no tiene índices se usa carga
masiva; si ya tiene datos, una inserción por fila para mantener el orden y los índices.
`CREATE TABLE ... FROM FILE` es el atajo que hace las dos cosas de una vez infiriendo el
esquema, y sigue disponible.

## 4. Cómo se guarda una tabla

| `USING` | Estructura | Formato de fila | Índices secundarios |
|---|---|---|---|
| `HEAP` (defecto) | `HeapFile`, páginas slotted | variable: clave aparte, resto empaquetado (`INT` 4 B, `VARCHAR` 2 B largo + bytes) | sí, B+ no agrupado o hash extensible sobre columnas `INT` |
| `SEQUENTIAL` | `SequentialFile` | igual que heap (mismos bytes por fila) | no: la reorganización reubica registros |
| `BPLUS` | `BPlusAgrupado` | fijo: `[pk][INT 4 B \| VARCHAR n B]...` | no: la fila vive en la hoja |

La carga con `FROM FILE` y `COPY` usa `cargar_masivo` en las tres organizaciones: el B+
agrupado construye las hojas al 90 %, el secuencial escribe el área principal de una vez
(sin auxiliares ni reorganizaciones) y el heap llena cada página en memoria antes de
escribirla. Insertar fila a fila también funciona, pero tardaba entre 2 y 10 veces más;
ver [carga_masiva.md](carga_masiva.md). La clave primaria se verifica siempre al insertar: el B+
agrupado la rechaza solo; en heap se consulta el índice sobre la clave si existe (si no,
recorrido completo); en secuencial, búsqueda binaria.

## 5. Planificador

Para cada `SELECT` o `DELETE` se elige **una** condición del `WHERE` que pueda
resolverse con una estructura y el resto se filtra en memoria:

| Condición | Heap | Secuencial | B+ agrupado |
|---|---|---|---|
| columna con índice B+, `=` | `busqueda_por_indice` | — | — |
| columna con índice B+, `BETWEEN` `<` `<=` `>` `>=` | `rango_por_indice` | — | — |
| columna con índice hash, `=` | `busqueda_por_indice` | — | — |
| columna con índice hash, rango | `scan_completo` + `filtro` (el hash no guarda orden) | — | — |
| clave primaria, `=` | `scan_completo` (no hay índice) | `busqueda_por_clave` | `busqueda_por_clave` |
| clave primaria, rango | `scan_completo` | `rango_por_clave` | `rango_por_clave` |
| cualquier otra | `scan_completo` + `filtro` | `scan_completo` + `filtro` | `scan_completo` + `filtro` |

Después: `agrupacion` (`ExternalHashAggregate`), `ordenamiento` (`ExternalMergeSort`
k-way, reporta `initial_runs` y `merge_passes`), `proyeccion` y `limite`.

La decisión vive en `planificar()`, que solo mira el catálogo y no abre ningún archivo de
datos. El ejecutor obedece esa decisión y mide lo que costó. Gracias a esa separación,
`EXPLAIN` sin `ANALYZE` puede mostrar el mismo plan que correrá la consulta sin leer una
sola página.

## 5.1. EXPLAIN

El plan se arma en orden de ejecución y se invierte al final, así que se lee como en
PostgreSQL: el nodo 0 es la raíz (lo último que corre) y el más profundo es el acceso a
disco. Como el motor no hace `JOIN`, el árbol siempre es una cadena.

```
EXPLAIN SELECT Index, Name FROM org_idx WHERE Index BETWEEN 10000 AND 13000;

Index Range Scan using org_idx_index on org_idx (Index)  (cost=3004.00 rows=3001)
   Index Cond: Index BETWEEN 10000 AND 13000
   Nota: el indice no agrupado lee una pagina de datos por fila encontrada
Nota: costo en paginas estimadas; rows supone claves densas y repartidas parejo
Planning Time: 0.412 ms
```

- Un nodo que usa un índice dice cuál y sobre qué columna:
  `Index Scan using idx_x on tabla (columna)`.
- `cost` son **páginas estimadas**, no la unidad arbitraria de PostgreSQL.
- `rows` son filas estimadas. Para un rango sobre la clave se supone que las claves son
  densas y están repartidas parejo, que es lo único que el catálogo permite suponer;
  para un filtro sin estructura se estima que pasa el 10 %.
- El costo de un acceso por índice no agrupado cuenta **la altura del árbol más una
  página de datos por fila**, que es justo lo que lo vuelve caro en rangos anchos.

Con `ANALYZE` la consulta se ejecuta de verdad y cada nodo añade lo medido:

```
EXPLAIN ANALYZE SELECT Country, COUNT(*) FROM org_idx WHERE Founded = 2000 GROUP BY Country LIMIT 5;

Limit on org_idx  (rows=5) (actual rows=5)
  -> HashAggregate on org_idx (actual time=0.825 rows=243)
     Group Key: Country
    -> Index Scan using org_idx_founded on org_idx (Founded)  (cost=103.00 rows=100) (actual time=5.458 rows=1940 pages=1962)
       Index Cond: Founded = 2000
Rows: 5
Planning Time: 0.507 ms
Execution Time: 20.202 ms
```

Comparar `rows` con `actual rows` muestra dónde falló la estimación: aquí el índice
esperaba 100 filas y encontró 1 940, y por eso leyó casi 2 000 páginas.

Los tiempos van siempre al final, como en PostgreSQL:

- `Planning Time` cuenta el parseo de la sentencia, la validación de tablas y columnas y
  la decisión del planificador.
- `Execution Time` cuenta solo la ejecución del árbol de operadores, así que nunca es
  menor que la suma de los `actual time` de sus nodos.

Los dos se miden con `std::chrono::steady_clock` y se muestran con 3 decimales.

`EXPLAIN` a secas solo describe `SELECT` y `DELETE`; para el resto hace falta `ANALYZE`,
porque el plan de un DDL es lo que hace al ejecutarse. `EXPLAIN` abre la tabla para leer
sus estadísticas, igual que PostgreSQL consulta `pg_class`; en un heap eso implica leer su
directorio de páginas, y por eso el `Planning Time` de un heap grande no es cero.

## 6. Formato JSON

```json
{"ok":true,"tipo":"select","mensaje":"12 filas","afectadas":12,"tiempo_ms":0.3,
 "planificacion_ms":0,"ejecucion_ms":0,"analizado":false,
 "columnas":["Index","Name"],"filas":[[1,"Acevedo LLC"],...],
 "plan":[{"operacion":"rango_por_clave","estructura":"secuencial","paginas_leidas":"15","filas":"12",
          "nodo":"Ordered Key Scan","relacion":"demo","indice":"","columna_indice":"Index",
          "cond":"Index BETWEEN 1 AND 12","nivel":1,"costo":15,"filas_estimadas":12,
          "filas_reales":12,"nodo_ms":0.21,"nodo_paginas":15,"nodo_paginas_escritas":-1}]}
```

`planificacion_ms` y `ejecucion_ms` son los mismos valores que `Planning Time` y
`Execution Time`; fuera de `EXPLAIN` quedan en 0 y el tiempo total va en `tiempo_ms`.

Los detalles de cada paso siguen yendo planos dentro del objeto. Los campos nuevos
describen el nodo al estilo de `EXPLAIN`: `nodo` es su nombre visible, `indice` y
`columna_indice` dicen qué índice se usa y sobre qué columna, `nivel` es la profundidad en
el árbol (0 = raíz) y los valores `-1` significan "no aplica" o "no se midió".

Errores: `{"ok":false,"error":"la columna Foo no existe en demo"}`. Con `--sql` la
respuesta es un arreglo con un objeto por sentencia. `--catalogo` devuelve tablas,
columnas e índices para el panel de archivos.

## 7. Límites conocidos

- Sin `UPDATE`, sin `JOIN`, sin `OR`, sin subconsultas. `WHERE` solo une con `AND`.
- Agregados solo sobre columnas `INT`; `AVG` devuelve texto con dos decimales.
- `CREATE INDEX ... USING HASH` crea un hash extensible paginado en disco
  (`motor/indices/hash_extensible_disco.h`, archivo `<tabla>__<indice>.hash`): directorio y
  buckets en páginas de 4 KB, 255 entradas por bucket y cadenas de desborde cuando muchas
  claves caen en el mismo bucket. Solo resuelve igualdad. Su profundidad global, igual que
  la del hash en memoria, está limitada a 16 bits (65 536 entradas de directorio).
- `CREATE INDEX ... USING RTREE` crea un R-Tree sobre una columna `POINT` de una tabla
  `HEAP`; el planificador lo usa para el radio y el k-NN (ver [sql_espacial.md](sql_espacial.md)).
- Claves repetidas: la clave primaria es única; el resto de columnas admite repetidos.
- Cada llamada al binario abre y cierra los archivos: todo queda en disco entre
  sentencias, pero la caché del B+ arranca fría en cada consulta.
- Las transacciones viven dentro de un lote y los locks son por tabla; ver
  [transacciones.md](transacciones.md).
