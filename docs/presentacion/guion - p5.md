# Guion de la demo final: Parte 5 y pendientes de la Parte 1

Cada bloque `[n]` es de [`demo - p5.sql`](demo%20-%20p5.sql). En la vista **Consultas** se
selecciona el bloque y se pulsa **Ejecutar** (o `Shift + Enter` sobre una sentencia). Cada uno
presenta lo suyo; el nombre va al inicio de cada parte.

| Parte | Quién | Tiempo |
|---|---|---|
| 0. Las dos apps sobre el mismo motor | Luis | 1 min |
| 1. App de la tienda | Dayane y Luis | 3 min |
| 2. INNER JOIN y algoritmos externos | Luis, Dayane y Noé | 3 min |
| 3. Texto, espacio e imagen (`SIMILAR_TO` a mano) | Dayane, Luis y Noé | 2 min |
| 4. App de copyright | Noé y Dayane | 3 min |
| 5. Cierre | Luis | 30 s |

## Antes de empezar (una sola vez, en la laptop de la demo)

1. Datos que deben estar: fotos de ABO en `C:\datos_bd2\imagenes` (o `BD2_DATOS`), los audios
   de GTZAN, `datos/productos/vocabulario_256.npy` e `idf_256.npy`, y `datos/audio/segmentos.csv`
   y `canciones.csv` (si faltan: `cd api && python -m multimedia.segmentos_audio --hop 5 --duracion 10`).
2. `python docs/presentacion/preparar_demo_p5.py --foto <foto de un taburete o una silla>`:
   deja `datos/audio/remix_demo.wav` y `.build/subidas/foto.jpg`. Usar una foto que no sea del
   dataset, por ejemplo una tomada con el celular.
3. Abrir la web con `npm run dev` (desde `web/`) y ejecutar los bloques **[1]** y **[2]**, que
   cargan la tienda y la base de copyright (unos 20 s). Si las tablas ya existen, saltarlos.
4. Ensayar una vez [10] escribiéndolo a mano.

---

## 0. Las dos apps sobre el mismo motor (Luis)

**Qué digo:**
> Para la Parte 5 hicimos dos aplicaciones del Anexo A: la **B**, una tienda con recomendaciones,
> y la **C**, un detector de copyright en audio. Elegimos la B porque usa los cuatro tipos de
> datos del motor en una sola pantalla, y la C porque reutiliza los MFCC y el HNSW de la Parte 4
> y le agrega lo que faltaba: decidir con un umbral si una coincidencia es real.
>
> Las dos son clientes de la misma API. No leen los archivos del motor: arman SQL y lo mandan
> por el mismo camino que la vista Consultas. Por eso todo lo que vamos a ver en las apps se
> puede escribir a mano, y cada respuesta trae las consultas que usó.

---

## 1. App de la tienda (Dayane y Luis)

