# Algoritmos externos de consultas en C++17

## 1. Implementacion

Los algoritmos C++ estan en:

- `motor/consultas/external_algorithms.h`: templates de ordenamiento, joins y
  `PlanTrace`.
- `motor/consultas/hash_externo.h`: el GROUP BY con external hashing en disco
  (seccion 4).
- `motor/consultas/external_algorithms.cpp`: unidad de compilacion.
- `motor/pruebas/external_algorithms_test.cpp`: pruebas principales.

Los templates reciben tipos concretos de registro y clave:

```cpp
motor::PlanTrace traza;
motor::ExternalMergeSort<int, int> sort(10, 1000, 9, &traza);
```

## 2. Paginas y presupuesto

El sort modela el presupuesto mediante `buffer_pages`, `records_per_page` y
`k`: sus runs todavia viven en `std::vector`, asi que `records_per_page` es un
limite de memoria reproducible, no una serializacion real. El GROUP BY ya
escribe sus particiones en paginas de 4 KB con `ArchivoTemporal` (seccion 4).

Para el CSV de prueba:

| Medida | Valor |
|---|---:|
| Filas | 100 000 |
| Fila promedio CSV | 140.17 bytes |
| Fila maxima CSV | 220 bytes |
| Payload total | 14 017 171 bytes |
| Pagina logica de 4 KiB | 25-27 filas con slots |
| Pagina logica de 8 KiB | 50-53 filas con slots |

El rango practico descuenta 16-32 bytes por slot para longitud, offset,
estado y alineamiento. En C++, `std::string`, capacidad reservada y allocator
pueden aumentar el costo real.

### Archivos temporales

`motor/consultas/archivo_temporal.h` (solo cabecera) es la base para que el
sort, el hashing y el join escriban sus runs y particiones en disco. La clase
`ArchivoTemporal` guarda filas (`Fila`, un vector de `Valor`) en paginas de
4 KB:

```text
pagina := [n filas: uint16] [fila 1] [fila 2] ... [ceros hasta 4096 B]
fila   := [n valores: uint16] y por valor [tipo: uint8] [datos]
          INT: int64 | FLOAT: float32 | VARCHAR: uint16 + bytes
          POINT: 2 int32 | VECTOR: uint16 + float32 x n
```

`codificar_fila` y `decodificar_fila` guardan el tipo de cada valor, asi que
no dependen del esquema de una tabla: sirven igual para las filas de un JOIN.
Si una fila ocupa mas de 4094 bytes se lanza `la fila no cabe en una pagina`.

```cpp
motor::PlanTrace traza;
motor::sql::ArchivoTemporal run(base_datos, &traza);  // <base>/tmp/temporal_*.pag
for (const auto& fila : filas) run.agregar(fila);     // vuelca cada pagina llena
run.cerrar_escritura();                               // escribe la ultima pagina
motor::sql::Fila fila;
while (run.siguiente(fila)) { /* carga una pagina a la vez */ }
run.reiniciar_lectura();                              // otra pasada desde el inicio
```

- `paginas_escritas()` y `paginas_leidas()` cuentan las paginas de ese archivo;
  si se pasa una `PlanTrace`, sus campos `paginas_escritas` y `paginas_leidas`
  suman las de todos los temporales de la consulta, para mostrarlas en el
  `EXPLAIN`.
- El archivo se borra en el destructor (RAII), tambien si la consulta termina
  con una excepcion. Se puede mover (por ejemplo, a un `std::vector` de runs)
  y lo borra el ultimo dueno.
- En memoria solo vive la pagina en curso: 4 KB para escribir y 4 KB para leer.

`motor/pruebas/archivo_temporal_test.cpp` escribe y relee 10 000 filas con los
5 tipos (231 paginas), compara cada fila, cuenta las paginas contra el calculo
esperado y comprueba que el archivo se borra y que los errores son claros.

## 3. External Merge Sort

`ExternalMergeSort<Registro, Clave>` calcula:

```text
chunk_size = buffer_pages * records_per_page
```

Cada chunk se ordena con `std::sort` y se convierte en un run logico. En la
prueba de 100 000 enteros:

```text
buffer_pages = 10
records_per_page = 1000
chunk_size = 10000
initial_runs = ceil(100000 / 10000) = 10
k = 9
```

Como 10 runs superan `k`, se realiza una pasada intermedia y luego el merge
final.

El merge usa `std::priority_queue` como heap de minimos. Cada entrada guarda
clave, indice del run y posicion dentro del run. El costo esperado es:

```text
O(N log k)
```

El heap usa aproximadamente `O(k)` elementos, sin contar los vectores de runs
que esta version conserva en memoria.

La traza registra `buffer_pages`, `records_per_page`, `k`, `initial_runs`,
`merge_passes` y `records`.

## 4. External Hash Aggregation en disco

`GROUP BY` (y los agregados sin `GROUP BY`) se resuelve con
`ExternalHashAggregate` de `motor/consultas/hash_externo.h`, sobre filas del
motor (`Fila`) y con `ArchivoTemporal` para las particiones. Con **B** paginas
de memoria (`PAGINAS_GROUP_BY = 10` en `ejecutor.cpp`):

1. **Particion.** Cada fila se reduce al valor del grupo y a las columnas que
   piden los agregados (`[grupo, v1, v2, ...]`) y se escribe en la particion
   `hash(grupo, nivel) % P`, con **P = B - 1**. Cada particion es un
   `ArchivoTemporal` con una sola pagina de buffer: cuando se llena, se
   escribe a disco. El hash es FNV-1a sobre el texto del grupo, mezclado con el
   nivel, asi que cada nivel reparte distinto.
2. **Agregacion.** Se lee una particion a la vez y se agrega con una tabla hash
   en memoria. Como el hash manda todas las filas de un grupo a la misma
   particion, cada grupo se completa dentro de su particion y se emite apenas
   termina; el archivo se borra en ese momento.
3. **Volver a particionar.** La tabla hash de una particion puede ocupar hasta
   B paginas: cada grupo cuenta su clave codificada mas 32 bytes por agregado
   (`EstadoAgregado`). Si se pasa, la particion se relee y se reparte en otras
   P con la semilla del nivel siguiente, hasta **3 niveles**. Si aun asi no
   cabe, la consulta falla con
   `GROUP BY: una particion no cabe en B paginas de memoria despues de 3 niveles`.
   El limite es sobre los grupos y no sobre las filas: un solo grupo con
   millones de filas cabe, porque ocupa una entrada.
4. **Una sola pasada.** `COUNT`, `SUM`, `AVG`, `MIN` y `MAX` de todo el `SELECT`
   se acumulan en la misma pasada (un `EstadoAgregado` por agregado). El valor
   de la columna agrupada sale de la particion; ya no hay un `std::map` con
   todos los grupos.

Las particiones salen en el orden del hash. Para no cambiar resultados que antes
salian ordenados, sin `ORDER BY` el ejecutor ordena la salida por el valor del
grupo; eso ordena una fila por grupo, no las filas de entrada.

`EXPLAIN ANALYZE` muestra las medidas en el nodo `HashAggregate`:

```text
HashAggregate on p10k (actual time=207.569 rows=10000)
Group Key: id
particiones=90  niveles=2  grupos_en_memoria=953  paginas_escritas=135  paginas_leidas=180
  -> Seq Scan on p10k  (cost=1939.00 rows=10000) (actual time=22.292 rows=10000 pages=1939)
```

- `particiones`: archivos temporales creados en todos los niveles (9 por cada
  reparto).
- `niveles`: 1 si ninguna particion se tuvo que volver a repartir.
- `grupos_en_memoria`: la tabla hash mas grande que hubo a la vez.
- `paginas_escritas` y `paginas_leidas`: paginas de 4 KB de las particiones.
  Se leen mas de las que se escriben cuando una particion se relee para
  repartirla.

### Medicion

`EXPLAIN ANALYZE` sobre `productos_10k.csv` y `productos_100k.csv` cargados con
`CREATE TABLE ... FROM FILE` (heap), B = 10, el mejor de 3 corridas, en Windows
con el repositorio en OneDrive. El tiempo es el del nodo `HashAggregate`, sin
la lectura de la tabla.

