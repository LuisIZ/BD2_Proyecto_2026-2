# Sequential File Paginado (en disco)

## 1. Objetivo

Organización de archivo que mantiene los registros **ordenados por clave en disco**,
en páginas de 4096 B, con:

- inserción manteniendo el orden (y área auxiliar cuando la página no tiene sitio),
- eliminación lazy con tumbas,
- reorganización cuando el desperdicio supera un umbral (30 % por defecto).

| Archivo | Qué es |
|---|---|
| `motor/comun/archivo.h` | `IFileOrganization` y `Registro`, compartidos con el heap. |
| `motor/archivos/sequential_file.h` | API pública de `SequentialFile`. |
| `motor/archivos/sequential_file.cpp` | Página ordenada, I/O, búsqueda, inserción, tumbas y reorganización. |
| `motor/pruebas/sequential_file_test.cpp` | 10 pruebas: orden en disco, auxiliar, tumbas, reorganización, persistencia, rango, costo de I/O. |
| `motor/pruebas/sequential_file_bench.cpp` | Bench con las mismas columnas que `heap_file_bench.cpp`. |

## 2. Qué hay en disco y qué en RAM

Un solo archivo. La página `p` empieza en el byte `(p + 1) × 4096`, igual que el heap.

```
página 0                      cabecera: magic "SEQP", version, page_size, num_paginas,
                              paginas_principal, umbral, factor_llenado, estadísticas de reorganización
páginas [0, P)                ÁREA PRINCIPAL: ordenadas por clave dentro y entre páginas
páginas [P, num_paginas)      ÁREA AUXILIAR: registros en orden de llegada
```

En RAM solo viven **dos buffers de una página** (uno para operar, otro para el
auxiliar durante la reorganización) y contadores (`registros_vivos`, `tumbas`,
`registros_auxiliares`, `bytes_muertos`). Los contadores no se escriben en cada
operación: al abrir se reconstruyen leyendo la cabecera de 12 B de cada página,
como hace el heap con su mapa de espacio libre.

## 3. Layout de una página

```
 0  u16 slot_count      4  u16 live_count     8  u32 reservado
 2  u16 free_ptr        6  u16 dead_bytes    12  directorio: (offset u16, largo u16) × slot_count
                                                 ...
free_ptr                datos, crecen desde el final de la página hacia arriba
```

- El **directorio está en orden de clave**. Insertar en la posición `i` desplaza
  las entradas `[i, n)` una posición (`memmove` de 4 B por slot) y copia los bytes
  del registro en `free_ptr − largo`.
- Una **tumba** es un slot con el bit alto de `largo` encendido. Conserva `offset` y
  `largo`: su clave sigue leyéndose (sirve de frontera para la búsqueda binaria) y
  su espacio cuenta en `dead_bytes` hasta que se reorganiza.
- El registro se codifica exactamente igual que en el heap:
  `[int32 clave][uint32 largo_valor][valor]`. Así el espacio en disco de ambos es
  comparable byte a byte. Valor máximo: 4072 B.

## 4. Búsqueda: `buscar(clave)`, `buscar_rango(desde, hasta)`

1. **Búsqueda binaria sobre las páginas principales** leyendo solo la primera clave
   de cada página probada: `⌈log₂ P⌉` lecturas. Se elige la última página cuya
   primera clave es `≤ clave`.
2. Búsqueda binaria dentro de la página (en RAM, ya está leída).
3. Si no está, **recorrido lineal del auxiliar**. Este es el costo que la
   reorganización mantiene acotado.

Con 20 000 registros (138 páginas) la prueba mide 8.2 lecturas por búsqueda.
El rango avanza por las páginas principales desde la de `desde` y fusiona con los
auxiliares que caen dentro; `scan()` es el rango `[INT_MIN, INT_MAX]`.

## 5. Inserción manteniendo el orden: `insertar(registro)`

1. Primer registro: se crea la página principal 0.
2. Búsqueda binaria de la página destino `p`.
3. Si `p` tiene sitio (`espacio_contiguo ≥ largo + 4`): se inserta en su posición
   ordenada (después de las claves iguales, para conservar el orden de llegada) y
   se escribe la página. **1 escritura.**
4. Si no: va a la **última página auxiliar** o a una nueva. **1 escritura**, más la
   cabecera si el archivo creció.
5. Se comprueba si toca reorganizar.

Como en el heap, no se rechazan claves repetidas: la unicidad la garantiza el índice.

## 6. Eliminación lazy: `eliminar(clave)`

Localiza el registro (principal o auxiliar), marca el slot como tumba, actualiza
`live_count` y `dead_bytes` de la página y la escribe. El archivo no cambia de
tamaño y la clave de la tumba sigue en su sitio. `buscar`, `scan` y `buscar_rango`
saltan las tumbas. Reinsertar una clave borrada la coloca junto a su tumba.

## 7. Reorganización: `reorganizar()`

Se dispara sola cuando

```
desperdicio = (tumbas + registros_auxiliares) / (registros_vivos + tumbas) > umbral
```

