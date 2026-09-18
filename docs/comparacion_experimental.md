# 2.1.6 Comparación experimental de técnicas

Análisis comparativo de las técnicas de organización de archivos e indexación
implementadas en el minigestor.

## 1. Metodología

**Datos.** Todas las mediciones usan el conjunto `organizations-N.csv` (N = 1 000,
10 000 y 100 000 registros), el mismo que carga la interfaz. Los registros son de
longitud variable (9 columnas, ~150 bytes de media). La página es de **4 096 bytes**
en todas las estructuras, y el *buffer pool* de los índices tiene **64 marcos** con
política LRU.

**Qué se mide.** Junto al tiempo se reportan las **páginas leídas**, que es la métrica
que no depende de la máquina: el tiempo varía con el disco y la caché del sistema
operativo, el número de páginas no. Las páginas por consulta se miden **enfriando la
caché antes de cada consulta**, porque el ejecutor abre un descriptor nuevo por
sentencia y siempre arranca en frío; los tiempos se miden en caliente, que es el caso
realista cuando se repiten consultas.

**Reproducción.**

```
mingw32-make bench                                  # heap, secuencial y B+ agrupado
mingw32-make .build/indices_bench.exe               # índices secundarios
.build/indices_bench --n 100000 --columna Founded
python datos/resultados/graficas.py                 # figuras
```

Los CSV crudos quedan en `datos/resultados/`.

---

## 2. Gestión de archivos: heap file vs archivo secuencial paginado

Figura: `fig1_gestion_archivos.png`

| Métrica (100 000 registros) | Heap file | Secuencial paginado |
|---|---|---|
| Tiempo de inserción | **1 716 ms** | 9 407 ms |
| Páginas leídas por búsqueda de clave primaria | 1 598 | **35** |
| Espacio en disco | **14 304 KB** | 17 680 KB |
| Tiempo de reorganización | **17.6 ms** | 130.6 ms |

**Inserción.** El heap gana por un factor de 5.5×: escribe siempre al final, sin buscar
posición. El secuencial paga por mantener el orden — durante la carga de 100 000
registros disparó **20 reorganizaciones automáticas** para vaciar el área auxiliar, y
esas reorganizaciones son la mayor parte de su coste.

**Búsqueda por clave primaria.** Aquí se invierte por completo: el heap no tiene ningún
orden, así que una búsqueda es un recorrido completo de 1 598 páginas. El secuencial
hace búsqueda binaria sobre el área principal y solo barre el área auxiliar: 35 páginas,
**46 veces menos**. La diferencia crece con n, porque el heap es O(n) y el secuencial
O(log n).

**Espacio.** El secuencial ocupa un 24 % más por el área auxiliar y por el factor de
llenado de 0.8 que deja hueco en las páginas para absorber inserciones.

**Cuándo conviene cada uno.** El heap es la organización correcta cuando la carga es
masiva y las consultas son recorridos completos o agregados (`COUNT`, `GROUP BY`), donde
el orden no aporta nada. El secuencial conviene cuando hay búsquedas por clave o por
rango frecuentes y las inserciones son moderadas.

---

## 3. Estructuras de indexación

### 3.1 Acceso por clave primaria: las tres organizaciones

Figura: `fig2_busqueda_clave_primaria.png`

| Páginas leídas por búsqueda | n = 1 000 | n = 10 000 | n = 100 000 |
|---|---|---|---|
| Heap file (recorrido completo) | 16 | 158 | 1 598 |
| Secuencial paginado (binaria) | 6 | 20 | 35 |
| **B+ agrupado** | **3** | **4** | **4** |

El B+ agrupado es el único que se mantiene **constante al crecer los datos**: 4 páginas
a 100 000 registros, igual que a 10 000. Es la altura del árbol (3) más la hoja. Ese es
el argumento central a favor de un índice sobre cualquier organización secuencial.

### 3.2 Índices secundarios: B+ no agrupado vs hash extensible

Ambos se construyeron sobre la misma tabla heap y la misma columna, en la misma
ejecución, para que la comparación sea directa. Se probaron dos cardinalidades porque
resultó ser el factor que más influye.