| Filas | Consulta | Grupos | Particiones | Niveles | Grupos en memoria | Paginas escritas | Paginas leidas | Tiempo |
|---:|---|---:|---:|---:|---:|---:|---:|---:|
| 10 000 | `tipo, COUNT(*)` | 328 | 9 | 1 | 49 | 73 | 73 | 56 ms |
| 10 000 | `marca, COUNT(*), MIN(id), MAX(id)` | 190 | 9 | 1 | 26 | 124 | 124 | 67 ms |
| 10 000 | `id, COUNT(*)` | 10 000 | 90 | 2 | 953 | 135 | 180 | 208 ms |
| 100 000 | `tipo, COUNT(*)` | 517 | 9 | 1 | 65 | 705 | 705 | 135 ms |
| 100 000 | `marca, COUNT(*), MIN(id), MAX(id)` | 360 | 9 | 1 | 46 | 1 203 | 1 203 | 160 ms |
| 100 000 | `id, COUNT(*)` | 100 000 | 819 | 3 | 953 | 1 756 | 2 206 | 3 300 ms |

- Con pocos grupos alcanza un nivel: cada fila se escribe y se lee una vez, y
  en memoria hay a la vez unos 1/9 de los grupos.
- Con un grupo por fila, 40 KB de tabla no alcanzan: 10 000 grupos necesitan 2
  niveles y 100 000 necesitan 3. Las paginas leidas superan a las escritas por
  las particiones que se releen.
- El costo de la memoria acotada es el tiempo: `GROUP BY id` sobre 100 000
  filas crea 819 archivos y tarda 3,3 s. Con mas paginas de buffer bajaria el
  numero de niveles.

## 5. Joins

### Hash join

`hash_join` construye un `std::unordered_map<Key, vector<Right>>` con la
relacion derecha y sondea con la izquierda:

```text
Tiempo esperado: O(|left| + |right| + |resultado|)
Memoria: O(|right|)
```

Las listas por clave soportan duplicados.

### Index nested loop join

`index_nested_loop_join` recorre la relacion externa y llama una funcion de
lookup por clave:

```text
Costo: O(|outer| * costo_lookup + |resultado|)
```

Es apropiado cuando la relacion externa es pequena y existe un indice sobre la
relacion interna.

### Planner

`JoinPlanner::choose` selecciona index nested loop si hay indice y
`outer_rows <= inner_rows`; en otro caso selecciona hash join. Es una
heuristica inicial. Una version posterior debe considerar paginas, I/O,
selectividad y cardinalidades del catalogo.

### INNER JOIN en SQL

El ejecutor usa estas piezas para `SELECT ... FROM a x JOIN b y ON x.c = y.c`
(ver [parser_sql.md](parser_sql.md)). `planificar_join` en
`motor/consultas/ejecutor.cpp`, sin leer datos:

1. Valida el `ON`: una columna de cada tabla, del mismo tipo, sin `POINT` ni
   `VECTOR`.
2. Reparte el `WHERE`: cada condicion menciona una sola columna, asi que se
   aplica en su tabla antes del join y puede usar sus indices.
3. Una tabla es **indexable** si su columna del `ON` tiene un indice B+ o
   HASH, o si es su clave primaria en una tabla `SEQUENTIAL` o `BPLUS` (en un
   heap la clave no tiene indice). Si las dos lo son, la interna es la mas
   grande.
4. Con una tabla indexable, `JoinPlanner::choose(filas estimadas de la
   externa, registros de la interna, true)` decide entre index nested loop y
   hash join. Sin ninguna, hash join.

Ejecucion:

- **Index nested loop join:** lee la externa con su parte del `WHERE` y, por
  cada clave distinta, busca en la interna con `columna = clave` mas su parte
  del `WHERE`; el planificador de una tabla elige el indice. Las claves ya
  vistas no se vuelven a buscar. El nodo de la interna suma las busquedas, las
  filas y las paginas leidas.
