# Parser SQL, ejecutor y cliente

## 1. Archivos

| Archivo | Qué es |
|---|---|
| `motor/consultas/valor.h` | `Valor` (entero o texto) y `Fila`. |
| `motor/consultas/parser_sql.{h,cpp}` | Lexer + parser de descenso recursivo. Devuelve una `Sentencia`. |
| `motor/consultas/catalogo.{h,cpp}` | Esquema de tablas, índices, serialización de filas a bytes y persistencia en `catalogo.txt`. |
| `motor/consultas/ejecutor.{h,cpp}` | Planificador de acceso y ejecución sobre Heap, Sequential File, B+ agrupado y B+ no agrupado. ORDER BY y GROUP BY usan `external_algorithms.h`. |
| `motor/consultas/motor_sql.cpp` | CLI: recibe SQL, responde JSON. |
| `motor/pruebas/sql_test.cpp` | Parser + ejecutor end-to-end con las tres organizaciones e índice secundario. |
| `api/motor_cli.py` | Envuelve el binario desde Python (compila si hace falta). Base para la API REST. |
| `api/ui_sql.py` | Cliente Tkinter con los 4 paneles: tablas, consulta, resultados, plan. |

## 2. Cómo correr

```bash
make gui                                   # compila .build/motor_sql y abre la interfaz
make test                                  # pruebas, incluida sql_test
python3 api/ui_sql.py                      # sin make: la UI compila el motor por su cuenta
python3 api/motor_cli.py "SHOW TABLES"     # consola
```

En Windows: `mingw32-make gui` (o `python api/ui_sql.py`).

Requisitos: `g++` en el PATH y Python 3 con Tkinter (viene con el instalador oficial de
Python en Windows; en Debian/Ubuntu `apt install python3-tk`). La base vive en
`datos/db/` (ignorada por git): `catalogo.txt` más un archivo por tabla e índice.

El binario también se usa directo:

```bash
.build/motor_sql --db datos/db --sql "SELECT * FROM t WHERE Index = 5; SHOW TABLES"
.build/motor_sql --db datos/db --catalogo
echo "SELECT COUNT(*) FROM t" | .build/motor_sql --db datos/db
```

## 3. Gramática

Palabras clave sin distinguir mayúsculas. Varias sentencias se separan con `;`.

```sql
CREATE TABLE t (col INT [PRIMARY KEY], col VARCHAR(n), ...) [USING HEAP | SEQUENTIAL | BPLUS]
CREATE TABLE t FROM FILE 'ruta.csv' [USING ...] [PRIMARY KEY col]
CREATE INDEX nombre ON t (col) [USING BPLUS | HASH]
DROP TABLE t
INSERT INTO t VALUES (v1, v2, ...)
DELETE FROM t [WHERE cond]
SELECT * | col, COUNT(*), SUM(col), AVG(col), MIN(col), MAX(col)
  FROM t [WHERE cond [AND cond]...] [GROUP BY col] [ORDER BY col [ASC|DESC]] [LIMIT n]
SHOW TABLES
DESCRIBE t

cond := col (= | != | <> | < | <= | > | >=) valor
      | col BETWEEN a AND b
```

Tipos: `INT` (int32) y `VARCHAR(n)`. La clave primaria debe ser `INT`; si no se indica
se toma la primera columna `INT`. `FROM FILE` infiere el esquema del CSV: `INT` si toda
la columna son enteros, si no `VARCHAR` del largo máximo; los nombres de columna se
normalizan a identificadores (`Organization Id` → `Organization_Id`).

## 4. Cómo se guarda una tabla

| `USING` | Estructura | Formato de fila | Índices secundarios |
|---|---|---|---|
| `HEAP` (defecto) | `HeapFile`, páginas slotted | variable: clave aparte, resto empaquetado (`INT` 4 B, `VARCHAR` 2 B largo + bytes) | sí, B+ no agrupado sobre columnas `INT` |
| `SEQUENTIAL` | `SequentialFile` | igual que heap (mismos bytes por fila) | no: la reorganización reubica registros |
| `BPLUS` | `BPlusAgrupado` | fijo: `[pk][INT 4 B \| VARCHAR n B]...` | no: la fila vive en la hoja |

La carga con `FROM FILE` usa `cargar_masivo` en el B+ agrupado (hojas al 90 %) e inserción
una a una en heap y secuencial. La clave primaria se verifica siempre al insertar: el B+
agrupado la rechaza solo; en heap se consulta el índice sobre la clave si existe (si no,
recorrido completo); en secuencial, búsqueda binaria.

## 5. Planificador

Para cada `SELECT` o `DELETE` se elige **una** condición del `WHERE` que pueda
resolverse con una estructura y el resto se filtra en memoria:

| Condición | Heap | Secuencial | B+ agrupado |
|---|---|---|---|
| columna con índice B+, `=` | `busqueda_por_indice` | — | — |
| columna con índice B+, `BETWEEN` `<` `<=` `>` `>=` | `rango_por_indice` | — | — |
| clave primaria, `=` | `scan_completo` (no hay índice) | `busqueda_por_clave` | `busqueda_por_clave` |
| clave primaria, rango | `scan_completo` | `rango_por_clave` | `rango_por_clave` |
| cualquier otra | `scan_completo` + `filtro` | `scan_completo` + `filtro` | `scan_completo` + `filtro` |

Después: `agrupacion` (`ExternalHashAggregate`), `ordenamiento` (`ExternalMergeSort`
k-way, reporta `initial_runs` y `merge_passes`), `proyeccion` y `limite`. Cada paso lleva
`paginas_leidas` de la estructura que tocó, así el panel de plan muestra el costo real:

```
[3] select: 12 filas  (0.30 ms)
      rango_por_clave   estructura=secuencial  condicion=Index BETWEEN 1 AND 12  paginas_leidas=15  filas=12
      proyeccion        columnas=4
```

## 6. Formato JSON

```json
{"ok":true,"tipo":"select","mensaje":"12 filas","afectadas":12,"tiempo_ms":0.3,
 "columnas":["Index","Name"],"filas":[[1,"Acevedo LLC"],...],
 "plan":[{"operacion":"rango_por_clave","estructura":"secuencial","paginas_leidas":"15","filas":"12"}]}
```

Errores: `{"ok":false,"error":"la columna Foo no existe en demo"}`. Con `--sql` la
respuesta es un arreglo con un objeto por sentencia. `--catalogo` devuelve tablas,
columnas e índices para el panel de archivos.

## 7. Límites conocidos

- Sin `UPDATE`, sin `OR`, sin subconsultas. `WHERE` solo une con `AND`.
- Del `JOIN` solo hay `INNER`, uno por consulta y con una sola igualdad en el `ON`.
  No hay alias: los calificadores son nombres de tabla (`organizaciones.Name`), así que
  tampoco se puede unir una tabla consigo misma.
- Agregados solo sobre columnas `INT`; `AVG` devuelve texto con dos decimales.
- `CREATE INDEX ... USING HASH` responde "aún no disponible en disco" hasta que el hash
  extensible salga de RAM.
- Claves repetidas: la clave primaria es única; el resto de columnas admite repetidos.
- Cada llamada al binario abre y cierra los archivos: todo queda en disco entre
  sentencias, pero la caché del B+ arranca fría en cada consulta.
- Sin transacciones ni locks (sección 2.1.4 pendiente).