**Dayane, en la vista Tienda:**
1. Busca *bar stool*: salen las tarjetas con foto, marca y precio. "Esto es `MATCH(descripcion,
   'bar stool') USING BM25` sobre el índice SPIMI."
2. Abre el producto **5**, el taburete Rivet Malida: ficha con foto, precio y descripción, leída
   por `id` en el B+ agrupado.
3. Baja al mapa: las 3 sedes más cercanas a UTEC con stock, numeradas como el k-NN de la Parte 2.
   Hace clic en otro punto del mapa y cambian. "Primero el B+ de inventario dice en qué sedes hay
   stock, y luego el R-Tree ordena esas sedes por distancia."

**Luis, en las recomendaciones del mismo producto:**
> El recomendador junta cuatro listas de 20: **texto** (BM25 con el nombre), **imagen** (HNSW
> con el vector de la foto), **metadata** (mismo tipo y misma marca o color) y **compras**
> (quienes compraron este producto, qué más compraron). Los scores no se pueden sumar: BM25 no
> tiene máximo, el coseno va de 0 a 1 y las compras son conteos. Por eso usamos **Reciprocal
> Rank Fusion**: cada producto suma 1 / (60 + puesto) por cada lista en la que aparece.

- Abre "Por qué aparece" en el **primero** (2142, el mismo modelo en gris): está en texto,
  imagen y metadata, puestos 1, 3 y 2, y suma 1/61 + 1/63 + 1/62 ≈ 0,048.
> Un producto que fuera primero en una sola lista sumaría 1/61 ≈ 0,016, menos que el décimo.
> RRF premia que las fuentes estén de acuerdo. Tarda medio segundo porque son cuatro consultas.

---

## 2. INNER JOIN y algoritmos externos (Luis, Dayane y Noé)

**Luis. Ejecuto [3]:** las 3 sedes más cercanas al centro con stock del producto 42.

**Qué se ve:** solo dos filas, **Tienda Breña** y **Tienda Ate**, porque solo dos sedes tienen
stock.

> El `WHERE` se reparte por tabla y se aplica antes del join: `producto_id = 42` usa el B+ de
> inventario, así que al join solo llegan 2 filas.

**Ejecuto [4]:** el `EXPLAIN` dice **Hash Join on inventario JOIN sedes**.
> `sedes` no tiene índice sobre `id`, así que se construye una tabla hash con el lado más chico.

**Ejecuto [5]:** "quienes compraron el producto 1 también compraron…". El plan dice **Index
Nested Loop Join on compras JOIN compras**.
> Aquí la tabla interna tiene un índice HASH sobre `usuario_id`, y por cada usuario de afuera se
> hace una sola búsqueda. El planificador elige entre los dos según los índices y el tamaño.

**Dayane. Ejecuto [6]:** `GROUP BY tipo` con `EXPLAIN ANALYZE`.

**Qué se ve:** `HashAggregate` con **328 grupos**, `particiones=9`, `niveles=1` y unas 87
páginas escritas y leídas.
> El `GROUP BY` tiene 10 páginas de memoria. Cada fila va a la partición hash(tipo) mod 9, que
> es un archivo temporal de páginas de 4 KB. Luego se agrega una partición a la vez: todas las
> filas de un grupo caen en la misma, así que cada grupo se completa ahí.

**Noé. Ejecuto [7]:** `ORDER BY precio DESC` con `EXPLAIN ANALYZE`.

**Qué se ve:** `Sort` con **initial_runs=200**, **merge_passes=3** y unas 8 000 páginas escritas
y leídas.
> Con 10 páginas de memoria se arman corridas ordenadas que se escriben a disco, y luego se
> mezclan de a 9 con un heap de mínimos. Es la fórmula de clase: 2N por pasada. Por eso escribe
> y lee el mismo número de páginas.

---

## 3. Texto, espacio e imagen (Dayane, Luis y Noé)

**Dayane. Ejecuto [8]:** *bar stool* con BM25. La relevancia sale con barra y las palabras
resaltadas; los 5 son taburetes con su precio.

**Luis. Ejecuto [9]:** las 3 sedes más cercanas a UTEC con el R-Tree: **Barranco**,
**Miraflores** y **Surquillo**. Se ven numeradas en el mapa.

**Noé. Escribo [10] a mano**, en una línea nueva del editor:

```sql
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10) USING HNSW;
```

> En el Avance 3 no supimos escribir esta consulta; ahora la vista **Sintaxis** tiene la
> estructura de cada una. `foto.jpg` es una foto que no está en el dataset. La API le saca el
> vector igual que a las fotos del catálogo (SIFT, 256 palabras visuales y TF-IDF), FAISS busca
> los 10 vecinos en el HNSW y el motor lee esas filas.

**Qué se ve:** la galería con los 10 productos más parecidos y su similitud.

---

## 4. App de copyright (Noé y Dayane)

**Noé:**
> La base guarda un vector por cada **tramo de 10 s**, uno cada 5 s: 999 canciones de GTZAN,
> 4 995 tramos. Así un fragmento corto se compara con algo de su tamaño. Cada tramo sale de los
> MFCC: la señal a 22 050 Hz se corta en ventanas de 93 ms, a cada una se le saca el espectro con
> la FFT, se agrupa en 128 bandas de la escala mel, se pasa a decibeles y la DCT deja 13
> coeficientes. Antes de cuantizar se normalizan por tramo, para que subir el volumen no cambie
> el vector.

**Ejecuto [11]:** los 5 tramos de `blues.00000` con el B+: 0-10, 5-15, 10-20, 15-25 y 20-30.

**Dayane, en la vista Copyright:** sube `datos/audio/remix_demo.wav` (40 s: 20 s de una canción
clásica y después un blues a la mitad del volumen).

**Qué se ve** (tarda de 6 a 8 s):
- Veredicto **Posible uso no autorizado**, con umbral de 74 % en al menos 2 tramos.
- La línea de tiempo: los tramos del inicio en un color (`classical.00011`), los del final en
  otro (`blues.00000`) y el tramo de la frontera en gris, porque mezcla las dos y no pasa el
  umbral.
- Las dos canciones con 3 tramos cada una, el 42,9 % de los tramos, similitud máxima 100 %. Clic
  en "Escuchar original" y en un tramo para reproducir desde ahí.

**Dayane:**
> Para pasar de las ventanas a un vector se usa la **cuantización**: K-Means aprende 256
> palabras acústicas, cada ventana se cambia por su palabra más cercana, se cuenta cuántas veces
> sale cada una y se aplica TF-IDF. Por eso el blues a la mitad del volumen da 100 %: la
> normalización quitó el cambio de volumen.

**Noé:**
> El umbral de 74 % no es a ojo: armamos 150 casos, 100 copias (recortes, volumen, ruido,
> velocidad y mezclas) y 50 que no debían coincidir, y 74 % dio el mejor F1, 0,865, con precisión
> 0,90 y recall 0,83. Lo que más falla son las mezclas de dos canciones: solo 5 de 15.

---

## 5. Cierre (Luis)

> Las dos apps usan todo lo que construimos: páginas de 4 KB, B+, hash, R-Tree, SPIMI, HNSW, el
> join y los algoritmos externos. Lo que más aprendimos es que no hay un índice mejor en
> general: cada uno conviene para un tipo de consulta, y la forma de saber cuál es medir.

## Preguntas probables

| Pregunta | Quién | Respuesta corta |
|---|---|---|
| ¿Cómo se calcula un MFCC? | los tres | Ventanas de 93 ms con Hann, FFT, 128 filtros mel, decibeles, DCT y 13 coeficientes (informe, Parte 4). |
| ¿Qué es la cuantización? | los tres | K-Means de 256 palabras, asignar cada ventana a su centroide, histograma y TF-IDF con norma L2 (informe, 9.9). |
| ¿Por qué RRF y no sumar scores? | Luis | Escalas distintas; RRF solo usa puestos y no necesita entrenar pesos. |
| ¿Cuándo hash join y cuándo index nested loop? | Luis | INLJ si la interna tiene índice sobre la columna del ON; si no, hash join con el lado más chico. |
| ¿Cuántas E/S hace el sort? | Noé | 2N(1 + pasadas), con pasadas = ⌈log_{B-1}(N/B)⌉. |
| ¿Qué pasa si un grupo no cabe en memoria? | Dayane | Esa partición se vuelve a particionar con otra semilla, hasta 3 niveles. |
| ¿Por qué 74 %? | Noé | Mayor F1 en la evaluación con 150 casos. |
| ¿Por qué falla con mezclas? | Noé | El histograma junta las palabras de las dos canciones y ninguna pasa el umbral. |
