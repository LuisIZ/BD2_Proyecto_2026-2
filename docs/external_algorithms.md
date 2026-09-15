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

Importante: esta version implementa particionamiento externo a nivel logico,
pero las particiones siguen en memoria. Para external hashing persistente se
deben usar archivos temporales o paginas y procesar una particion por vez.

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

## 6. Trazas y logs

`PlanTrace` conserva eventos como `PlanEvent` y los agrega a:

```text
logs/external_algorithms_cpp.log
```

Ejemplo:

```text
INFO Plan externo: external_merge_sort buffer_pages=10 records_per_page=1000 k=9 initial_runs=10 merge_passes=1 records=100000
```

Tambien se registran `external_hash_aggregate`, `hash_join` e
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
