# B+ agrupado frente a B+ no agrupado

Este documento responde a la observación de que *"no tiene sentido que el
agrupado tenga tiempo mayor que el no agrupado"*. La respuesta corta: la
construcción del árbol está bien, y el agrupado gana en todo menos en los
recorridos completos. Donde pierde, el motivo no es el árbol sino el formato de
registro.

Las mediciones se reproducen con:

```bash
make bench-agrupado          # o: python3 api/bench_agrupado.py
```

Salen a `datos/resultados/bplus_agrupado_vs_no_agrupado.csv`. Cada tiempo es la
mediana de 3 repeticiones sobre `datos/organizations-100000.csv`, con el mismo
CSV cargado en las dos estructuras.

## Qué se compara

| | B+ agrupado | heap + B+ no agrupado |
|---|---|---|
| Organización | los registros viven en las hojas del árbol, ordenados por la clave | los registros viven en un heap; el árbol guarda solo `(clave → página, slot)` |
| Archivos | uno (`.bpa`) | dos (`.heap` y `.bplus`) |
| Registro | tamaño fijo: los `VARCHAR` se rellenan hasta su ancho máximo | tamaño variable: cada `VARCHAR` ocupa lo que mide |

## Resultados con 100 000 filas

| Operación | B+ agrupado | heap + B+ no agrupado | Quién gana |
|---|---|---|---|
| Construcción | **349 ms** | 396 ms | agrupado, 1,1× |
| Búsqueda puntual | **0,26 ms** · 5 págs | 5,6 ms · 4 págs | agrupado, 22× |
| Rango del 3 % | **9,0 ms** · 237 págs | 16,7 ms · 3 033 págs | agrupado, 1,9× |
| Recorrido completo | 166 ms · 7 698 págs | **157 ms** · 3 709 págs | no agrupado, 6 % |
| `ORDER BY` sobre texto | 631 ms · 7 698 págs | **624 ms** · 3 709 págs | no agrupado, 1 % |
| Tamaño en disco | 31,6 MB | 15,2 MB | no agrupado, 2,1× |

> Estas cifras son posteriores a darle carga masiva al heap y al secuencial (ver
> [carga_masiva.md](carga_masiva.md)). Antes el heap tardaba 753 ms en cargar por
> un defecto de E/S, y eso exageraba la ventaja del agrupado en construcción:
> salía 2,2× en vez del 1,1× real.

## La construcción está bien

El agrupado se construye con **carga masiva de abajo hacia arriba**: ordena las
filas por la clave, llena las hojas al 90 % y va creando los niveles internos
hasta quedarse con una raíz. No hace una inserción por fila, así que no hay
divisiones de nodo ni reescrituras.

Aun así gana poco: la alternativa necesita dos pasadas (una para llenar el heap
y otra para recorrerlo insertando una entrada por registro en el árbol), pero el
heap también escribe cada página una sola vez desde que tiene carga masiva. Lo
que domina el tiempo en los dos casos es leer y convertir el CSV.

Si al medir la construcción el agrupado sale más lento, revisa que el tiempo del
no agrupado incluya **también** el `CREATE INDEX`. Medir solo el `CREATE TABLE`
del heap deja fuera la mitad del trabajo. En la pestaña *Comparar estructuras*
la columna *Carga (ms)* de `org_idx` ya suma la tabla más sus dos índices.

## Por qué pierde en los recorridos

El B+ agrupado guarda **registros de tamaño fijo** porque cada hoja localiza sus
registros por posición, lo que permite buscar dentro de la página con búsqueda
binaria. El precio es que un `VARCHAR(51)` ocupa 51 bytes aunque el valor mida 8.

Con este CSV la suma de anchos máximos da 316 bytes por registro, frente a los
151 que ocupa de media en el heap. De ahí sale todo lo demás:

- El archivo pesa 2,1× más.
- Caben 13 registros por página en vez de 27.
- **Cualquier operación que lea la tabla entera lee 2,1× más páginas.**

Que leyendo el doble de páginas solo tarde un 6 % más indica que, a este tamaño,
el costo lo domina desempaquetar las filas y no la E/S: el sistema operativo
mantiene los dos archivos en memoria caché. La desventaja se notaría de verdad
con datos que no quepan en RAM.

## Conclusión

El agrupado hace exactamente lo que promete: evita el salto del índice al dato.
Por eso una búsqueda puntual le cuesta 0,26 ms frente a 5,6 ms, y un rango del
3 % le cuesta 237 páginas en lugar de 3 033, porque el no agrupado lee **una
página de datos por cada fila encontrada** y esas páginas están desperdigadas.

El agrupado solo pierde cuando la consulta no aprovecha el orden de la clave y
hay que leerlo todo. Eso no es un fallo del árbol, es el costo del registro de
tamaño fijo.

Para quitarle esa desventaja habría que guardar registros de tamaño variable en
las hojas, con una directorio de slots dentro de la página como el que ya usa
[`pagina_slotted`](../motor/archivos/pagina_slotted.h) para el heap. Queda
pendiente: cambia el formato de página del `.bpa` y obliga a rehacer la búsqueda
dentro de la hoja.

## Cómo verlo en la interfaz

En *Comparar estructuras*, el caso **Rango por clave** lo muestra de un vistazo:
las gráficas de páginas y de tiempo dejan a `org_bplus` muy por debajo de
`org_idx`. El caso **ORDER BY** muestra lo contrario, y la columna *Espacio en
disco* de la tabla de arriba explica por qué.

También se puede pedir el plan directamente:

```sql
EXPLAIN SELECT Index, Name FROM org_bplus WHERE Index BETWEEN 10000 AND 13000;
EXPLAIN SELECT Index, Name FROM org_idx   WHERE Index BETWEEN 10000 AND 13000;
```

El primero estima 234 páginas y el segundo 3 004: el planificador ya sabe que el
índice no agrupado paga una página por fila.
