# Detección de copyright: base de originales

La opción C del Anexo A detecta si una canción contiene fragmentos de otras. Para eso la base de originales no guarda un vector por canción, sino uno por cada tramo de la canción. Así un fragmento corto se compara con un tramo de su mismo tamaño y no con el clip entero.

La base son los 999 clips de 30 s de GTZAN (ver [extraccion_audio.md](extraccion_audio.md)).

## Cómo se parte

Cada clip se parte en tramos de 10 s, uno cada 5 s:

```
0-10, 5-15, 10-20, 15-25, 20-30   → 5 tramos por clip
```

Varios clips duran 29,98 s y no 30, así que el último tramo se acepta si le falta menos de medio segundo.

Cada tramo se convierte en vector con el código de `api/multimedia/segmentos_audio.py`:

1. `extraer_descriptores_audio(ruta, offset=inicio, duracion=10)` lee solo ese tramo y calcula sus MFCC: unas 431 ventanas de 13 coeficientes.
2. Se normaliza el tramo: a cada coeficiente se le resta su promedio y se divide por su desviación dentro del tramo.
3. Cada ventana se asigna a su palabra acústica más cercana del vocabulario `vocabulario_copyright_256.npy` y se arma el histograma.
4. Se aplica TF-IDF con `idf_copyright_256.npy` y norma L2. El resultado es un `VECTOR(256)`.

El vocabulario (K-Means de FAISS con hasta 1 000 000 de ventanas normalizadas) y el IDF se calculan con los propios tramos. No son los de `extraccion_audio.md`, así que `SIMILAR_TO('cancion.mp3')` no sirve sobre `segmentos`: un audio de consulta se analiza con `POST /api/copyright/analizar`, que lo procesa igual que los tramos.

## Por qué se normaliza cada tramo

Sin normalizar, el coeficiente 0 del MFCC mide la energía y es mucho más grande que los otros. Al bajar el volumen cambia en todas las ventanas, el K-Means las manda a otras palabras y el histograma sale distinto. Normalizar quita ese efecto, porque una constante sumada a un coeficiente desaparece al restar el promedio, y también reduce el efecto del ruido.

Se midió con 100 recortes de 15 s de canciones al azar, buscados entre los 4 995 tramos. La tabla dice en qué fracción de los tramos de consulta el tramo más parecido es de la canción correcta:

| Caso | Sin normalizar | Normalizado |
|---|---|---|
| Recorte limpio | 0,96 | 0,98 |
| Volumen −6 dB | 0,07 | 0,98 |
| Volumen +6 dB | 0,06 | 0,95 |
| Ruido blanco, SNR 20 dB | 0,13 | 0,78 |
| Velocidad +5 % | 0,99 | 0,97 |
| Velocidad −5 % | 0,76 | 0,94 |
| Mezcla de dos canciones | 0,08 | 0,41 |

Además separa mejor lo correcto de lo incorrecto: en el recorte limpio la mediana de similitud con la canción correcta es 0,91 y con la mejor otra 0,66 (sin normalizar, 0,97 y 0,81). Quitar solo el coeficiente 0 arreglaba el volumen pero no el ruido. La mezcla sigue siendo difícil, porque el histograma junta las palabras de las dos canciones.

## Por qué 10 s cada 5 s

- **10 s de largo:** dan unas 431 ventanas, suficientes para que el histograma sea estable. Con tramos de 5 s serían 216 ventanas y el histograma cambiaría más con poco ruido.
- **Cada 5 s (50 % de solapamiento):** cualquier fragmento de 10 s del original comparte al menos 7,5 s con algún tramo, y cualquier fragmento de 15 s contiene un tramo completo. Sin solapamiento (cada 10 s) un fragmento podría caer mitad en un tramo y mitad en otro.
- **Costo:** 5 tramos por clip en vez de 3, unos 5 000 vectores en total. Sigue siendo una base pequeña.

## Generar los tramos

```powershell
cd api
..\.venv\Scripts\python.exe -m multimedia.segmentos_audio --hop 5 --duracion 10
```

Lee `datos/audios_1k.csv` y los audios de `C:\datos_bd2\audio\genres_original` (se cambia con `--csv` y `--audios`). Reparte las canciones entre varios procesos y escribe en `datos/audio/`:

| Archivo | Columnas | Filas |
|---|---|---|
| `canciones.csv` | `id, archivo, genero` | 999 |
| `segmentos.csv` | `id, cancion_id, inicio, fin, v` | 4 995 |

`v` va en el formato `VECTOR(...)` que acepta `COPY`. `segmentos.csv` pesa 8,9 MB. También quedan `vocabulario_copyright_256.npy` (si ya existe, se reutiliza) e `idf_copyright_256.npy`. Con 8 procesos los 999 clips tardaron **30 s**, contando el K-Means. Cada proceso usa un solo hilo: con varios hilos cada uno se estorbaban y tardaba 704 s. La carpeta `datos/audio/` no se versiona.

## Cargar en el motor

`docs/copyright.sql` crea las tablas `canciones` y `segmentos`, las carga con `COPY` y crea dos índices:

- `segmentos_hnsw`, un HNSW sobre `segmentos(v)` para buscar los tramos más parecidos;
- `segmentos_cancion`, un B+ sobre `segmentos(cancion_id)` para traer los tramos de una canción.

Se puede pegar en la web o correr con `python tools/probar_sql.py docs/copyright.sql`.

## Control

Se tomaron 20 tramos al azar de la base (semilla 2026) y se buscó cada uno con `SIMILAR_TO(VECTOR(...), k = 5)`, con HNSW y sin índice:

| Medida | Resultado |
|---|---|
| El primero es el mismo tramo (similitud 1) | 20 de 20 |
| Recall@5 de HNSW contra la búsqueda sin índice | 1,000 |

Los otros vecinos suelen ser los tramos de al lado de la misma canción, porque se solapan 5 s con el buscado.

## Pruebas

```powershell
cd api
..\.venv\Scripts\python.exe -m unittest multimedia.test_segmentos
```

Usan audios sintéticos y revisan cuántos tramos salen, sus inicios y fines, la normalización, que un tramo da el mismo vector que ese recorte guardado como archivo aparte y que a la mitad del volumen el vector casi no cambia.
