# Comparación de índices vectoriales de imágenes

## Corrida

El benchmark de `api/multimedia/bench_vectorial.py` mide k-NN para k=10 con distancias
euclidiana y coseno. Para cada tamaño compara:

- `secuencial_exact`: ground truth exacto de FAISS (`IndexFlatL2`, o `IndexFlatIP` con
  vectores L2-normalizados para coseno).
- `IVF`: `IndexIVFFlat`, con tres valores de `nprobe`.
- `HNSW`: `IndexHNSWFlat` envuelto para asociar IDs, con varios `ef_busqueda`.

Ejemplo con la misma matriz 10k producida por el extractor:

```powershell
.\.venv\Scripts\python.exe api\multimedia\bench_vectorial.py `
  --vectores datos\productos\vectores_10000.npy `
  --tamanos 1000 10000 --metricas euclidean cosine
```

La opción `--ids` acepta un `.npy` de claves primarias alineadas con las filas del vector.
Sin `--vectores`, la prueba usa datos sintéticos deterministas de 1k/10k; esa modalidad
sirve para comprobar el benchmark, no para presentar resultados del dataset ABO.

El CSV `datos/resultados/vectorial_bench.csv` contiene tiempo de construcción, latencia
promedio por consulta, Recall@10, tamaño serializado del índice en memoria y en disco.
El script crea SVG de construcción/consulta contra N. Si se dan `--productos-csv` y
`--imagenes`, también genera HTML de galería con los vecinos devueltos frente al ground
truth exacto, indicando casos correctos y errores:

```powershell
.\.venv\Scripts\python.exe api\multimedia\bench_vectorial.py `
  --vectores datos\productos\vectores_10000.npy --tamanos 1000 10000 `
  --productos-csv datos\productos_10k.csv --imagenes C:\datos_bd2\imagenes_10k
```

El ground truth se consulta con los mismos vectores y las mismas consultas para todos los
índices. Los vectores query son filas de la colección con una pequeña perturbación
determinista; así se mide una búsqueda de vecinos y no solo el auto-match de cada fila.

## Interpretación y límites actuales

| Técnica | Ventajas | Desventajas | Recomendación |
|---|---|---|---|
| Flat exacto | Recall 1; sirve como ground truth y baseline simple | Examina toda la colección por consulta | Colecciones pequeñas y evaluación |
| IVF | Ajusta latencia/recall con `nprobe`; menos listas por consulta | Requiere entrenamiento; puede omitir vecinos | Colecciones medianas/grandes con latencia configurable |
| HNSW | Buen recall con búsqueda aproximada y no entrena centroides | Más memoria/enlaces; construcción costosa; aproxima | Búsqueda interactiva si cabe en memoria |
| Euclidiana | Geometría L2 directa de los histogramas | La escala del vector afecta la distancia | Datos sin normalizar o requisito L2 |
| Coseno | Compara orientación; TF-IDF se normaliza L2 | Requiere normalizar queries y vectores | Default para histograma TF-IDF |

El tamaño `memoria_index_bytes` es el tamaño serializado por FAISS, no RSS de todo el
proceso. La columna `secuencial_exact` es el índice Flat de FAISS, que recorre la colección
completa y sirve como baseline exacto. No mide aún una sentencia `ORDER BY ... LIMIT` en el
motor C++: el motor actual no admite columnas `VECTOR` ni SQL `SIMILAR_TO`. Tampoco guarda
resultados reales ni presenta una galería ABO en el repositorio; deben generarse después de
extraer imágenes y ejecutar la CLI.

## Pruebas

Desde la raíz del proyecto:

```powershell
$env:PYTHONPATH = "$PWD\api"
.\.venv\Scripts\python.exe -m unittest `
  multimedia.test_indices multimedia.test_extraer multimedia.test_bench_vectorial -v
```

Las pruebas del benchmark cubren 1k y 10k vectores sintéticos; las pruebas de índices
validan FAISS IVF/HNSW frente al cálculo exacto y el guardado/carga.
