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

```bash
make gui        # compila el motor y abre la interfaz (tablas, consulta, resultados, plan)
make test       # todas las pruebas
make bench      # benchmarks de heap, secuencial y B+ agrupado en datos/resultados/
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
