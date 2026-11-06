# Extracción de características de audio

Se usa el mismo pipeline de imágenes ([extraccion_caracteristicas.md](extraccion_caracteristicas.md)), pero con MFCC en lugar de SIFT. El código está en `api/multimedia/extraer_audio.py` y reutiliza `entrenar_vocabulario`, `calcular_vectores` y `guardar_vectores` de `extraer.py`.

1. `librosa` carga el audio en mono a 22 050 Hz (máximo 30 s) y calcula 13 coeficientes MFCC por cada ventana de 2048 muestras, con un salto de 512. Un audio de 30 s da unas 1 290 ventanas, y cada ventana es un descriptor de 13 dimensiones.
2. FAISS K-Means aprende K "palabras acústicas" con una muestra de hasta 1 000 000 de ventanas.
3. Cada ventana se asigna a su palabra más cercana y el audio queda como un histograma de K frecuencias.
4. Se aplica TF-IDF y norma L2, igual que en imágenes. El vector del audio tiene dimensión K.

`extract_features_audio(ruta)` devuelve el vector de un audio ya usando el vocabulario y el IDF guardados. K es 256.

## Colección

Se usa GTZAN (Kaggle, "GTZAN Dataset - Music Genre Classification"): 1000 clips de 30 s, 100 por cada uno de 10 géneros. Se quitó `jazz.00054.wav` porque está dañado y no se abre, así que son 999 audios. Los audios no se subieron al repositorio.

```powershell
# 1. CSV id,audio,genero
python tools/dataset/preparar_audio.py "C:\ruta\a\genres_original"

# 2. vectores (K=256 por defecto)
cd api
..\.venv\Scripts\python.exe -m multimedia.extraer_audio `
  --csv ..\datos\audios_1k.csv --audios "C:\ruta\a\genres_original"
```

Quedan en `datos/audio/`: `vocabulario_audio_256.npy`, `idf_audio_256.npy`, `vectores_audio_999.npy` y `vectores_audio_999.csv` (`id,vector`). Esa carpeta no se versiona. Con `--mfcc 20` se usan 20 coeficientes.

## Cargar en el motor

```sql
CREATE TABLE canciones (id INT PRIMARY KEY, vector VECTOR(256));
COPY canciones FROM FILE 'datos/audio/vectores_audio_999.csv';
SELECT id FROM canciones ORDER BY distancia(vector, VECTOR(...), 'euclidiana') LIMIT 5;
SELECT id, SIMILARITY_SCORE() FROM canciones WHERE SIMILAR_TO('blues/blues.00000.wav', k=5) WITH METRIC=euclidean;
```

`SIMILAR_TO` acepta un audio del dataset o uno subido con `POST /api/multimedia/subir`, en `wav`, `mp3`, `flac` u `ogg`. La API le aplica `extract_features_audio`, así que necesita el vocabulario y el IDF de `datos/audio/` (ver [sql_multimedia.md](sql_multimedia.md)).

El `id` es el mismo de `datos/audios_1k.csv`, que dice qué audio y qué género es cada uno.

## Escucharlos en la web

La carpeta que contiene los audios `genres_original` se copia a `C:\datos_bd2\audio\genres_original`. La API sirve cada audio en `/api/audios/<id>` y usa `datos/audios_1k.csv` para saber qué archivo es.

Si la consulta es sobre la tabla `canciones`, la galería muestra un reproductor en cada resultado en lugar de una miniatura.

## Resultados

Para cada audio se piden sus 5 vecinos más cercanos (euclidiana, sin contarse a sí mismo) y se cuenta cuántos son de su mismo género. Al azar sería 10 %.

| K | mismo género | tiempo |
|---|---|---|
| 64 | 47 % | 64 s |
| 128 | 52 % | 56 s |
| 256 | 54 % | 93 s |

Se eligió K=256, que es el valor por defecto y el mismo de las imágenes. Con K=256, clásica acierta 86 % y rock 25 %.

## Limitación

El histograma solo cuenta cuántas veces aparece cada palabra acústica, no cuándo. Se pierde el orden temporal de los sonidos, y con él el ritmo y la forma de la melodía. Géneros con instrumentos parecidos (rock, disco, country) se confunden más que los que suenan muy distinto, como clásica.
