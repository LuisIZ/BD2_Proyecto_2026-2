# BD2_Proyecto_2026-2

Minigestor de base de datos multimodal construido desde cero para el curso de Base de
Datos 2 (UTEC, ciclo 2026-2). Esta entrega cubre la Parte 1 (base de datos relacional con
tablas y SQL) y la Parte 2 (datos espaciales con coordenadas geográficas).

Stack: C++17 para el motor, Python con FastAPI para la API y React con TypeScript para la web.

Informe del proyecto: [docs/informe/informe.pdf](docs/informe/informe.pdf).

## Arquitectura

El sistema está organizado en capas; cada una solo usa a la que tiene debajo.

```text
Interfaz web (React)        archivos, consultas, resultados, plan de ejecución y comparación
        │  HTTP
API (Python + FastAPI)      recibe el SQL y llama al binario motor_sql
        │  proceso
Consultas (C++)             parser, planificador, ejecutor, EXPLAIN [ANALYZE], transacciones
Algoritmos externos         external merge sort (ORDER BY) y external hashing (GROUP BY)
Índices                     B+ agrupado, B+ no agrupado, hash extensible en disco
Archivos                    Heap File con páginas slotted y archivo secuencial paginado
Páginas de 4 KB             gestor de páginas y buffer pool
```

Todo lo que toca el disco está en C++ y se prueba sin la interfaz. El binario
`motor_sql` recibe un lote de sentencias y responde en JSON con las filas, el plan y las
páginas leídas; la API solo traduce peticiones HTTP a llamadas a ese binario.

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
| SQL: `distancia(col, POINT(...)) < metros` y `ORDER BY distancia(...) LIMIT k` | listo, con recorrido secuencial |
| Índice R-Tree, intersección con polígonos, mapa y comparación con GiST | pendiente |

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
SELECT * FROM tiendas ORDER BY distancia(ubicacion, POINT(-12.05, -77.04)) LIMIT 3;
```

## Organización del código

```text
BD2_Proyecto_2026-2/
├── motor/                C++: todo lo que toca el disco y ejecuta consultas
│   ├── comun/            interfaz IFileOrganization y Registro
│   ├── archivos/         heap_file, sequential_file, pagina_slotted
│   ├── indices/          bplus_agrupado, bplus_no_agrupado, hash_extensible_disco, buffer_pool
│   ├── consultas/        parser_sql, catalogo, ejecutor, external_algorithms, motor_sql
│   ├── espacial/         distancia.h (euclidiana y haversine)
│   ├── transacciones/    gestor_locks.h (2PL estricto y deadlocks)
│   └── pruebas/          pruebas de cada módulo y benchmarks
├── api/                  Python: conecta el motor con la web
├── web/                  React: la interfaz
├── tools/                comando motor (instala, compila y abre la web)
├── datos/                datasets CSV y resultados de los experimentos
└── docs/                 documentación técnica e informe
```

## Manual de instalación

### Requisitos

- `g++` con soporte de C++17 (Windows: MinGW-w64; Linux: `sudo apt install g++`; macOS: `xcode-select --install`).
- Python 3.10 o superior.
- Node.js 22.12 o superior (solo para la interfaz web).
- `make` es opcional (en Windows viene como `mingw32-make`).

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
| `motor test` | corre las pruebas del motor y de la interfaz |
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

### Problemas frecuentes

| Síntoma | Causa y solución |
|---|---|
| `motor: orden no encontrada` | lanzarlo una vez como `./bd2 run` y abrir una terminal nueva |
| `No se encontró g++` | instalar g++ y comprobar que esté en el PATH |
| `No se pudo crear el entorno virtual` | en Debian o Ubuntu: `sudo apt install python3-venv` |
| El puerto 5173 u 8000 está ocupado | cerrar un `motor run` anterior |
| Algo quedó a medias | `motor clean` y volver a empezar |

## Equipo

- Luis Izaguirre (@LuisIZ)
- Dayane Rojas (@DayaneRojas1506)
- Noe Paredes (@NoeParedes)
- Jimena Huamani (@jimena-mr)
- Omar Chavarria (@OmarUTEC)

Planificación en el [Project board](https://github.com/users/LuisIZ/projects/1).
