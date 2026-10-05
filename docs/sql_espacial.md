# SQL espacial: POINT y distancia

Extensión del parser y del ejecutor para la Parte 2 (sección 2.2.3). Permite guardar
puntos geográficos y consultarlos por radio o por cercanía, en las tres organizaciones
(`HEAP`, `SEQUENTIAL`, `BPLUS`).

## 1. Tipo POINT

| Aspecto | Decisión |
|---|---|
| Orden | `(lat, lon)`, igual que el enunciado |
| Representación | dos enteros en **microgrados** (grados × 1 000 000) |
| Tamaño en disco | 8 bytes (`int32 lat`, `int32 lon`) en el formato variable y en el fijo |
| Precisión | hasta 6 decimales, unos 11 cm en el ecuador |
| Rango | lat entre -90 y 90, lon entre -180 y 180 |

Se guarda en enteros y no en `double` porque el motor solo maneja `INT` y `VARCHAR`, y
así el literal `POINT(-12.0464, -77.0428)` se convierte sin redondeos: el parser lee las
cifras como texto y arma `-12046400` y `-77042800`.

Una columna `POINT` no puede ser clave primaria ni llevar índice B+ o hash, y no se
compara con `=` o `<`: solo con `distancia(...)`.

## 2. Sintaxis

```sql
CREATE TABLE tiendas (id INT PRIMARY KEY, nombre VARCHAR(20), ubicacion POINT);
INSERT INTO tiendas VALUES (1, 'Centro', POINT(-12.0464, -77.0428));
COPY tiendas FROM FILE 'tiendas.csv';

-- radio: todas las tiendas a menos de 5 km
SELECT * FROM tiendas WHERE distancia(ubicacion, POINT(-12.0464, -77.0428)) < 5000;

-- k vecinos: las 10 más cercanas
SELECT * FROM tiendas ORDER BY distancia(ubicacion, POINT(-12.0464, -77.0428)) LIMIT 10;

-- métrica explícita
SELECT * FROM tiendas WHERE distancia(ubicacion, POINT(-12.0464, -77.0428), 'euclidiana') <= 3000;
```

- `distancia(col, POINT(lat, lon) [, 'haversine' | 'euclidiana'])` devuelve **metros**.
  Sin tercer argumento usa Haversine.
- En el `WHERE` se compara con `<`, `<=`, `>` o `>=` contra un entero en metros, y se
  puede unir con otras condiciones con `AND`.
- En el `ORDER BY` admite `ASC` y `DESC`; con `LIMIT k` da los k vecinos más cercanos.
  No se combina con `GROUP BY` ni con agregados.
- En un CSV para `COPY`, la columna `POINT` se escribe `lat lon`, `lat, lon` (entre
  comillas) o `POINT(lat lon)`.
- Los decimales solo se aceptan dentro de `POINT(...)`; en el resto del SQL siguen
  siendo un error.

## 3. Métricas

| Métrica | Fórmula | Uso |
|---|---|---|
| `haversine` (por defecto) | distancia sobre la esfera, radio 6 371 km | distancias reales entre puntos de la Tierra |
| `euclidiana` | distancia en el plano de grados, convertida a metros con el mismo radio | comparar con la fórmula simple; se aleja de la real lejos del ecuador |

Con los puntos de la prueba, desde el centro de Lima: San Isidro queda a unos 5.7 km,
Callao a 8.3 km, Miraflores a 8.4 km y Barranco a 11.6 km.

Las dos métricas viven en `motor/espacial/distancia.h`, que es el módulo compartido por
el ejecutor y por el R-Tree:

```cpp
struct Punto { int lat_e6, lon_e6; };              // microgrados, orden (lat, lon)
enum class Metrica { EUCLIDIANA, HAVERSINE };
double distancia_m(Punto a, Punto b, Metrica m);   // metros
```

`motor/pruebas/distancia_test.cpp` las verifica con distancias conocidas:

| Caso | Haversine | Euclidiana |
|---|---:|---:|
| 1° de latitud, o 1° de longitud sobre el ecuador | 111 194.9 m | 111 194.9 m |
| Lima centro a San Isidro | 5 745.3 m | 5 747.2 m |
| Lima a Cusco | 574.6 km | 588.0 km |
| 1° de longitud a latitud 60° | 55 596.9 m | 111 194.9 m |

La última fila muestra el límite de la euclidiana: trata un grado de longitud como si
midiera lo mismo en cualquier latitud, cuando a 60° mide la mitad. Cerca del ecuador, como
en Lima, las dos casi coinciden.

## 4. Plan de ejecución

### Sin índice

Toda consulta espacial recorre la tabla. El radio es un filtro sobre la distancia:

```
EXPLAIN SELECT nombre FROM tiendas WHERE distancia(ubicacion, POINT(-12.0464, -77.0428)) < 6000;

Filter on tiendas  (cost=1.00 rows=1)
Filter: distancia(ubicacion, POINT(-12.046400, -77.042800)) [haversine] < 6000
  -> Seq Scan on tiendas  (cost=1.00 rows=5)
```

y el k-NN ordena toda la tabla con el `ExternalMergeSort` de `ORDER BY`, usando la
distancia en milímetros como clave, antes de que `LIMIT` corte.

### Con R-Tree

```sql
CREATE INDEX idx_geo ON tiendas (ubicacion) USING RTREE;
```

crea un R-Tree paginado ([rtree.md](rtree.md)) sobre una columna `POINT` de una tabla
`HEAP`, y se mantiene con cada `INSERT` y `DELETE`. El planificador lo usa en dos casos:

| Consulta | Acceso |
|---|---|
| `WHERE distancia(col, POINT(...)) < r` (o `<=`) | `rango_espacial`: el R-Tree devuelve los puntos a `r` metros o menos y un filtro conserva el operador exacto |
| `ORDER BY distancia(col, POINT(...)) LIMIT k`, sin otras condiciones y en orden ascendente | `knn_espacial`: el R-Tree devuelve solo los k más cercanos y el `Sort` ordena esas k filas |

```
EXPLAIN ANALYZE SELECT nombre FROM tiendas WHERE distancia(ubicacion, POINT(-12.0464, -77.0428)) < 6000 ORDER BY nombre;

Projection on tiendas (actual rows=2)
  -> Sort on tiendas (actual time=0.371 rows=2)
     Sort Key: nombre ASC
    -> Filter on tiendas (actual time=0.003 rows=2)
       Filter: distancia(ubicacion, POINT(-12.046400, -77.042800)) [haversine] < 6000
      -> Index Scan using idx_geo on tiendas (ubicacion)  (cost=4.00 rows=1) (actual time=0.041 rows=2 pages=3)
         Index Cond: distancia(ubicacion, POINT(-12.046400, -77.042800)) [haversine] < 6000
```

El detalle `nodos_visitados` del paso de acceso dice cuántos nodos del R-Tree se leyeron.
Si el `ORDER BY distancia` va junto con otras condiciones en el `WHERE`, no se usa el k-NN
del índice, porque los k más cercanos podrían no cumplirlas; se recorre y se ordena como
sin índice. Las pruebas comparan las dos formas y dan el mismo resultado.

## 5. Intersección con polígono

```sql
SELECT nombre FROM tiendas
WHERE dentro(ubicacion, POLYGON((-12.13 -77.05, -12.13 -77.02, -12.09 -77.02, -12.09 -77.05)));
```

- Los vértices van como `lat lon` separados por comas, en orden (horario o antihorario);
  hacen falta al menos 3 y el polígono se cierra solo.
- `dentro` no lleva operador de comparación y se puede unir con otras condiciones con `AND`.
- La pertenencia se decide con *ray casting*: desde el punto se traza un rayo y se cuentan
  los lados que cruza; si son impares, el punto está dentro. Las cuentas se hacen en
  microgrados con enteros de 64 bits, así que un punto sobre un borde se detecta de forma
  exacta y cuenta como dentro.
- Sin índice es un filtro sobre el recorrido. Con R-Tree, el planificador usa la ruta
  `poligono_espacial`: pide al índice los puntos de la caja que envuelve al polígono
  (`en_caja`) y el filtro hace la prueba exacta, igual que con el radio:

```
-> Filter on tiendas (actual time=0.002 rows=1)
   Filter: dentro(ubicacion, POLYGON(3 vertices))
  -> Index Scan using idx_geo on tiendas (ubicacion)  (cost=4.00 rows=1) (actual time=0.037 rows=2 pages=3)
     Index Cond: dentro(ubicacion, POLYGON(3 vertices))
```

Con los puntos de la prueba, el rectángulo del ejemplo devuelve Miraflores y San Isidro, y
el triángulo que usa la misma base pero corta en diagonal devuelve solo Miraflores.

## 6. Benchmark PostGIS

La comparación reproducible con PostGIS `geography(Point,4326)` y GiST está en
[`datos/resultados/postgis_bench.sql`](../datos/resultados/postgis_bench.sql), y su
metodología y comandos están en
[`docs/comparacion_espacial/README.md`](comparacion_espacial/README.md). PostGIS recibe
coordenadas como `POINT(lon lat)`, por lo que el benchmark construye la geometría con
longitud primero, aunque el motor recibe `POINT(lat, lon)`.

El parser valida que la latitud esté entre -90 y 90 y la longitud entre -180 y 180, tanto
en literales `POINT` como en los vértices de `POLYGON`.
