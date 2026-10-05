# Instructivo de la interfaz

La interfaz ejecuta SQL sobre el motor C++ del proyecto. Las filas, los planes, las páginas leídas y los tiempos vienen del motor; nada es simulado. Los datos se guardan en `datos/db/`.

## 1. Instalar (una sola vez)

Necesitas Node.js 22.12 o superior, Python 3.10 o superior y `g++` con C++17 en el PATH.

Desde la raíz del proyecto, en PowerShell:

```powershell
python -m pip install -r api/requirements.txt
cd web
npm.cmd install
```

## 2. Iniciar

Desde `web`:

```powershell
npm.cmd run dev
```

Abre **http://127.0.0.1:5173** y deja la terminal abierta. La primera vez se compila el motor y puede tardar. Cuando la API responde, arriba a la derecha aparece `datos/db · N tablas`. Para detenerla usa **Ctrl + C**.

En Linux o macOS usa `npm` en lugar de `npm.cmd`.

## 3. Consultas

| Panel | Para qué sirve |
|---|---|
| Archivos | Tablas cargadas, columnas, clave primaria, organización e índices. **Cargar CSV** crea una tabla nueva. |
| Consulta SQL | Editor con números de línea, ejemplos e historial. **Ctrl + Enter** ejecuta todo (o el texto seleccionado). **Shift + Enter** o **Ejecutar sentencia** ejecuta solo la sentencia donde está el cursor, para ir una por una. Los comentarios `-- ...` se ignoran. |
| Resultados | Filas en páginas de 50 y exportación a CSV. Con varias sentencias aparece una pestaña por sentencia. |
| Plan de ejecución | Pasos del motor: acceso usado, índice, páginas leídas, algoritmo de ordenamiento o agrupación. |

Primera prueba: **Cargar CSV** → `organizations-1000.csv` → nombre `organizaciones` → Heap File → **Cargar tabla**. Luego **Consultar esta tabla** y **Ejecutar**.

La pestaña **Sintaxis** resume las sentencias que acepta el parser y el acceso que elige el planificador según la condición del `WHERE`.

## 4. Demo guiada

La pestaña **Demo guiada** recorre el guion de la exposición paso por paso, con los datos de **1k, 10k o 100k** registros (botones arriba a la derecha). Cada paso muestra su SQL, y al ejecutarlo, el resumen (filas, acceso usado, páginas leídas, tiempo); a la derecha aparecen los resultados y el plan del paso seleccionado.

| Control | Qué hace |
|---|---|
| **1k / 10k / 100k** | Elige el CSV. Cambiarlo reinicia el guion; los valores del guion escalan con el tamaño (clave buscada = n/2, rango desde n/10, DELETE hasta 40 % de n, INSERT con clave n+1). Con 1k son los valores originales: 500, 100–120, 1–400 y 1001. |
| **Ejecutar paso N** o **Ctrl + Enter** | Ejecuta el siguiente paso pendiente. |
| **Ejecutar** / **Repetir** en cada fila | Ejecuta ese paso por separado, en cualquier orden. |
| Clic sobre el SQL de un paso ya ejecutado | Muestra sus resultados y su plan a la derecha. |
| **Reiniciar** | Borra los resultados de la pantalla (no toca las tablas). |
| **Abrir guion en el editor** | Copia el guion completo, con comentarios, a Consultas. |

Secciones del guion:

0. **Limpiar**: `DROP TABLE` de `org_heap`, `org_seq`, `org_bp` y `organizaciones`. Si no existen, el paso se marca como omitido.
1. **Cargar** el mismo CSV en Heap, Secuencial y B+ agrupado; `SHOW TABLES` y `DESCRIBE`.
2. **Misma consulta, tres estructuras**: `Index = k` en las tres; comparar `paginas_leidas`.
3. **Rango** `Index BETWEEN a AND b` en las tres.
4. **Índice secundario** sobre `Founded`: la consulta antes y después de `CREATE INDEX`, y un rango con `ORDER BY`.
5. **Agregados y ordenamiento**: `GROUP BY` con hashing externo y `ORDER BY ... LIMIT`.
6. **Insertar, clave repetida, borrar**: el segundo `INSERT` debe fallar (se marca como «rechazado como se esperaba»); `DELETE` por rango en el secuencial y `COUNT(*)`.
7. **Cierre**: `SELECT * ... WHERE Index = k` en las tres tablas.

Referencia con 1 000 registros: B+ agrupado 2 páginas, secuencial 7 (búsqueda binaria), heap 19 (recorre hasta encontrar la clave). Con 100 000: 3, 13 y 1 844.

## 5. Comparar estructuras

Esta pestaña demuestra las cuatro organizaciones con el mismo CSV y las mismas consultas.

1. Elige el CSV (1 000, 10 000 o 100 000 registros) y pulsa **Crear tablas**. Se crean:

   | Tabla | Estructura |
   |---|---|
   | `org_heap` | Heap File |
   | `org_seq` | Secuencial paginado |
   | `org_bplus` | B+ agrupado sobre `Index` |
   | `org_idx` | Heap con índices B+ no agrupados sobre `Index` y `Founded` |

   La tabla muestra registros, páginas, espacio en disco (con el de los índices aparte), tiempo de carga y el estado del archivo (tumbas, área auxiliar, reorganizaciones, altura del árbol). **Volver a crear** borra esas cuatro tablas y las carga de nuevo; no toca las demás.

