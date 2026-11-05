# BD2_Proyecto_2026-2

Minigestor de base de datos multimodal construido desde cero para el curso de Base de
Datos 2 (UTEC, ciclo 2026-2). Esta entrega cubre la Parte 1 (base de datos relacional con
tablas y SQL), la Parte 2 (datos espaciales), la Parte 3 (búsqueda de texto) y la Parte 4
(búsqueda multimedia por similitud). La Parte 5 corresponde a la Entrega Final.

Stack: C++17 para el motor, Python con FastAPI para la API, FAISS y OpenCV para la búsqueda
multimedia, y React con TypeScript para la web.

Informe del proyecto: [docs/informe/informe.pdf](docs/informe/informe.pdf).

## Arquitectura

El sistema está organizado en capas; cada una solo usa a la que tiene debajo.

```text
Interfaz web (React)        archivos, consultas, resultados, plan, mapa, galería y comparación
        │  HTTP
API (Python + FastAPI)      recibe el SQL y llama al binario motor_sql
  Multimedia (Python)       extracción SIFT y MFCC, índices IVF y HNSW de FAISS, SIMILAR_TO
        │  proceso
Consultas (C++)             parser, planificador, ejecutor, EXPLAIN [ANALYZE], transacciones
Algoritmos externos         external merge sort (ORDER BY) y external hashing (GROUP BY)
Texto y vectores            SPIMI, TF-IDF y BM25; tipo VECTOR, 3 métricas y k-NN secuencial
Índices                     B+ agrupado, B+ no agrupado, hash extensible y R-Tree en disco
Archivos                    Heap File con páginas slotted y archivo secuencial paginado
Páginas de 4 KB             gestor de páginas y buffer pool
```

Todo lo que toca el disco está en C++ y se prueba sin la interfaz. El binario
`motor_sql` recibe un lote de sentencias y responde en JSON con las filas, el plan y las
páginas leídas; la API traduce peticiones HTTP a llamadas a ese binario.

La excepción son los índices IVF y HNSW: son los de FAISS (el profesor aprobó usarlos) y viven
en la API, porque el motor no enlaza FAISS. El motor guarda la columna `VECTOR`, registra el
índice en su catálogo y anota los cambios confirmados en un archivo. La API aplica esos cambios
a FAISS, busca los vecinos y le pide al motor esas filas con `CANDIDATES`. El detalle está en
[sql_multimedia](docs/sql_multimedia.md).

## Funcionalidades

### Parte 1: base de datos relacional

| Sección | Qué se implementó | Documento |
|---|---|---|
| 2.1.1 Archivos | Heap File con páginas slotted y reutilización de espacio; archivo secuencial con área auxiliar, eliminación lazy y reorganización al 30 % | [heap](docs/heap_file_slotted_pages.md), [secuencial](docs/sequential_file_paged.md) |
| 2.1.2 Índices | B+ agrupado, B+ no agrupado, hash extensible paginado en disco | [comparación](docs/comparacion_indices/README.md), [hash](docs/extendible_hash.md) |
| 2.1.2 Algoritmos externos | merge sort k-way para `ORDER BY`, external hashing por particiones para `GROUP BY` | [external_algorithms](docs/external_algorithms.md) |
| 2.1.3 SQL | parser, planificador por reglas, `EXPLAIN` y `EXPLAIN ANALYZE` con Planning Time y Execution Time | [parser_sql](docs/parser_sql.md) |
| 2.1.4 Transacciones | `BEGIN`/`END`/`ROLLBACK`, 2PL estricto, detección de deadlocks y demo con hilos | [transacciones](docs/transacciones.md) |
| 2.1.5 Interfaz | paneles de archivos, consultas, resultados y plan (árbol y grafo) | [web/INSTRUCTIVO.md](web/INSTRUCTIVO.md) |
| 2.1.6 Comparación | Heap vs Secuencial y B+ agrupado vs no agrupado vs hash | [informe](docs/informe/informe.pdf) |

### Parte 2: base de datos espacial

