# Extracción de características de imágenes ABO

## Pipeline

`api/multimedia/extraer.py` usa OpenCV SIFT y FAISS para producir vectores de bag of
visual words:

1. SIFT lee la imagen en escala de grises y produce hasta 500 keypoints/descriptores de
   128 dimensiones. Una imagen sin keypoints tiene un histograma cero.
2. `entrenar_vocabulario` toma una muestra determinista y acotada de hasta 1 000 000
   descriptores; los descriptores por imagen se descartan luego de procesarlos. FAISS
   `Kmeans(128, K)` aprende K centroides y guarda `vocabulario_K.npy`.
3. `IndexFlatL2` asigna cada descriptor de una imagen a su palabra visual. Se conserva
   solo el histograma de K frecuencias.
4. Se calcula `idf = ln((N+1)/(df+1)) + 1` sobre las imágenes procesadas; TF usa
   `1 + ln(tf)`. El vector TF-IDF se normaliza con norma L2.

El límite de descriptores de entrenamiento, K, máximo por imagen, workers y cache se
pueden configurar. `calcular_vectores` procesa imagen por imagen con `multiprocessing`;
el cache guarda histogramas, no la colección completa de descriptores. `extract_features`
usa el vocabulario e IDF ya entrenados y devuelve exactamente una matriz `float32` de
forma `(K,)`.

## Resolver las rutas del CSV y el tar ABO

La columna `imagen` de `productos_1k.csv`/`productos_10k.csv` contiene rutas relativas
como `ab/abdc8684.jpg`; el layout del tar es `images/small/<ruta>`. Se pueden extraer solo
las rutas del CSV indicado, sin desempaquetar todo el archivo:

```powershell
.\.venv\Scripts\python.exe api\multimedia\extraer.py `
  --csv datos\productos_1k.csv `
  --imagenes C:\datos_bd2\imagenes_1k `
  --archivo-tar datos\abo-images-small.tar `
  --k 256 --procesos 4
```

El tar se recorre secuencialmente; el proceso valida las rutas y falla con una lista de
faltantes si el archivo no contiene todas las imágenes referenciadas. También acepta un
directorio ya extraído. `resolver_imagen` busca en la raíz y en los layouts `images/small`,
`small` e `images`.

El resultado se escribe en `datos/productos/`:

- `vocabulario_K.npy` e `idf_K.npy`
- `vectores_<n>.npy`, matriz float32 en el orden de filas del CSV
- `vectores_<n>.csv`, encabezado `id,vector`, con valores `VECTOR(...)`
- `cache/`, histogramas por imagen, invalidado si cambia imagen o vocabulario

Si se procesan colecciones distintas con el mismo K y se necesitan conservar sus
vectores para consultas futuras, usa un archivo IDF distinto con `--idf` para cada
coleccion y selecciona el correspondiente mediante `BD2_IDF_VISUAL`. El archivo IDF
predeterminado se reemplaza en cada ejecucion.

El CSV deja los IDs del dataset junto a los vectores, pero el motor SQL aún no acepta el
tipo `VECTOR` en `COPY`; su integración corresponde al trabajo de tipos/consultas
vectoriales. El `.npy` ya es directamente utilizable por FAISS y por el benchmark.

## Ejecución y configuración

```powershell
# 1k; el vocabulario e IDF se entrenan con esta colección la primera vez
.\.venv\Scripts\python.exe api\multimedia\extraer.py `
  --csv datos\productos_1k.csv --imagenes C:\datos_bd2\imagenes_1k --k 256

# 10k; mismo K, vocabulario ya existente, y cache de histogramas
.\.venv\Scripts\python.exe api\multimedia\extraer.py `
  --csv datos\productos_10k.csv --imagenes C:\datos_bd2\imagenes_10k --k 256
```

`--limite 1000` permite probar una porción de un CSV grande. `--muestra-descriptores`,
`--procesos`, `--cache`, `--vocabulario`, `--idf` y `--salida` aceptan valores
personalizados. Después de generar los artefactos se puede seleccionar el modelo para
`extract_features(ruta)` con `BD2_K`, `BD2_VOCABULARIO` y `BD2_IDF_VISUAL`.

El tiempo total se imprime al final de cada corrida para anotar el caso de 100k. No se
publican tiempos de 100k en este repositorio porque ese conjunto requiere extraer las
imágenes y ejecutar el pipeline en la máquina que lo procesa.

## Dependencias y pruebas

`api/requirements.txt` declara `numpy`, `faiss-cpu` y `opencv-python`. La instalación de
estas tres dependencias se verificó en Windows con el entorno `.venv` del proyecto.

```powershell
$env:PYTHONPATH = "$PWD\api"
.\.venv\Scripts\python.exe -m unittest `
  multimedia.test_extraer multimedia.test_indices multimedia.test_bench_vectorial -v
```

La prueba de SIFT verifica dimensión 128, vector estable para la misma imagen, norma L2 y
vecindad entre una imagen y otra escalada/rotada. La prueba de benchmark usa colecciones
sintéticas de 1k y 10k para cubrir el código de IVF/HNSW, no reemplaza una corrida con las
imágenes reales de los CSV.