Las tumbas son espacio muerto; los auxiliares son registros fuera de sitio que
encarecen cada búsqueda. Ambos se arreglan con la misma operación. La condición es
estricta: 30 tumbas sobre 100 registros no reorganiza, 31 sí.

Pasos:

1. Un pase por el auxiliar recogiendo `(clave, página, slot)` de los vivos — 12 B
   por registro en RAM — y ordenándolos por clave.
2. Se abre `ruta.reorg` y se hace un **merge en streaming**: el principal se lee
   página a página (ya está ordenado) y cada entrada del auxiliar se lee de su
   página cuando le toca. Las tumbas no se copian.
3. Cada página nueva se llena hasta `factor_llenado × 4084 B` (80 % por defecto)
   y se vuelca. El 20 % libre es para que las inserciones siguientes entren en su
   página sin ir al auxiliar. Con `factor_llenado = 1.0` el archivo queda más chico
   pero cualquier inserción en medio cae al auxiliar.
4. Se cierra el original, se borra y se renombra el temporal. Todo pasa a ser
   principal; tumbas y auxiliar quedan en cero. Se mide el tiempo.

## 8. Estadísticas

`stats()` devuelve `EstadisticasArchivo` (interfaz común). `stats_secuencial()`
añade `paginas_principal`, `paginas_auxiliares`, `bytes_desperdiciados`,
`tamano_archivo_bytes`, `reorganizaciones`, `tiempo_reorganizaciones_us`.
`paginas_leidas()` / `paginas_escritas()` / `reiniciar_contadores()` cuentan I/O
como en el heap. `verificar_orden()` recorre el archivo y comprueba que el
principal esté ordenado y cada página sea consistente.

## 9. Compilar y correr

Desde la raíz del repo.

```bash
mkdir -p .build datos/resultados
g++ -std=c++17 -Wall -Wextra -pedantic -O2 \
  motor/archivos/sequential_file.cpp motor/pruebas/sequential_file_test.cpp \
  -o .build/sequential_file_test
./.build/sequential_file_test

g++ -std=c++17 -Wall -Wextra -pedantic -O2 \
  motor/archivos/sequential_file.cpp motor/pruebas/cargador_csv.cpp motor/pruebas/sequential_file_bench.cpp \
  -o .build/sequential_file_bench
for n in 1000 10000 100000; do
  ./.build/sequential_file_bench --n $n --salida datos/resultados/sequential_bench.csv
  ./.build/sequential_file_bench --n $n --barajar --salida datos/resultados/sequential_bench.csv
done
```

```powershell
New-Item -ItemType Directory -Force .build, datos/resultados | Out-Null
g++ -std=c++17 -Wall -Wextra -pedantic -O2 `
  motor/archivos/sequential_file.cpp motor/pruebas/sequential_file_test.cpp `
  -o .build/sequential_file_test.exe
.\.build\sequential_file_test.exe

g++ -std=c++17 -Wall -Wextra -pedantic -O2 `
  motor/archivos/sequential_file.cpp motor/pruebas/cargador_csv.cpp motor/pruebas/sequential_file_bench.cpp `
  -o .build/sequential_file_bench.exe
foreach ($n in 1000, 10000, 100000) {
  .\.build\sequential_file_bench.exe --n $n --salida datos/resultados/sequential_bench.csv
  .\.build\sequential_file_bench.exe --n $n --barajar --salida datos/resultados/sequential_bench.csv
}
```

Las columnas del CSV son las mismas que las de `heap_bench.csv` (más
`reorganizaciones_auto`, `t_reorganizaciones_auto_us`, `paginas_principal`,
`registros_auxiliares`, `umbral`, `factor_llenado` al final), así que se pueden
concatenar y graficar juntas.

## 10. Heap vs Secuencial, 100 000 organizaciones, misma máquina

| | Heap | Secuencial (ordenado) | Secuencial (barajado) |
|---|---:|---:|---:|
| Inserción | 662 ms | 3018 ms (20 reorg. automáticas, 113 ms) | 3380 ms (13 reorg., 125 ms) |
| Búsqueda por clave | 2616 µs / 1598 pág | **56 µs / 35 pág** | **182 µs / 115 pág** |
| Búsqueda fallida | 5854 µs | 585 µs | 1382 µs |
| Scan completo | 6 ms | 44 ms | 51 ms |
| Reorganizar tras borrar 30 % | 10 ms | 32 ms | 80 ms |
| Disco | 14.6 MB | 18.1 MB (factor 0.8) | 14.7 MB |

Lectura: el heap inserta en O(1) escrituras y gana en carga y scan; el secuencial
paga `log₂ P` lecturas por inserción y las reorganizaciones, y a cambio responde
búsquedas por clave y por rango 15–50× más rápido sin necesitar índice. El auxiliar
es lo que degrada la búsqueda (115 páginas con llegada aleatoria vs 35 ordenada);
el umbral controla ese costo a cambio de reorganizar más seguido.