| Sección | Estado |
|---|---|
| Tipo `POINT` (lat, lon en microgrados) | listo |
| Métricas euclidiana y haversine | listo ([sql_espacial](docs/sql_espacial.md)) |
| SQL: `distancia(col, POINT(...)) < metros` y `ORDER BY distancia(...) LIMIT k` | listo |
| Índice R-Tree paginado (`CREATE INDEX ... USING RTREE`) usado por el planificador | listo ([rtree](docs/rtree.md)) |
| Intersección con polígono: `dentro(col, POLYGON((lat lon, ...)))` | listo |
| Comparación secuencial vs R-Tree vs GiST de PostgreSQL | listo ([comparación espacial](docs/comparacion_espacial/README.md)) |
| Panel de mapa en la interfaz (Leaflet) | listo |
| Comparación con PostGIS (`datos/resultados/postgis_bench.sql`) | script listo, falta medir |

### Parte 3: búsqueda de texto

| Sección | Estado |
|---|---|
| Índice invertido SPIMI: bloques en memoria y merge k-way a disco | listo ([busqueda_texto](docs/busqueda_texto.md)) |
| Preprocesamiento: minúsculas, stopwords en inglés y stemming de Porter | listo |
| Ranking TF-IDF con coseno (lnc.ltc) y BM25 | listo ([ranking_texto](docs/ranking_texto.md)) |
| SQL: `CREATE INDEX ... USING SPIMI`, `MATCH`, `USING TF_IDF \| BM25` y `SCORE()` | listo |
| Comparación TF-IDF vs BM25 vs GIN de PostgreSQL con 1k, 10k y 100k documentos | listo ([comparación de texto](docs/comparacion_texto/README.md)) |
| Resultados con score y términos resaltados en la interfaz | listo |

### Parte 4: búsqueda multimedia

| Sección | Estado |
|---|---|
| `ExtractFeatures` de imágenes: SIFT, palabras visuales con K-Means y TF-IDF | listo ([extracción](docs/extraccion_caracteristicas.md)) |
| `ExtractFeatures` de audio: MFCC, palabras acústicas y TF-IDF | listo ([audio](docs/extraccion_audio.md)) |
| Tipos `FLOAT` y `VECTOR(k)` en las tres organizaciones | listo ([vectores](docs/vectores.md)) |
| Métricas euclidiana, producto punto y coseno; k-NN secuencial en el motor | listo |
| Índices IVF y HNSW de FAISS, con inserción y borrado | listo ([ivf](docs/ivf.md), [hnsw](docs/hnsw.md)) |
| SQL: `SIMILAR_TO`, `SIMILARITY_SCORE()`, `USING IVF \| HNSW`, `WITH METRIC` | listo ([sql_multimedia](docs/sql_multimedia.md)) |
| `SIMILAR_TO('foto.jpg')` con una imagen | listo; la API extrae el vector |
| `SIMILAR_TO('cancion.mp3')` con un archivo de audio | falta; el audio se busca con `VECTOR(...)` |
| Comparación IVF vs HNSW y euclidiana vs coseno con 1k, 10k y 100k imágenes | listo ([comparación vectorial](docs/comparacion_vectorial/README.md)) |
| Galería de imágenes y reproductor de audio en la interfaz | listo |
| Subir la imagen de consulta desde la interfaz | falta; la API ya tiene `POST /api/multimedia/subir` |

Ejemplo de consultas:

```sql
CREATE TABLE org FROM FILE 'datos/organizations-10000.csv' USING HEAP PRIMARY KEY Index;
CREATE INDEX idx_f ON org (Founded);
CREATE INDEX idx_emp ON org (Number_of_employees) USING HASH;
EXPLAIN ANALYZE SELECT Name FROM org WHERE Founded = 2000 ORDER BY Name LIMIT 5;

BEGIN TRANSACTION;
DELETE FROM org WHERE Index <= 10;
ROLLBACK;

CREATE TABLE tiendas (id INT PRIMARY KEY, nombre VARCHAR(20), ubicacion POINT);
INSERT INTO tiendas VALUES (1, 'Centro', POINT(-12.0464, -77.0428));
CREATE INDEX idx_geo ON tiendas (ubicacion) USING RTREE;
SELECT * FROM tiendas ORDER BY distancia(ubicacion, POINT(-12.05, -77.04)) LIMIT 3;
SELECT * FROM tiendas WHERE dentro(ubicacion, POLYGON((-12.06 -77.05, -12.06 -77.03, -12.04 -77.03, -12.04 -77.05)));

CREATE TABLE productos FROM FILE 'datos/productos_1k.csv';
CREATE INDEX productos_desc ON productos (descripcion) USING SPIMI;
SELECT id, nombre, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'wooden door photo') USING BM25 ORDER BY relevancia DESC LIMIT 5;

CREATE TABLE catalogo (id INT PRIMARY KEY, nombre VARCHAR(20), precio FLOAT, v VECTOR(4));
INSERT INTO catalogo VALUES (1, 'zapato rojo', 45.5, VECTOR(1, 0, 0, 0));
INSERT INTO catalogo VALUES (2, 'zapato azul', 80, VECTOR(0.9, 0.1, 0, 0));
INSERT INTO catalogo VALUES (3, 'mesa', 120, VECTOR(0, 0, 1, 0));
CREATE INDEX catalogo_hnsw ON catalogo (v) USING HNSW WITH (M = 16, ef_busqueda = 64);
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=2) USING HNSW WITH METRIC=cosine ORDER BY score DESC;
```

