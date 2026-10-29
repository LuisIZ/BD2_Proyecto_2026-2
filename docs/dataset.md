# Dataset de productos (ABO)

Las Partes 3 y 4 usan el mismo dataset: **Amazon Berkeley Objects** (ABO), listings de
productos de Amazon con texto en varios idiomas e imágenes de catálogo ya reducidas. De
cada producto se indexa el texto (Parte 3) y la imagen principal (Parte 4), y como ambos
comparten el `id`, una consulta puede cruzar las dos búsquedas. También sirve para la
Parte 5 (opción B).

- Página: <https://amazon-berkeley-objects.s3.amazonaws.com/index.html>
- Licencia: Creative Commons Attribution 4.0 International (CC BY 4.0).

## Licencia y cita

ABO se publica bajo CC BY 4.0: se puede copiar, modificar y redistribuir, dando crédito y
señalando los cambios. La atribución que pide el README del dataset es:

> Credit for the data, including all images and 3d models, must be given to: Amazon.com
>
> Credit for building the dataset, archives and benchmark sets must be given to:
> Matthieu Guillaumin (Amazon.com), Thomas Dideriksen (Amazon.com), Kenan Deng
> (Amazon.com), Himanshu Arora (Amazon.com), Jasmine Collins (UC Berkeley) and
> Jitendra Malik (UC Berkeley)

Cambios que hacemos: nos quedamos con un registro por producto, solo el texto en
inglés, quitamos el HTML y recortamos la descripción (ver abajo).

## Descarga

Los archivos van fuera del repo y de OneDrive, en `BD2_DATOS` (por defecto
`C:\datos_bd2`):

```bash
mkdir -p /c/datos_bd2/abo && cd /c/datos_bd2/abo
curl -LO https://amazon-berkeley-objects.s3.amazonaws.com/archives/abo-listings.tar
curl -LO https://amazon-berkeley-objects.s3.amazonaws.com/archives/abo-images-small.tar
curl -LO https://amazon-berkeley-objects.s3.amazonaws.com/LICENSE-CC-BY-4.0.txt
tar -xf abo-listings.tar && tar -xf abo-images-small.tar
```

| Archivo | Tamaño | Contenido |
|---|---|---|
| `abo-listings.tar` | 87 MB | `listings/metadata/listings_{0..f}.json.gz`, 16 archivos JSON Lines |
| `abo-images-small.tar` | 3,25 GB | `images/metadata/images.csv.gz` (`image_id, height, width, path`) e `images/small/` (398 212 imágenes, lado mayor ≤ 256 px) |
| `LICENSE-CC-BY-4.0.txt` | 14 KB | texto de la licencia |

Descomprimido ocupa otro tanto; los `.tar` se pueden borrar después.

## Conteos

Los 147 702 registros de ABO son *listings*: el mismo `item_id` aparece una vez por
marketplace (amazon.com, amazon.in, amazon.de…).

| Paso | Cantidad |
|---|---:|
| Listings | 147 702 |
| Listings sin imagen principal en `images.csv.gz` | 575 |
| Productos únicos (`item_id`) con imagen | 145 050 |
| … con nombre en inglés | **121 549** |
| … solo en otros idiomas | 23 501 |

Con 121 549 productos en inglés alcanza para 100 000, así que no hizo falta completar con
otros idiomas ni con `other_image_id` (el script lo haría si faltaran).

Los tipos están muy cargados hacia las fundas de celular, algo a tener en cuenta al
armar consultas y juicios de relevancia (#56):

| `tipo` | Productos en 100k |
|---|---:|
| `CELLULAR_PHONE_CASE` | 53 355 |
| `SHOES` | 8 027 |
| `GROCERY` | 5 044 |
| otros 514 tipos | 33 574 |

## Tabla `productos`

```sql
CREATE TABLE productos (
    id INT PRIMARY KEY,
    item_id VARCHAR(20),
    nombre VARCHAR(200),
    tipo VARCHAR(50),
    categoria VARCHAR(200),
    marca VARCHAR(80),
    color VARCHAR(40),
    descripcion VARCHAR(1000),
    imagen VARCHAR(80)
);
COPY productos FROM FILE 'datos/productos_10k.csv';
```

| Columna | Origen en ABO | Notas |
|---|---|---|
| `id` | generado | 1..n en el orden del CSV; es el mismo producto en los tres tamaños |
| `item_id` | `item_id` | identificador de Amazon (ASIN) |
| `nombre` | `item_name` | valor en inglés, primero `en_US` |
| `tipo` | `product_type` | por ejemplo `SHOES` |
| `categoria` | `node[0].node_name` | ruta, por ejemplo `/Categories/Men/Accessories/Neck Gaiters`; vacía en ~5 % |
| `marca` | `brand` | vacía en ~3 % |
| `color` | `color[].standardized_values[0]` | valor estandarizado; vacío en ~20 % y `multi-colored` en la mitad (fundas) |
| `descripcion` | `item_name + bullet_point + product_description` | sin HTML ni saltos de línea, recortada a 1 000 bytes |
| `imagen` | `path` de `main_image_id` en `images.csv.gz` | ruta relativa dentro de `images/small/`, por ejemplo `ab/abdc8684.jpg` |

Reglas de la limpieza:

- De cada campo multilingüe se toma el valor con `language_tag` que empieza con `en_`
  (primero `en_US`).
- Un solo registro por `item_id`: el listing con la descripción en inglés más larga.
- Los VARCHAR se recortan en **bytes** UTF-8, sin partir caracteres, porque el motor mide
  el largo en bytes y rechaza el `COPY` si una fila se pasa.
- Sin saltos de línea: el lector de CSV del motor lee una línea por registro. Un registro
  ocupa como mucho ~1,7 KB y cabe en una página de 4 KB.

## Archivos generados

Los subconjuntos son anidados (1k ⊂ 10k ⊂ 100k): salen del mismo orden aleatorio con
semilla 2026, así que los primeros 1 000 productos de cualquier CSV son los mismos.

| Archivo | Filas | Tamaño | Imágenes distintas | Dónde está |
|---|---:|---:|---:|---|
| `datos/productos_1k.csv` | 1 000 | 0,7 MB | 991 | repo |
| `datos/productos_10k.csv` | 10 000 | 6,7 MB | 9 755 | repo |
| `datos/productos_100k.csv` | 100 000 | 67 MB | 90 313 | se regenera (en `.gitignore`) |
| `imagenes_1k.zip` | — | 6,5 MB | 991 | Drive del grupo |
| `imagenes_10k.zip` | — | 65 MB | 9 755 | Drive del grupo |
| `C:\datos_bd2\imagenes\` | — | 793 MB | 90 313 | se regenera |

Hay menos imágenes que productos porque algunos productos distintos comparten la misma
foto principal. Todas son JPG. El `COPY` de los tres CSV en el motor carga sin errores
(100 000 filas en ~1 s).

## Cómo regenerarlo

```bash
python tools/dataset/preparar.py                                   # ~20 s, escribe datos/productos_*.csv
python tools/dataset/extraer_imagenes.py datos/productos_100k.csv  # copia a C:\datos_bd2\imagenes
python tools/dataset/extraer_imagenes.py datos/productos_1k.csv --destino C:/datos_bd2/imagenes_1k --zip
```

Las rutas por defecto salen de la variable `BD2_DATOS` (o `C:\datos_bd2` si no está
definida); se pueden cambiar con `--abo`, `--salida` y `--destino`.
