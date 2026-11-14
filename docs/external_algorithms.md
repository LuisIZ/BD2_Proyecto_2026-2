# Algoritmos externos de consultas en C++17

## 1. Implementacion

Los algoritmos C++ estan en:

- `motor/consultas/external_algorithms.h`: templates de ordenamiento,
  agregacion, joins y `PlanTrace`.
- `motor/consultas/external_algorithms.cpp`: unidad de compilacion.
- `motor/pruebas/external_algorithms_test.cpp`: pruebas principales.

Los templates reciben tipos concretos de registro y clave:

```cpp
motor::PlanTrace traza;
motor::ExternalMergeSort<int, int> sort(10, 1000, 9, &traza);
```

## 2. Paginas y presupuesto

El C++ modela el presupuesto mediante `buffer_pages`, `records_per_page` y
`k`. No se crean paginas fisicas ni archivos temporales en esta version: runs
y particiones viven en `std::vector`. Por tanto, `records_per_page` es un
limite de memoria reproducible, no una serializacion real.

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

## 4. External Hash Aggregation

`ExternalHashAggregate<Registro, Grupo>` particiona por:

```text
hash(grupo) % partitions
```

El limite logico de grupos simultaneos es:

```text
max_groups_in_memory = buffer_pages * records_per_page
```

La cantidad de particiones es al menos `buffer_pages - 1` y aumenta cuando el
volumen supera el limite. Cada grupo acumula `COUNT`, `SUM`, `AVG`, `MIN` y
`MAX` en `AggregateResult`.

La prueba usa 100 grupos, 4 paginas y 2 registros por pagina:

```text
max_groups_in_memory = 4 * 2 = 8
```

Se prueban 100 grupos, 12.5 veces el limite simultaneo.

Las particiones se agregan una por vez: cada una usa su propia tabla de hash,
se vuelca al resultado y se libera antes de pasar a la siguiente. Como el hash
manda todas las filas de un grupo a la misma particion, el resultado es el
mismo que agregando todo junto, pero en memoria solo viven los grupos de la
particion en curso. La traza registra ese maximo como `peak_groups_in_memory`
y el plan del motor SQL lo muestra como `grupos_en_memoria`:

```text
SELECT Index, COUNT(*) FROM t GROUP BY Index    (300 claves distintas)
grupos=300 particiones=9 grupos_en_memoria=42
```

Importante: las particiones todavia viven en `std::vector`. Para external
hashing persistente se deben escribir en archivos temporales o paginas.

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

Tambien se registran `external_hash_aggregate` (con `partitions`,
`max_groups_in_memory` y `peak_groups_in_memory`), `hash_join` e
`index_nested_loop_join`.

## 7. Pruebas

`external_algorithms_test.cpp` verifica:

1. Ordenamiento de 100 000 registros.
2. 10 runs con 10 paginas y 1 000 registros por pagina.
3. Merge k-way con `k=9`.
4. 100 grupos con capacidad logica de 8 grupos.
5. `COUNT`, `SUM`, `AVG`, `MIN` y `MAX`.
6. Hash join con claves coincidentes.
7. Index nested loop join.
8. Eleccion del planner.
9. Persistencia de la traza.

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

La implementacion C++ preserva las APIs principales de Python, pero aun no
persiste runs ni particiones. Para cumplir completamente el modelo externo
sobre disco se necesita:

- formato de pagina de 4 KiB u 8 KiB;
- administrador de paginas temporales;
- serializacion de registros variables;
- lectura por streams en vez de vectores completos;
- conteo de lecturas y escrituras de pagina;
- planner basado en costo de I/O.
