# Algoritmos externos de consultas

## 1. Objetivo

Se implementaron algoritmos que permiten procesar relaciones mayores que la
memoria disponible:

- `ExternalMergeSort` para `ORDER BY`.
- `ExternalHashAggregate` para `GROUP BY` y agregados.
- `hash_join` e `index_nested_loop_join` para `JOIN`.
- `JoinPlanner` para seleccionar y registrar el algoritmo de join.

La implementación utiliza archivos temporales como representación de runs y
particiones. El presupuesto de memoria se expresa en páginas de buffer.

Los archivos principales son:

- `motor/consultas/external_algorithms.py`.
- `motor/consultas/__init__.py`.
- `motor/pruebas/test_external_algorithms.py`.

Los eventos de planificación se guardan en un historial separado:

```text
logs/external_algorithms.log
```

## 2. Presupuesto de buffers

Los constructores reciben:

```python
buffer_pages=10
records_per_page=100
```

`records_per_page` traduce páginas a una cantidad de registros para hacer
reproducible la prueba en memoria. El algoritmo no mantiene toda la relación
en memoria: escribe runs o particiones en archivos temporales.

En el sort, `k` representa la cantidad máxima de runs que se fusionan en una
pasada. Por defecto es `buffer_pages - 1`, reservando una página para la
salida, pero puede configurarse explícitamente:

```python
sorter = ExternalMergeSort(buffer_pages=10, records_per_page=100, k=5)
```

El valor válido de `k` está entre 2 y `buffer_pages - 1`.

## 3. External Merge Sort para ORDER BY

### Generación de runs

La relación se lee en grupos de:

```text
buffer_pages * records_per_page
```

Cada grupo se ordena en memoria y se almacena en un archivo temporal llamado
run. Si hay 100 000 registros, 10 páginas y 1 000 registros por página, se
generan 10 runs de 10 000 registros cada uno.

### Merge k-way

Los runs se fusionan usando un heap de mínimos:

1. Se lee el primer registro de cada run.
2. Se inserta cada registro en el heap junto con su clave y origen.
3. Se extrae el menor registro.
4. Se escribe en la salida y se lee el siguiente registro del mismo run.
5. Se repite hasta vaciar todos los runs.

Si hay más de `k` runs, se realizan pasadas intermedias hasta reducirlos a
`k` o menos. El resultado se produce ordenado sin cargar la relación completa
en memoria.

Uso:

```python
sorter = ExternalMergeSort(buffer_pages=10, records_per_page=1000)
ordenados = sorter.sort(registros, key=lambda registro: registro["id"])
```

La traza registra:

- `buffer_pages`.
- `records_per_page`.
- `k`.
- cantidad inicial de runs.
- cantidad de pasadas de merge.
- cantidad final de registros.

## 4. External Hashing para GROUP BY

`ExternalHashAggregate` particiona los registros por el hash de su clave de
grupo. Cada partición se guarda en un archivo temporal y luego se procesa por
separado, de manera que los grupos de una partición puedan agregarse sin
mantener todos los grupos globales en memoria.

El límite estimado de grupos simultáneos es:

```text
max_groups_in_memory = buffer_pages * records_per_page
```

El número de particiones aumenta cuando la cantidad de registros supera ese
límite. Así, el algoritmo puede manejar más grupos que los que caben en
memoria, como demuestra el test con 100 grupos y capacidad de 8 grupos.

Uso:

```python
aggregate = ExternalHashAggregate(buffer_pages=4, records_per_page=2)
resultado = aggregate.aggregate(
    registros,
    group_key=lambda registro: registro["country"],
    value=lambda registro: registro["employees"],
    aggregates={
        "count": "COUNT",
        "total": "SUM",
        "average": "AVG",
        "minimum": "MIN",
        "maximum": "MAX",
    },
)
```

El resultado tiene una entrada por grupo:

```python
{
    "Peru": {
        "count": 2,
        "total": 120,
        "average": 60.0,
        "minimum": 40,
        "maximum": 80,
    }
}
```

Soporta `COUNT`, `SUM`, `AVG`, `MIN` y `MAX`. La traza informa el presupuesto
de buffers, las particiones utilizadas, el límite de grupos, registros y
grupos resultantes.

## 5. Joins

### Hash join

`hash_join` construye una tabla hash con la relación interna y luego consulta
la tabla con cada registro de la relación externa. Es apropiado para
equi-joins cuando no existe un índice útil.

```python
resultado = hash_join(
    clientes,
    pedidos,
    left_key=lambda cliente: cliente["id"],
    right_key=lambda pedido: pedido["cliente_id"],
)
```

### Index nested loop join

`index_nested_loop_join` recorre la relación externa y consulta un índice para
cada clave. El índice puede ser una función callable o un objeto con método
`buscar`.

```python
resultado = index_nested_loop_join(
    clientes,
    indice_pedidos,
    outer_key=lambda cliente: cliente["id"],
)
```

### Planner

`JoinPlanner` elige index nested loop cuando existe índice y la relación
externa no es mayor que la interna. En los demás casos selecciona hash join.
La decisión y la ejecución quedan registradas en `PlanTrace`:

```python
trace = PlanTrace()
planner = JoinPlanner(trace)
resultado = planner.execute(clientes, pedidos, clave_cliente, clave_pedido)
```

Cada evento de la traza contiene `algorithm` y los datos de la decisión,
como cardinalidades, disponibilidad del índice y cantidad de filas
producidas.

`PlanTrace` escribe esos mismos eventos en el archivo
`logs/external_algorithms.log`. La ruta puede personalizarse:

```python
trace = PlanTrace(log_path="tmp/mi-plan.log")
```

Los operadores `ExternalMergeSort`, `ExternalHashAggregate` y `JoinPlanner`
tambien aceptan `log_path` cuando crean su propia traza. Para cerrar el
archivo explicitamente se puede llamar `cerrar_log(ruta)`, una practica util
en tests y en Windows.

## 6. Pruebas

La suite se encuentra en `motor/pruebas/test_external_algorithms.py` y cubre:

- generación de runs y merge k-way;
- configuración y reporte de buffers y `k`;
- ordenamiento de 100 000 registros con 10 páginas de buffer;
- agregados `COUNT`, `SUM`, `AVG`, `MIN` y `MAX`;
- GROUP BY con más grupos que el límite de memoria;
- hash join;
- index nested loop join;
- elección y registro del plan de joins.
- persistencia de la traza en un archivo `.log` separado.

Desde la raíz del proyecto:

```powershell
python -m unittest discover -s motor/pruebas -p "test_*.py" -v
```

También se puede comprobar la sintaxis con:

```powershell
python -m compileall -q motor/consultas motor/pruebas
```