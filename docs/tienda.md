# Tienda (Parte 5): datos, API y recomendador

La tienda (opción B del Anexo A) trabaja sobre los 10 000 productos de `productos_10k.csv`, que ya
tienen texto, foto y vector (ver [dataset.md](dataset.md)). ABO no trae precios, tiendas, stock ni
compras, así que esos datos son **sintéticos y reproducibles**: la misma semilla da siempre los
mismos archivos.

## Archivos

Los 6 CSV ya están en el repositorio, en `datos/tienda/` (unos 21 MB):
- `productos.csv`, `sedes.csv`, `inventario.csv`, `usuarios.csv` y `compras.csv`;
- `catalogo.csv`, con el vector de cada producto (14 MB).

Se cargan con [tienda.sql](tienda.sql), entero desde la vista **Consultas** de la web.

## Cómo regenerarlos

Solo hace falta si se cambia el generador o la semilla:

```
python tools/tienda/generar_datos.py --n 10000 --semilla 7
python tools/dataset/preparar_catalogo.py --n 10000
copy datos\productos\catalogo_10000.csv datos\tienda\catalogo.csv
```

- El primero reescribe los 5 CSV de `datos/tienda/` en un segundo. Con la misma semilla salen
  idénticos.
- El segundo escribe `datos/productos/catalogo_10000.csv` y necesita
  `datos/productos/vectores_100000.npy`, que no está en el repo por su tamaño (ver
  [comparacion_vectorial/README.md](comparacion_vectorial/README.md)). Por eso `catalogo.csv` se
  sube ya armado.
Para probarlo sobre una base temporal:

```
python tools/probar_sql.py docs/tienda.sql
python -m unittest tools.tienda.test_generar
```

Si las tablas ya existen, primero hay que borrarlas con los `DROP TABLE` comentados al final del
script, porque el motor no tiene `DROP TABLE IF EXISTS`.

## Tablas

Con `--n 10000 --semilla 7`:

| Tabla | Filas | Columnas | Organización | Índices |
|---|---|---|---|---|
| `productos` | 10 000 | las de `productos_10k.csv` + `precio FLOAT` | BPLUS | SPIMI en `descripcion` |
| `catalogo` | 10 000 | `id, nombre, v VECTOR(256)` | BPLUS | HNSW en `v` |
| `sedes` | 25 | `id, nombre, distrito, ubicacion POINT` | HEAP | RTREE en `ubicacion` |
| `inventario` | 29 915 | `id, sede_id, producto_id, stock` | HEAP | BPLUS en `producto_id` |
| `usuarios` | 500 | `id, nombre` | HEAP | — |
| `compras` | 7 458 | `id, usuario_id, producto_id` | HEAP | BPLUS en `producto_id`, HASH en `usuario_id` |

Los `id` de `productos` y de `catalogo` son los mismos (los de `productos_10k.csv`). Así, un vecino
que devuelve el HNSW se cruza con su precio y su stock sin traducir ids. Las dos tablas son
`BPLUS` porque la ficha y los vecinos de FAISS se leen por `id`. El registro más largo
(`productos`) ocupa unos 1 400 bytes, así que entra en una página de 4 KB.

## Cómo se generó cada una

**`productos.precio`.** Cada `tipo` frecuente tiene un rango en soles en `PRECIOS`
([generar_datos.py](../tools/tienda/generar_datos.py)). Por ejemplo:

| Tipo | Rango (S/) |
|---|---|
| funda de celular (`CELLULAR_PHONE_CASE`) | 15 a 80 |
| zapatos (`SHOES`) | 90 a 450 |
| sofá (`SOFA`) | 900 a 6 000 |

Los tipos que no están en la lista usan de 20 a 500. El precio sale al azar dentro del rango y
termina en ,90.

**`sedes`.** Hay una tienda por distrito de Lima Metropolitana (Miraflores, Barranco, San Isidro,
Surco, La Molina, Los Olivos, Comas…), en las coordenadas del centro del distrito. `ubicacion` va
en el CSV como `lat lon`, que `COPY` lee como `POINT`.