**Alta cardinalidad** — columna `Number of employees`, ~10 filas por clave.
Figura: `fig3_indices_alta_cardinalidad.png`

| Métrica (100 000 registros) | B+ no agrupado | Hash extensible |
|---|---|---|
| Páginas por búsqueda de igualdad | 4.11 | **2.00** |
| Páginas por búsqueda de rango | **4.42** | no soportado |
| Espacio adicional | 2 840 KB | **2 224 KB** |
| Tiempo de construcción | 1 344 ms | **1 104 ms** |

**Baja cardinalidad** — columna `Founded`, ~1 887 filas por clave.
Figura: `fig4_indices_baja_cardinalidad.png`

| Métrica (100 000 registros) | B+ no agrupado | Hash extensible |
|---|---|---|
| Páginas por búsqueda de igualdad | 21.9 | **16.5** |
| Páginas por rango de 5 años (10 685 filas) | **107** | no soportado |
| Espacio adicional | 3 864 KB | **1 648 KB** |
| Tiempo de construcción | 189 ms | **162 ms** |

**Igualdad.** El hash lee **exactamente 2 páginas** —una de directorio y una de bucket—
a 1 000, 10 000 y 100 000 registros. Es O(1) medido, no teórico. El B+ crece con la
altura del árbol: 3 → 3 → 4 páginas. Con claves muy repetidas ambos degradan, porque hay
que leer todas las entradas de la clave, pero el hash sigue por debajo (16.5 contra 21.9).

**Rango.** El hash **no puede resolverlos**: la función de dispersión destruye el orden
de las claves, así que claves consecutivas caen en buckets sin relación. El ejecutor lo
detecta y cae a recorrido completo, dejándolo escrito en el plan:

```
scan_completo  estructura=heap  paginas_leidas=369  nota=el indice hash no resuelve rangos: recorrido completo
```

**Espacio.** El hash ocupa siempre menos, y la ventaja crece con los duplicados: 22 %
menos en alta cardinalidad, **57 % menos** en baja. El B+ guarda claves repetidas en las
páginas internas para poder navegar; el hash solo las almacena en los buckets.

---

## 4. Dos resultados que contradicen la intuición

### 4.1 Un índice secundario puede ser más caro que no tener índice

Medido con el motor SQL sobre una tabla heap de 10 000 filas (369 páginas) con un B+
no agrupado sobre `Founded`:

| Consulta | Filas | Coste con índice | Coste del recorrido completo | Gana |
|---|---|---|---|---|
| `Founded = 2019` | 195 | 3 + 195 = **198 páginas** | 369 páginas | el índice |
| `Founded BETWEEN 2015 AND 2017` | 572 | 6 + 572 = **578 páginas** | 369 páginas | **el recorrido** |

La causa es que un índice **no agrupado** no controla dónde viven las filas: cada fila
que coincide cuesta una lectura aleatoria al heap. El punto de quiebre está cuando el
número de filas del resultado se acerca al número de páginas de la tabla. Por eso un
optimizador real estima la selectividad antes de decidir usar un índice, en vez de usarlo
siempre que exista.

### 4.2 La heurística del planificador de joins elige mal

El proyecto elige entre `hash_join` e `index_nested_loop_join` con la regla *"si hay
índice y la relación externa es menor que la interna, usa index nested loop"*. Midiendo
la misma consulta (1 000 organizaciones × 131 décadas, 1 000 filas de resultado) en los
dos sentidos:

| | `hash_join` | `index_nested_loop_join` |
|---|---|---|
| Páginas del lado izquierdo | 38 | 1 |
| Páginas del lado derecho | 1 | 1 009 |
| **Total** | **39** | **1 010** |
| Tiempo | 10.9 ms | 15.6 ms |

El planificador eligió *index nested loop* (131 ≤ 1 000 y había índice) y resultó **26
veces más caro en páginas**. Es el mismo fenómeno de 4.1: las 131 sondas devuelven ~7.6
filas cada una y cada fila cuesta un acceso aleatorio al heap. Una heurística basada solo
en cardinalidades no puede ver ese coste; haría falta comparar
`páginas(externo) + filas_resultado` contra `páginas(externo) + páginas(interno)`.

