# BD2_Proyecto_2026-2

Minigestor de base de datos implementado desde cero para el curso de Base de Datos 2.
Incluye gestión de archivos, índices, procesamiento de consultas SQL, transacciones e interfaz web.

**Stack:** C++17 (motor) · Python + FastAPI (API) · React + TypeScript (web)

## Estructura del proyecto

```text
BD2_Proyecto_2026-2/
├── motor/          # C++: todo lo que toca el disco y ejecuta consultas
│   ├── comun/      # lo compartido: pagina.h, indice.h, tabla.h
│   ├── archivos/   # heap y secuencial
│   ├── indices/    # B+ agrupado, B+ no agrupado, hash
│   ├── consultas/  # parser y ejecución
│   └── pruebas/
├── api/            # Para guardar las métricas de comparación técnica
├── api/            # Python: conecta el motor con la web
├── web/            # React: la interfaz
├── datos/          # CSVs y resultados de los experimentos
└── docs/           # informe y diagramas
```

`motor/comun/` es la única carpeta compartida: define los tipos y las interfaces
que todos usamos. Cambiar algo ahí se avisa al grupo. En las demás carpetas,
cada quien trabaja sin pedir permiso.

## Compilar y correr

### Interfaz React

La interfaz web de la primera entrega está en [`web/`](web/README.md). Incluye los cuatro paneles, carga CSV, consultas reales, una comparación de Heap, Secuencial, B+ agrupado y B+ no agrupado con las mismas consultas y un visor de mediciones.

Desde la raíz del repo recién clonado, **un solo comando**:

```bash
motor run
```

Instala las dependencias de Python y de npm, compila el motor C++ y abre
http://127.0.0.1:5173. No hay que crear entornos virtuales ni activar nada a
mano. La primera vez tarda cerca de un minuto; las siguientes arrancan en
segundos, porque cada paso se salta si ya está hecho. `Ctrl + C` cierra la API y
la web a la vez.

La primerísima vez, en Linux y macOS, hay que lanzarlo una vez como `./bd2 run`:
eso instala el comando `motor` en `~/.local/bin` y a partir de ahí `motor run`
funciona desde cualquier carpeta. (El ejecutable vive en `tools/bin/motor` y no
en la raíz porque ahí ya está la carpeta `motor/` del código C++, y un archivo no
puede llamarse igual que una carpeta. En Windows no hace falta: `motor.cmd` está
en la raíz y `motor run` funciona desde el primer momento.)

Con `make` instalado, `make run` hace lo mismo sin instalar nada.

Las demás acciones:

| Comando | Qué hace |
|---|---|
| `motor run` | prepara todo y abre la web (lo normal) |
| `motor test` | prepara todo y corre las pruebas del motor y de la interfaz |
| `motor setup` | solo instala y compila, sin abrir nada |
| `motor bench` | benchmarks de las estructuras, a `datos/resultados/` |
| `motor clean` | borra `.venv`, `node_modules` y `.build`; no toca `datos/db` |

Requisitos: **Node.js 22.12+**, **Python 3.10+** y **`g++`** en el PATH. El
comando los comprueba al arrancar y dice qué falta y cómo instalarlo.

Si algo falla:

| Síntoma | Causa y arreglo |
|---|---|
| `motor: orden no encontrada` | Lánzalo una vez como `./bd2 run`; si avisa de que `~/.local/bin` no está en el PATH, sigue lo que indica y abre una terminal nueva |
| `No se encontró Node.js` pero sí lo tienes | Lo gestiona nvm, fnm o volta, que solo entran al PATH al abrir una terminal. El comando los busca solo; si aun así falla, abre una terminal nueva o `source ~/.nvm/nvm.sh` |
| `no se encontro Python` | Instala Python 3.10 o superior desde python.org |
| `No se encontró g++` | Windows: MinGW-w64 · Linux: `sudo apt install g++` · macOS: `xcode-select --install` |
| `No se pudo crear el entorno virtual` | En Debian o Ubuntu falta: `sudo apt install python3-venv` |
| El puerto 5173 u 8000 está ocupado | Queda un `motor run` anterior abierto; ciérralo |
| Algo quedó a medias | `motor clean` y vuelve a empezar |

<details>
<summary>Levantarlo a mano, sin el comando</summary>

```bash
python3 -m venv .venv
.venv/bin/pip install -r api/requirements.txt
cd web && npm install && npm run dev
```

En Windows, `python`, `.venv\Scripts\pip` y `npm.cmd`.
</details>

La interfaz incluye `EXPLAIN` y `EXPLAIN ANALYZE` con el plan en árbol y en grafo, la
elección de clave primaria e índices al cargar un CSV, y gráficas comparativas que se
regeneran con cada medición.

Manual: [web/INSTRUCTIVO.md](web/INSTRUCTIVO.md). Gramática y planificador:
[docs/parser_sql.md](docs/parser_sql.md). Por qué el B+ agrupado gana en búsquedas y
pierde en recorridos: [docs/bplus_agrupado_vs_no_agrupado.md](docs/bplus_agrupado_vs_no_agrupado.md).
Por qué la carga de heap y secuencial era lenta: [docs/carga_masiva.md](docs/carga_masiva.md).

### Motor y cliente de escritorio

```bash
make gui        # compila el motor y abre la interfaz (tablas, consulta, resultados, plan)
make test       # todas las pruebas
make bench      # benchmarks de heap, secuencial y B+ agrupado en datos/resultados/
make bench-agrupado  # B+ agrupado vs heap + B+ no agrupado, a traves del motor SQL
make clean
```

En Windows con MinGW: `mingw32-make gui`. Sin make, `python api/ui_sql.py` compila el
motor por su cuenta. Consola: `python3 api/motor_cli.py "SHOW TABLES"`. Gramática y
planificador en [docs/parser_sql.md](docs/parser_sql.md).

## Equipo

- @DayaneRojas1506
- @OmarUTEC
- @LuisIZ
- @NoeParedes
- @jimena-mr

Planificación en el [Project board](https://github.com/users/LuisIZ/projects/1)