`USING HNSW` e `USING IVF` pasan por la API, así que esa última consulta se corre desde la web.
Con los datos del dataset, también se puede consultar con una imagen:
`SIMILAR_TO('ab/abdc8684.jpg', k=10)`.

El recorrido completo de las Partes 3 y 4, con el resultado esperado de cada bloque, está en
[docs/pruebas_partes_3_4.sql](docs/pruebas_partes_3_4.sql).

## Organización del código

```text
BD2_Proyecto_2026-2/
├── motor/                C++: todo lo que toca el disco y ejecuta consultas
│   ├── comun/            interfaz IFileOrganization y Registro
│   ├── archivos/         heap_file, sequential_file, pagina_slotted
│   ├── indices/          bplus_agrupado, bplus_no_agrupado, hash_extensible_disco, rtree, buffer_pool
│   ├── consultas/        parser_sql, catalogo, ejecutor, external_algorithms, motor_sql
│   ├── espacial/         distancia.h (euclidiana y haversine)
│   ├── texto/            preprocesamiento, indice_invertido (SPIMI) y ranking
│   ├── vectorial/        vector.h (tipo Vector y las 3 métricas)
│   ├── transacciones/    gestor_locks.h (2PL estricto y deadlocks)
│   └── pruebas/          pruebas de cada módulo y benchmarks
├── api/                  Python: conecta el motor con la web
│   └── multimedia/       extraer, extraer_audio, indices (FAISS), similitud y benchmarks
├── web/                  React: la interfaz
├── tools/                comando motor, preparación del dataset y recorrido SQL
├── datos/                datasets CSV y resultados de los experimentos
└── docs/                 documentación técnica e informe
```

## Manual de instalación

### Requisitos

- `g++` con soporte de C++17 (Windows: MinGW-w64; Linux: `sudo apt install g++`; macOS: `xcode-select --install`).
- Python 3.12 o superior (lo piden `numpy` y `librosa`). Se probó con 3.14.
- Node.js 22.12 o superior (solo para la interfaz web).
- `make` es opcional (en Windows viene como `mingw32-make`).

`api/requirements.txt` instala FastAPI, `faiss-cpu`, `opencv-python`, `numpy` y `librosa`. Las
opciones 1 y 3 lo hacen por ti.

### Opción 1: un solo comando

Desde la raíz del repositorio:

```bash
motor run
```

Instala las dependencias de Python y de npm, compila el motor y abre
http://127.0.0.1:5173. La primera vez tarda cerca de un minuto. En Linux y macOS, la
primera vez se lanza como `./bd2 run`, que instala el comando `motor` en `~/.local/bin`.
En Windows, `motor.cmd` ya está en la raíz. Con `make` instalado, `make run` hace lo mismo.

