# Carga inicial: por qué heap y secuencial tardaban de más

Responde a la observación *"revisar el tiempo de Heap File vs Sequential en la
comparación"*. Al cargar un CSV, los tiempos no cuadraban: el secuencial tardaba
más del triple que el heap, y el heap más del doble que el B+ agrupado, aunque es
la estructura más simple de las tres.

La causa era la misma en los dos casos: **la carga inicial insertaba fila por
fila**, usando el mismo camino que un `INSERT` suelto. El B+ agrupado ya tenía
carga masiva y por eso era el único que salía bien.

## Antes y después

Carga de `datos/organizations-100000.csv`, 100 000 filas:

| Organización | Antes | Después | Mejora |
|---|---|---|---|
| Heap | 753 ms | **320 ms** | 2,4× |
| Secuencial | 3 050 ms | **306 ms** | 10,0× |
| B+ agrupado | 329 ms | 311 ms | ya usaba carga masiva |

Ahora las tres tardan prácticamente lo mismo, que es lo esperable: el tiempo lo
domina leer y convertir el CSV, no escribir el archivo.

## Qué estaba mal

### Secuencial: todo acababa en el área auxiliar

`insertar()` coloca el registro en la página que le toca por clave. Si esa página
está llena, el registro cae al **área auxiliar**, y cuando el auxiliar supera el
umbral se dispara una **reorganización**, que reescribe el archivo entero.

Cargando 10 000 filas así, el archivo quedaba con 107 páginas auxiliares y **13
reorganizaciones**, es decir, se reescribió 13 veces durante una carga que debía
ser de una sola pasada.

`SequentialFile::cargar_masivo()` ordena las filas por clave y escribe el área
principal de una vez, llenando cada página al factor de llenado. Resultado:
`aux=0`, `reorganizaciones=0`.

### Heap: una lectura y una escritura de página por fila

`insertar_bytes()` lee la página destino, inserta y la vuelve a escribir. Es lo
correcto para un `INSERT` suelto, pero en una carga de 100 000 filas significa
mover unos **800 MB de E/S para un archivo de 15 MB**, porque la misma página se
relee y reescribe una vez por cada registro que recibe.

`HeapFile::cargar_masivo()` llena la página en memoria y la escribe una sola vez,
enlazando `next_page` en el mismo paso para no tener que releerla.

## Lo que no cambió

- `INSERT` sigue yendo por el camino de siempre. La carga masiva solo se usa
  cuando el archivo está vacío, que es cuando se puede.
- Las dos funciones exigen el archivo vacío y fallan si no lo está.
- El heap ocupa 3 709 páginas en vez de 3 687: al llenar en orden no se
  reaprovechan los huecos que dejaba el recorrido circular. Es un 0,6 % más de
  espacio a cambio de ser 2,4 veces más rápido.

## Cómo comprobarlo

```sql
CREATE TABLE t FROM FILE 'datos/organizations-10000.csv' USING SEQUENTIAL;
SHOW TABLES;   -- la columna detalle debe decir aux=0 reorganizaciones=0
```

En la pestaña *Comparar estructuras*, la columna *Carga (ms)* de las cuatro
tablas debe quedar en el mismo orden de magnitud. Si el secuencial se dispara,
es que volvió a cargarse fila por fila.