**`inventario`.**
- Cada producto está en 1 a 5 sedes al azar (3 en promedio), y por eso hay unas 30 000 filas.
- El 15 % de las filas tiene stock 0: el producto figura en esa sede, pero está agotado. El resto
  tiene de 1 a 50 unidades.
- La primera sede de cada producto siempre tiene stock.

Así, "las sedes cercanas con stock" no son simplemente las 3 más cercanas.

**`usuarios`.** 500 nombres armados con nombres y apellidos comunes en el Perú.

**`compras`.** Para que "quienes compraron X también compraron Y" tenga sentido, las compras no
son al azar:

1. Cada usuario elige 2 o 3 **tipos preferidos**, entre los que tienen al menos 10 productos.
2. Hace de 10 a 20 compras, sin repetir producto.
3. El 80 % de esas compras sale de sus tipos preferidos y el 20 % de cualquier producto.

Dos usuarios que compraron la misma silla suelen preferir sillas, y lo demás que compraron
también tiende a ser muebles.

## Pruebas

[test_generar.py](../tools/tienda/test_generar.py) comprueba:
- que la misma semilla da los mismos archivos y otra semilla da otros;
- los conteos y que los `id` no se repiten;
- que cada precio está en el rango de su tipo y cada sede está en Lima;
- que cada `sede_id`, `usuario_id` y `producto_id` existe;
- que hay una fila de inventario por sede y producto, que todo producto tiene stock en alguna sede
  y que hay productos agotados en alguna sede;
- que los 3 tipos que más compra cada usuario cubren el 80 % de sus compras;
- que cada registro cabe en 4 KB.

Además, los bloques `[10]` a `[13]` de [tienda.sql](tienda.sql) prueban las consultas que usa el
recomendador: texto con el precio, el stock de un producto, las 3 sedes más cercanas a UTEC
(Barranco, Miraflores y Surquillo) y las compras de un usuario.

## API de la tienda

[api/tienda.py](../api/tienda.py) es un router de FastAPI incluido en `web_api.py`. Sus
respuestas siguen el contrato de la interfaz, `web/src/tienda/types.ts`. Cada endpoint devuelve
también las consultas SQL que corrió, para mostrarlas en la web.

| Endpoint | Qué hace |
|---|---|
| `GET /api/tienda/productos?q=&tipo=&pagina=` | busca con `MATCH(descripcion, q) USING BM25` (con `AND tipo = ...` si hay tipo), o lista por tipo; 12 por página |
| `GET /api/tienda/productos/{id}` | la ficha: nombre, descripción, categoría, precio y foto |
| `GET /api/tienda/productos/{id}/recomendaciones?k=10` | el top k del recomendador, con la fuente y el puesto de cada producto |
| `GET /api/tienda/productos/{id}/sedes?lat=&lon=&k=3` | las k sedes más cercanas que tienen stock |

El motor no tiene `OR` ni `OFFSET`, así que la API cruza los resultados en Python. Tampoco usa
el `JOIN` del motor, que llegó después; [pruebas_entrega_final.sql](pruebas_entrega_final.sql)
comprueba que el `JOIN` da lo mismo que estas consultas cruzadas en Python:
- **Páginas:** pide todas las coincidencias y corta la página; sin filtros pagina con
  `id BETWEEN a AND b` (los `id` van de 1 a 10 000). Los tipos del filtro salen de un
  `GROUP BY tipo` que se calcula una vez por base.
- **Sedes:** `inventario WHERE producto_id = X AND stock > 0` (B+) y
  `sedes ORDER BY distancia(ubicacion, POINT(lat, lon))` (R-Tree). Se recorren las sedes de la
  más cercana a la más lejana y se quedan las k primeras con stock. La distancia en metros es la
  de Haversine, la misma del motor.

## Recomendador

Para un producto X se arman 4 listas de hasta 20 candidatos, cada una con su consulta:

| Fuente | Consulta | Orden de la lista |
|---|---|---|
| texto | `SELECT id, SCORE() AS relevancia FROM productos WHERE MATCH(descripcion, '<nombre de X>') USING BM25 ORDER BY relevancia DESC LIMIT 21` | score BM25 |
| imagen | `SELECT id, SIMILARITY_SCORE() FROM catalogo WHERE SIMILAR_TO(VECTOR(<vector de X>), k = 21) USING HNSW` | similitud coseno (FAISS) |
| metadata | `SELECT id, marca, color FROM productos WHERE tipo = '<tipo de X>'`, y en Python misma marca o mismo color | primero marca y color, luego marca, luego color |
| compras | `SELECT usuario_id FROM compras WHERE producto_id = X` (B+) y, por cada usuario u, `SELECT producto_id FROM compras WHERE usuario_id = u` (HASH) | en cuántos compradores de X aparece |

En cada lista se quita X y los repetidos. Las listas se juntan con Reciprocal Rank Fusion
(Cormack, Clarke y Büttcher, 2009):

$$\text{score}(d) = \sum_{\text{listas } L \ni d} \frac{1}{60 + \text{puesto}_L(d)}$$

con el puesto desde 1. RRF solo usa los puestos, así que no hay que calibrar entre sí los scores
de BM25, del coseno y de los conteos. Los empates se rompen por el mejor puesto y luego por `id`.
Si una fuente falla o viene vacía (por ejemplo, un producto que nadie compró), se fusionan las
demás y la respuesta lo dice en `avisos`.

Las consultas se mandan al motor en 3 lotes (el producto y su vector, las 4 fuentes y las tarjetas
del top k), porque cada llamada al binario abre y cierra los archivos. Con los 10 000 productos,
las recomendaciones tardan entre 0,5 y 0,6 s (0,7 s la primera vez, cuando se carga el HNSW).

Ejemplo real, el top 10 del producto 5 (taburete Rivet Malida, tipo `STOOL_SEATING`); las
columnas de las fuentes dan el puesto en cada lista:

| # | id | Producto | texto | imagen | metadata | compras | score |
|---:|---:|---|---:|---:|---:|---:|---:|
| 1 | 2142 | Rivet Malida Faux Leather Bar Stool, Grey | 1 | 3 | 2 | | 0,0484 |
| 2 | 3232 | Ravenna Home Scott Tufted Back Bar Stool | 3 | 5 | | | 0,0313 |
| 3 | 7633 | Stone & Beam Alaina High-Back Swivel Stool | 5 | 4 | | | 0,0310 |
| 4 | 2442 | Rivet Decatur Faux Leather Kitchen Bar Stool | 9 | | 3 | | 0,0304 |
| 5 | 7760 | Rivet Modern Industrial Wood and Metal Stool | 7 | | 6 | | 0,0301 |
| 6 | 123 | Rivet Decatur Faux Leather Counter Stool | 14 | | 1 | | 0,0299 |
| 7 | 6513 | Rivet Decatur Modern Counter-Height Stool | 13 | | 5 | | 0,0291 |
| 8 | 3978 | Rivet Vermont Diamond-Stitched Counter Stool | | 15 | 4 | | 0,0290 |
| 9 | 1571 | Ravenna Home Dawn Nailhead Saddle Stool | | 16 | | 7 | 0,0281 |
| 10 | 2176 | Stone & Beam Sophia Swivel Counter Stool | 17 | 14 | | | 0,0265 |

El primero aparece en tres listas y gana con claridad; los otros nueve aparecen en dos. RRF
premia que un producto salga en varias fuentes más que un buen puesto en una sola.

Pruebas: `python -m unittest test_tienda -v` (desde `api/`) usa una base temporal de 8 productos y
comprueba RRF contra el cálculo a mano con 3 listas, que se excluye el producto consultado, una
fuente vacía y otra que falla, las sedes ordenadas por distancia y con stock, y el catálogo con sus
páginas.
