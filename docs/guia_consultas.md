# Guía de consultas

Estructura de cada consulta que acepta el gestor, según la gramática de `motor/consultas/parser_sql.h`. La vista **Sintaxis** de la web muestra lo mismo.

- `<tabla>`: valor que escribe el usuario.
- `[ ... ]`: opcional.
- `a | b`: una opción u otra.

Las sentencias se separan con `;` y los comentarios `-- ...` llegan hasta el fin de la línea.

## Tablas y carga

```sql
CREATE TABLE <tabla> (<columna> INT PRIMARY KEY, <columna> INT | FLOAT | VARCHAR(<n>) | POINT | VECTOR(<dim>), ...)
  [USING HEAP | SEQUENTIAL | BPLUS];
```
Crea una tabla vacía. `HEAP` (por defecto) guarda en orden de llegada, `SEQUENTIAL` ordena por la clave y `BPLUS` usa un B+ agrupado por la clave.

```sql
CREATE TABLE <tabla> FROM FILE '<archivo.csv>' [USING HEAP | SEQUENTIAL | BPLUS]
  [PRIMARY KEY <columna>] [INDEX (<columna>, ...)];
```
Deduce las columnas del CSV y lo carga. `INDEX` crea un B+ no agrupado por cada columna.

```sql
COPY <tabla> FROM FILE '<archivo.csv>';
```
Carga un CSV en una tabla existente. Los encabezados deben ser sus columnas, en el mismo orden.

```sql
INSERT INTO <tabla> VALUES (<valor>, <valor>, ...);
DELETE FROM <tabla> [WHERE <condición>];
DROP TABLE <tabla>;
SHOW TABLES;
DESCRIBE <tabla>;
```
- `INSERT` agrega una fila.
- `DELETE` borra las filas que cumplen la condición, o todas sin `WHERE`.
- `DROP TABLE` borra la tabla y sus índices.
- `SHOW TABLES` lista las tablas con organización, clave, filas, páginas e índices.
- `DESCRIBE` muestra columnas, clave e índices.

## Índices

```sql
CREATE INDEX <nombre> ON <tabla> (<columna>) [USING BPLUS | HASH | RTREE | SPIMI];
```
Sin `USING` crea un `BPLUS`.

```sql
CREATE INDEX <nombre> ON <tabla> (<columna VECTOR>) USING IVF | HNSW
  [WITH (<parámetro> = <valor>, ...)];
```
Índices de FAISS para `SIMILAR_TO ... USING IVF | HNSW`. Los parámetros omitidos toman su valor por defecto.

| Índice | Columna | Sirve para | Parámetros `WITH` (por defecto) |
|---|---|---|---|
| `BPLUS` | INT, VARCHAR | `=`, `<`, `>`, `BETWEEN` y el `ON` de un JOIN | — |
| `HASH` | INT, VARCHAR | `=` y el `ON` de un JOIN | — |
| `RTREE` | POINT | radio, k vecinos y `dentro(...)` | — |
| `SPIMI` | VARCHAR | `MATCH` | — |
| `IVF` | VECTOR | `SIMILAR_TO ... USING IVF` | `nlist = 100` (regiones del espacio) · `nprobe = 8` (regiones revisadas por consulta) · `metrica = coseno` |
| `HNSW` | VECTOR | `SIMILAR_TO ... USING HNSW` | `M = 32` (vecinos por nodo) · `ef_construccion = 40` (candidatos al insertar) · `ef_busqueda = 64` (candidatos al buscar) · `umbral_reconstruccion = 0.3` (fracción de borrados que reconstruye el índice) · `metrica = coseno` |

## Relacionales

```sql
SELECT * | <columna>, ... FROM <tabla>
  [WHERE <columna> = | != | <> | < | <= | > | >= <valor> [AND ...]]
  [ORDER BY <columna> [ASC | DESC]] [LIMIT <n>];
```
Condiciones unidas con `AND` (no hay `OR`). Usa la clave o un índice B+ o HASH si existe; si no, recorre la tabla.

```sql
SELECT * FROM <tabla> WHERE <columna> BETWEEN <desde> AND <hasta>;
```
Rango cerrado. Usa la clave en `SEQUENTIAL` o `BPLUS`, o un índice B+. El HASH solo sirve para `=`.

