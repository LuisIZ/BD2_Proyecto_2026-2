# R-Tree paginado en disco

Índice espacial de la Parte 2 (sección 2.2.1) para puntos 2D (latitud, longitud).
Responde consultas por radio y de k vecinos más cercanos sin recorrer toda la tabla.

| Archivo | Qué es |
|---|---|
| `motor/indices/rtree.h` | `RTree`: nodos de 4 KB, inserción, split cuadrático, rango, k-NN y eliminación. |
| `motor/espacial/distancia.h` | Métricas haversine y euclidiana que usan las consultas. |
| `motor/pruebas/rtree_test.cpp` | 100 000 puntos comparados contra búsqueda secuencial. |

## 1. Formato en disco

Usa el mismo `GestorPaginas` y `BufferPool` (64 marcos) que el B+ no agrupado y el hash.

```
página 0    cabecera: mágico "RTRE", versión, altura, raíz, número de nodos y de entradas
página p    nodo: [es_hoja u16][num u16][reservado u32] + hasta 170 entradas
entrada     [min_lat][min_lon][max_lat][max_lon] (int32, microgrados) + ref (int64) = 24 B
```

En una hoja, cada entrada es un punto (su MBR tiene área cero) y `ref` es la posición del
registro en la tabla. En un nodo interno, el MBR cubre todo el subárbol y `ref` es la página
del hijo. Con 4 096 bytes caben (4 096 − 8) / 24 = 170 entradas por nodo, y el mínimo
tras un split es el 40 % (68).

## 2. Inserción

1. Desde la raíz se baja por la entrada cuyo MBR crece menos al agregar el punto; si dos
   empatan, la de menor área (*ChooseLeaf* de Guttman).
2. El punto se agrega a la hoja. Al volver, cada nodo actualiza el MBR de la entrada por la
   que bajó.
3. Si un nodo pasa de 170 entradas, se divide con el **split cuadrático**: se eligen como
   semillas las dos entradas que más área desperdiciarían juntas y luego se reparte el resto,
   cada vez la entrada con mayor preferencia por un grupo, al grupo que menos crece. Si un
   grupo necesita todas las que quedan para llegar al mínimo, se las lleva.
4. El nodo nuevo sube como entrada al padre; si la raíz se divide, se crea una raíz nueva y
   el árbol gana un nivel.

## 3. Consultas

**Rango por radio.** Se calcula la caja de grados que contiene al círculo (con un margen del
5 % y corrigiendo la longitud por la latitud) y se baja solo por los nodos cuyo MBR la corta.
En las hojas se filtra con la distancia exacta de la métrica pedida.

**k vecinos más cercanos.** Búsqueda *best-first* con una cola de prioridad ordenada por la
distancia mínima del punto a cada MBR (la distancia al punto más cercano del rectángulo).
Esa distancia nunca es mayor que la de cualquier punto dentro del rectángulo, así que el
primer punto que sale de la cola es el más cercano, el segundo el siguiente, y así hasta k.
La salida ya viene ordenada.

Las dos consultas guardan `nodos_visitados()` para el plan de ejecución.

## 4. Eliminación

Se busca la hoja que contiene el par exacto (punto, posición) bajando solo por los MBR que
contienen al punto, y se quita la entrada. Los MBR de los ancestros no se encogen: siguen
siendo correctos porque contienen a lo que queda, aunque puedan quedar más grandes de lo
necesario. Es una simplificación frente al *CondenseTree* de Guttman.

## 5. Uso desde SQL

`CREATE INDEX idx_geo ON tiendas (ubicacion) USING RTREE` construye el índice recorriendo el
heap y lo guarda en `<tabla>__<indice>.rtree`. El ejecutor lo mantiene en cada `INSERT` y
`DELETE` y lo usa para `distancia(...) < r` y `ORDER BY distancia(...) LIMIT k`; el detalle
se explica en [sql_espacial.md](sql_espacial.md).

## 6. Resultados con 100 000 puntos

Puntos al azar (semilla 42) en una caja alrededor de Lima:

| Medida | Valor |
|---|---:|
| Altura | 3 |
| Nodos | 872 |
| Tamaño en disco | 3 492 KB |
| Nodos visitados, radio de 1 km | 14 de 872 |
| Nodos visitados, k = 10 | 8 |

La prueba compara 60 consultas de rango (radios de 1, 5 y 10 km con las dos métricas) y
30 de k-NN (k = 10, 50 y 100) contra una búsqueda secuencial sobre los 100 000 puntos, y
todas coinciden. También verifica el invariante (el MBR de cada entrada interna contiene a
todo su hijo), la eliminación de 1 000 puntos y que el árbol responda igual al reabrirlo.

```bash
make test                     # incluye rtree_test
.build/rtree_test             # solo el R-Tree
```