| Comando | Qué hace |
|---|---|
| `motor run` | prepara todo y abre la web |
| `motor test` | corre las pruebas del motor y de la interfaz (las de Python van aparte, ver [Pruebas](#pruebas)) |
| `motor setup` | solo instala y compila |
| `motor bench` | benchmarks de las estructuras en `datos/resultados/` |
| `motor clean` | borra `.venv`, `node_modules` y `.build` |

### Opción 2: el motor con make

```bash
make test                 # compila y corre todas las pruebas del motor
make motor                # solo el binario .build/motor_sql
make demo-transacciones   # demo de concurrencia con hilos (2.1.4)
make bench                # heap, secuencial y B+ agrupado con 1k, 10k y 100k filas
make bench-indices        # comparación de índices y sus gráficas
make bench-espacial       # búsqueda secuencial vs R-Tree con 1k, 10k y 100k puntos (2.2.4)
make bench-texto          # TF-IDF vs BM25 con el índice SPIMI (2.3.2)
make bench-ivf            # IVF de FAISS vs fuerza bruta
make bench-hnsw           # HNSW de FAISS: barrido de ef_busqueda y borrado lógico
make bench-vectorial      # IVF vs HNSW vs Flat vs k-NN secuencial del motor (2.4.4)
make gui                  # cliente de escritorio en Tkinter
```

En Windows con MinGW se usa `mingw32-make` en lugar de `make`.

El binario también se puede usar directo desde la consola:

```bash
.build/motor_sql --db datos/db --sql "CREATE TABLE t FROM FILE 'datos/organizations-1000.csv'; SELECT COUNT(*) FROM t"
```

### Opción 3: la web a mano

```bash
python3 -m venv .venv
.venv/bin/pip install -r api/requirements.txt
cd web && npm install && npm run dev
```

En Windows: `python`, `.venv\Scripts\pip` y `npm.cmd`.

### Datos que no están en el repositorio

`datos/productos_1k.csv` y `datos/productos_10k.csv` sí están en el repositorio y alcanzan para
probar la búsqueda de texto. Lo demás pesa demasiado y se genera o se copia aparte:

| Qué | Dónde va | Para qué | Cómo se obtiene |
|---|---|---|---|
| `productos_100k.csv` | `datos/` | `bench-texto` con 100k | `tools/dataset/preparar.py` ([dataset](docs/dataset.md)) |
| `vectores_100000.npy` | `datos/productos/` | `bench-vectorial` | `api/multimedia/extraer.py` ([extracción](docs/extraccion_caracteristicas.md)) |
| `vocabulario_256.npy` e `idf_256.npy` | `datos/productos/` | `SIMILAR_TO('foto.jpg')` | igual que los vectores |
| Imágenes de ABO | `C:\datos_bd2\imagenes` (o `BD2_DATOS`) | galería y consultas con imagen | [dataset](docs/dataset.md) |
| Audios de GTZAN | `C:\datos_bd2\audio\genres_original` | reproductor de audio | [audio](docs/extraccion_audio.md) |

### Pruebas

```bash
make test                                            # motor C++
cd web && npx playwright test                        # interfaz
cd api && python -m unittest multimedia.test_similitud multimedia.test_indices multimedia.test_extraer multimedia.test_extraer_audio multimedia.test_bench_vectorial
python tools/probar_sql.py docs/pruebas_partes_3_4.sql   # recorrido SQL por la API
```

Cada línea se corre desde la raíz. Python tiene que ser el del entorno virtual.

### Problemas frecuentes

| Síntoma | Causa y solución |
|---|---|
| `motor: orden no encontrada` | lanzarlo una vez como `./bd2 run` y abrir una terminal nueva |
| `No se encontró g++` | instalar g++ y comprobar que esté en el PATH |
| La web dice `sin conexión con la API` y la API, `el motor no respondio` | en Windows, no levantarla desde Git Bash: carga otro `libstdc++-6.dll` y el motor no arranca. Usar PowerShell, o poner primero en el PATH la carpeta del g++ que compiló el motor |
| `no existe el vocabulario visual` | falta `datos/productos/vocabulario_256.npy` (ver [Datos](#datos-que-no-están-en-el-repositorio)) |
| `No se pudo crear el entorno virtual` | en Debian o Ubuntu: `sudo apt install python3-venv` |
| El puerto 5173 u 8000 está ocupado | cerrar un `motor run` anterior |
| Algo quedó a medias | `motor clean` y volver a empezar |

## Equipo

- Luis Izaguirre (@LuisIZ)
- Dayane Rojas (@DayaneRojas1506)
- Noe Paredes (@NoeParedes)

Participaron en avances anteriores: Jimena Huamani (@jimena-mr) y Omar Chavarria (@OmarUTEC).

Planificación en el [Project board](https://github.com/users/LuisIZ/projects/1).