---

## 5. Tabla resumen

| Técnica | Ventajas | Desventajas | Usar cuando |
|---|---|---|---|
| **Heap file** | Inserción más rápida (1 716 ms / 100 k); espacio mínimo; reorganización barata | Búsqueda O(n): 1 598 páginas a 100 k; sin orden | Carga masiva; consultas que recorren todo o agregan |
| **Secuencial paginado** | Búsqueda O(log n): 35 páginas a 100 k; rangos eficientes; datos ordenados | Inserción 5.5× más lenta; reorganizaciones periódicas; 24 % más espacio | Búsquedas y rangos por clave frecuentes, inserciones moderadas |
| **B+ agrupado** | Coste constante: 4 páginas a cualquier n; rangos y orden casi gratis | Registros de tamaño fijo (≤ 2 040 B); admite menos registros por página | Tabla con acceso intensivo por clave primaria y por rango |
| **B+ no agrupado** | Único índice secundario con rangos y `ORDER BY`; se mantiene barato al insertar | Una lectura aleatoria por fila resultante; puede perder contra el scan (§4.1); más espacio que el hash | Columna secundaria consultada por rango o con alta selectividad |
| **Hash extensible** | Igualdad en 2 páginas sin importar n; menor espacio (hasta 57 %); construcción más rápida | **No resuelve rangos ni `ORDER BY`**; degrada con claves muy repetidas (cadenas de desborde) | Columna secundaria consultada solo por igualdad exacta |

---

## 6. Conclusiones

1. **No hay una técnica mejor en todo.** La elección depende de la proporción entre
   lecturas y escrituras y de la forma de las consultas. El heap gana en carga; el
   secuencial y el B+ agrupado ganan en consulta.

2. **El índice agrupado es el que más aporta por clave primaria**, y es el único cuyo
   coste no crece con el tamaño de los datos en el rango medido (4 páginas de 1 000 a
   100 000 registros).

3. **Entre índices secundarios, la pregunta decisiva es si habrá consultas por rango.**
   Si las hay, el hash queda descartado sin discusión, por rápido que sea en igualdad. Si
   solo hay igualdad exacta, el hash es preferible: menos páginas por consulta, menos
   espacio y construcción más rápida.

4. **Tener un índice no implica que convenga usarlo.** Un índice no agrupado deja de
   compensar cuando el resultado se acerca en tamaño al número de páginas de la tabla
   (§4.1). La consecuencia de diseño es que el planificador debería estimar selectividad,
   no limitarse a comprobar si el índice existe.

5. **La cardinalidad de la clave manda en los índices secundarios.** Al pasar de ~10 a
   ~1 887 filas por clave, el coste de una búsqueda por igualdad se multiplicó por 5 en
   el B+ (4.1 → 21.9 páginas) y por 8 en el hash (2.0 → 16.5). Indexar una columna de
   baja cardinalidad rinde poco en cualquiera de las dos estructuras.

---

## 7. Limitaciones conocidas

- Los **algoritmos externos** (`external_merge_sort`, `external_hash_aggregate`) simulan
  el particionado en memoria: las *runs* y particiones viven en `std::vector`, no en
  archivos temporales. Las métricas de `initial_runs` y `merge_passes` son reproducibles,
  pero no corresponden a E/S real.
- El **hash extensible no fusiona buckets** al borrar: el espacio se reutiliza mediante
  una lista de páginas libres, pero el directorio nunca se reduce.
- El **JOIN** admite solo `INNER`, uno por consulta y con una sola igualdad en el `ON`.
- Los índices secundarios solo se admiten **sobre tablas heap** y **columnas `INT`**.
- La construcción del hash con claves muy repetidas es sensible a cómo se inserta: la
  versión inicial recorría toda la cadena de desborde en cada inserción y tardaba
  5 917 ms en indexar `Founded` sobre 100 000 filas. Insertando siempre en la página
  cabeza y omitiendo la comprobación de duplicado —innecesaria porque el RID ya es
  único— pasó a **162 ms**, 36 veces más rápido, con la misma estructura resultante
  (figura `fig5_costo_dedupe.png`).
