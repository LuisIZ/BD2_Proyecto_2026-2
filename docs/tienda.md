# Tienda (Parte 5): datos

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