2. En **Misma consulta en las cuatro tablas** elige un caso, ajusta el valor si quieres y pulsa **Ejecutar en las cuatro**:

   | Caso | Qué muestra |
   |---|---|
   | Búsqueda por clave | Heap recorre páginas; Secuencial usa búsqueda binaria; los dos B+ bajan por el árbol. |
   | Rango por clave | El B+ agrupado lee pocas hojas; el no agrupado lee una página del heap por fila. |
   | Columna no clave | Solo `org_idx` tiene índice sobre `Founded`; las demás recorren todo. |
   | ORDER BY | External merge sort (runs iniciales y pasadas en el plan). |
   | GROUP BY | Agrupación con hashing externo. |
   | INSERT y DELETE | Inserta, busca, elimina y vuelve a buscar. Muestra páginas escritas y eliminación lazy. |

   Arriba de la tabla aparece si las cuatro devolvieron el mismo resultado. **Ver plan** abre el plan y las filas de una tabla. **Abrir en el editor** copia las sentencias a Consultas. **Repeticiones** ejecuta el caso varias veces y muestra la mediana del tiempo.

3. En **B+ agrupado vs B+ no agrupado** pulsa **Medir con N creciente**. Se ejecuta `SELECT COUNT(*) ... WHERE Index BETWEEN 1 AND N` con N de 10 hasta el total, en las cuatro tablas. La gráfica (escala logarítmica) cambia entre páginas leídas y tiempo, y debajo se indica desde qué N el índice no agrupado deja de convenir frente a recorrer todo el heap.

**Exportar CSV** en cada sección descarga las mediciones para el informe.

Referencia con 10 000 registros (páginas leídas, no dependen de la máquina):

| Consulta | Heap | Secuencial | B+ agrupado | B+ no agrupado |
|---|---|---|---|---|
| `Index = 5000` | 185 | 10 | 3 | 3 |
| `Index BETWEEN 1000 AND 1300` | 369 | 131 | 27 | 306 |
| `Founded = 2000` | 369 | 447 | 772 | 207 |

El índice no agrupado deja de convenir entre 250 y 500 filas (2,5 % a 5 % de la tabla).

## 6. Guion corto para la demo

1. **Consultas**: mostrar los cuatro paneles con una tabla cargada.
2. **Demo guiada**: elegir 1k (o 10k) y recorrer el guion con **Ctrl + Enter**, comentando `paginas_leidas` en cada plan.
3. **Comparar estructuras**: crear las tablas con 10 000 registros y comentar el espacio en disco.
4. Ejecutar **Búsqueda por clave** y **Rango por clave**; abrir **Ver plan** del B+ no agrupado (`paginas indice` y `paginas heap`).
5. Ejecutar **Columna no clave** e **INSERT y DELETE**.
6. **Medir con N creciente** y explicar el cruce en la gráfica.

## 7. Mediciones

Lee los CSV de `datos/resultados/` generados por `make bench` (o `mingw32-make bench`). Elige archivo y métrica. Los campos `_us` están en microsegundos y el espacio en bytes.

## 8. Mapa espacial

En **Consultas**, selecciona una tabla que tenga una columna `POINT`. El panel **Mapa espacial** carga sus ubicaciones; los resultados se resaltan y al seleccionar una fila el mapa centra esa ubicación. Las consultas `distancia(columna, POINT(lat, lon)) < radio` dibujan el círculo de búsqueda en metros. Leaflet usa mosaicos de OpenStreetMap, por lo que el fondo del mapa requiere conexión a internet.

## 9. Problemas frecuentes

| Problema | Solución |
|---|---|
| PowerShell bloquea `npm.ps1` | Usa `npm.cmd`. |
| No se encuentra `g++` | Agrega el `bin` de MinGW/MSYS2 al PATH y abre otra terminal. |
| Faltan FastAPI o Uvicorn | `python -m pip install -r api/requirements.txt` desde la raíz. |
| Arriba dice «sin conexión con la API» | Revisa la terminal y espera a que termine de compilar. |
| Puerto 5173 u 8000 ocupado | Cierra la instancia anterior de `npm.cmd run dev`. |
| Tabla o índice ya existe | Usa otro nombre o `DROP TABLE`. |
| SQL no soportado | Revisa la pestaña Sintaxis. `gramar.md` incluye sintaxis que aún no está implementada. |
| `caracter inesperado: '-'` | El motor no acepta comentarios `--`; la API los quita antes de enviarlos, así que solo pasa si llamas al motor directamente (`motor_cli.py`). |

Un lote de sentencias no es una transacción: si una falla, las anteriores no se deshacen.

## 10. Pruebas y archivos

Con la interfaz detenida, desde `web`:

```powershell
npm.cmd run build
npx.cmd playwright install chromium
npm.cmd test
```

Las pruebas usan una base aparte en `.build/web-test-*`. Cubren carga de CSV, consultas, índices, INSERT y DELETE, errores, la comparación de las cuatro estructuras, la demo guiada completa (31 pasos con 1k), la ejecución de la sentencia bajo el cursor y la vista móvil.

```text
web/src/App.tsx          barra superior, archivos y editor
web/src/Results.tsx      resultados y plan
web/src/Demo.tsx         pestaña demo guiada
web/src/demo.ts          guion de la demo (escala con 1k / 10k / 100k)
web/src/sql.ts           separar sentencias, quitar comentarios, sentencia bajo el cursor
web/src/Comparison.tsx   pestaña comparar estructuras
web/src/structures.ts    tablas de prueba, casos y métricas
web/src/RangeChart.tsx   gráfica del barrido
web/src/Experiments.tsx  mediciones de datos/resultados
web/src/Syntax.tsx       referencia de sintaxis
api/web_api.py           API FastAPI que llama al motor
```

Flujo: **React → FastAPI → `api/motor_cli.py` → motor C++ → archivos en disco**.
