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

Esta implementación de `distancia_m` vive en `motor/consultas/ejecutor.cpp` y se
reemplaza por la de la tarjeta de métricas (#26) cuando esté lista, sin cambiar la
sintaxis.

## 4. Plan de ejecución

Mientras no exista el R-Tree, toda consulta espacial recorre la tabla:

```
EXPLAIN SELECT nombre FROM tiendas WHERE distancia(ubicacion, POINT(-12.0464, -77.0428)) < 6000;

Filter on tiendas  (cost=1.00 rows=1)
Filter: distancia(ubicacion, POINT(-12.046400, -77.042800)) [haversine] < 6000
  -> Seq Scan on tiendas  (cost=1.00 rows=5)
```

El k-NN ordena con el mismo `ExternalMergeSort` de `ORDER BY`, usando la distancia en
milímetros como clave, y `LIMIT` corta:

```
Limit on tiendas  (rows=3) (actual rows=3)
  -> Projection on tiendas (actual rows=5)
    -> Sort on tiendas (actual time=0.506 rows=5)
       Sort Key: distancia(ubicacion, POINT(-12.046400, -77.042800)) [haversine] ASC
      -> Seq Scan on tiendas  (cost=1.00 rows=5) (actual time=0.013 rows=5 pages=1)
```

## 5. Pendiente

- `CREATE INDEX ... USING RTREE` ya se reconoce, pero responde que el índice aún no está
  disponible. Cuando el R-Tree (#24) esté en `main`, el planificador lo usará para el
  radio y el k-NN en lugar del recorrido.
- Intersección con polígono: la sintaxis propuesta es
  `dentro(col, POLYGON((lat lon, lat lon, ...)))`; todavía no está implementada.
- Para comparar con PostGIS (#29) hay que invertir el orden: PostGIS usa
  `POINT(lon lat)`.