- **Hash join:** lee las dos tablas con su parte del `WHERE` y llama a
  `hash_join` construyendo la tabla hash con la que trajo menos filas.

Las filas unidas (las columnas de `a` y luego las de `b`) pasan por las mismas
etapas que una tabla sola: `agrupar`, `ordenar`, `proyectar` y `limitar`
reciben un esquema con las columnas calificadas por tabla o alias, no una
`Tabla`. El `EXPLAIN` muestra el join como un arbol:

```text
Limit on compras JOIN compras  (rows=10) (actual rows=10)
  -> Sort on compras JOIN compras (actual time=0.944 rows=33)
     Sort Key: veces DESC
    -> HashAggregate on compras JOIN compras (actual time=1.872 rows=33)
       Group Key: c2.producto_id
      -> Index Nested Loop Join on compras JOIN compras (actual time=0.788 rows=33)
         Join Cond: c1.usuario_id = c2.usuario_id
        -> Index Scan using compras_producto on compras c1 (producto_id)  (cost=40.00 rows=37) (actual time=0.065 rows=2 pages=4)
           Index Cond: producto_id = 5
        -> Index Scan using compras_usuario on compras c2 (usuario_id) (actual time=0.788 rows=33 pages=38)
           Index Cond: usuario_id = c1.usuario_id
           Nota: una busqueda por cada clave distinta de c1
```

Las dos tablas se leen enteras en memoria (filtradas por su `WHERE`); el join
todavia no usa `ArchivoTemporal` para particionar en disco.

## 6. Trazas y logs

`PlanTrace` conserva eventos como `PlanEvent` y los agrega a:

```text
logs/external_algorithms_cpp.log
```

Ejemplo:

```text
INFO Plan externo: external_merge_sort buffer_pages=10 records_per_page=1000 k=9 initial_runs=10 merge_passes=1 records=100000
```

Tambien se registran `external_hash_aggregate` (con `buffer_pages`,
`partitions`, `levels`, `repartitions`, `peak_groups_in_memory`,
`pages_written`, `pages_read`, `records`, `groups` y `aggregates`),
`hash_join` e `index_nested_loop_join`.

## 7. Pruebas

`external_algorithms_test.cpp` verifica:

1. Ordenamiento de 100 000 registros.
2. 10 runs con 10 paginas y 1 000 registros por pagina.
3. Merge k-way con `k=9`.
4. GROUP BY en disco con 1, 100 y 50 000 grupos: `COUNT`, `SUM`, `AVG`,
   `MIN` y `MAX` iguales a agrupar en memoria, y cada grupo emitido una vez.
5. Con B = 3 y 200 grupos, la particion no cabe y se vuelve a particionar
   (2 niveles); con 60 000 grupos ni 3 niveles alcanzan y el error es claro.
   Los temporales se borran tambien cuando hay error.
6. Hash join con claves coincidentes.
7. Index nested loop join.
8. Eleccion del planner.
9. Persistencia de la traza.

`sql_test.cpp` (`prueba_group_by_externo`) lo prueba por SQL: 20 000 filas con
137 tipos y los cinco agregados en una consulta, `GROUP BY id` con 20 000
grupos (2 niveles), agregados sin `GROUP BY` y las medidas del `EXPLAIN
ANALYZE`.

Compilacion:

```powershell
New-Item -ItemType Directory -Force .build | Out-Null
g++ -std=c++17 -Wall -Wextra -pedantic motor/consultas/external_algorithms.cpp motor/pruebas/external_algorithms_test.cpp -o .build/external_algorithms_test.exe
```

Ejecucion:

```powershell
.\.build\external_algorithms_test.exe
```

## 8. Limitaciones

El GROUP BY ya particiona en disco. El sort y el join todavia trabajan con
vectores en memoria; para llevarlos al modelo externo completo se necesita:

- formato de pagina de 4 KiB u 8 KiB;
- administrador de paginas temporales;
- serializacion de registros variables;
- lectura por streams en vez de vectores completos;
- conteo de lecturas y escrituras de pagina;
- planner basado en costo de I/O.