```sql
SELECT <columna>, COUNT(*) | SUM(<columna>) | AVG(<columna>) | MIN(<columna>) | MAX(<columna>) [AS <alias>]
  FROM <tabla> [WHERE ...] GROUP BY <columna> [ORDER BY <alias> [DESC]] [LIMIT <n>];
```
Agrupa con external hashing en disco. Sin `GROUP BY`, resume toda la tabla.

```sql
SELECT <x>.<columna>, <y>.<columna>, ... FROM <tabla_a> [AS] <x> [INNER] JOIN <tabla_b> [AS] <y>
  ON <x>.<columna> = <y>.<columna> [WHERE ...] [GROUP BY ...] [ORDER BY ...] [LIMIT <n>];
```
Une dos tablas por igualdad. Usa index nested loop si alguna tiene índice o clave en su columna del `ON`; si no, hash join.

## Espaciales

```sql
SELECT * FROM <tabla> WHERE distancia(<columna POINT>, POINT(<lat>, <lon>) [, 'haversine' | 'euclidiana']) <= <metros>;
```
Puntos dentro de un radio en metros (entero), con el RTREE. Haversine por defecto.

```sql
SELECT * FROM <tabla> ORDER BY distancia(<columna POINT>, POINT(<lat>, <lon>)) LIMIT <k>;
```
Los k puntos más cercanos, con el RTREE.

```sql
SELECT * FROM <tabla> WHERE dentro(<columna POINT>, POLYGON((<lat> <lon>, <lat> <lon>, <lat> <lon>, ...)));
```
Puntos dentro del polígono. El RTREE descarta lo que cae fuera de su rectángulo.

## Texto

```sql
SELECT *, SCORE() [AS <alias>] FROM <tabla>
  WHERE [<condición> AND ...] MATCH(<columna VARCHAR>, '<consulta>') USING TF_IDF | BM25
  [ORDER BY <alias> DESC] [LIMIT <n>];
```
Búsqueda por palabras con el índice SPIMI. `USING` es obligatorio y elige el ranking; `SCORE()` es la relevancia. `MATCH` va una sola vez y no se combina con JOIN ni con `SIMILAR_TO`.

## Multimedia

```sql
SELECT <columnas>, SIMILARITY_SCORE() [AS <alias>] FROM <tabla>
  WHERE SIMILAR_TO([<columna VECTOR>,] '<imagen o audio>' | VECTOR(<x1>, ..., <xn>) [, k = <n>]) [AND <condición> ...]
  [USING IVF | HNSW] [WITH METRIC = coseno | euclidiana | producto_punto];
```
Los k más parecidos (10 por defecto) a una imagen, un audio o un vector.
- El archivo se busca en las subidas y en el dataset.
- Con `USING` busca en FAISS; sin `USING` compara contra todas las filas.
- Métrica coseno por defecto; debe ser la misma del índice.
- `SIMILARITY_SCORE()` es mayor cuanto más parecido.

```sql
SELECT * FROM <tabla> ORDER BY distancia(<columna VECTOR>, VECTOR(<x1>, ..., <xn>)
  [, 'euclidiana' | 'producto_punto' | 'coseno']) LIMIT <k>;
```
Los k vectores más cercanos, sin FAISS. Euclidiana por defecto.

```sql
SELECT * FROM <tabla> WHERE distancia(<columna VECTOR>, VECTOR(<x1>, ..., <xn>) [, '<métrica>']) < <número>;
```
Vectores a menos de cierta distancia.

## Transacciones y EXPLAIN

```sql
BEGIN [TRANSACTION];
  <sentencias>
COMMIT; | ROLLBACK;
```
`COMMIT` (o `END`) guarda los cambios; `ROLLBACK` los deshace.

```sql
EXPLAIN <sentencia>;
EXPLAIN ANALYZE <sentencia>;
```
`EXPLAIN` muestra el plan sin ejecutar. `EXPLAIN ANALYZE` lo ejecuta y añade filas, páginas y tiempo por nodo. Detalle en [parser_sql.md](parser_sql.md).
