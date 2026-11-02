# Búsqueda de texto: SPIMI, TF-IDF y BM25

## Preprocesamiento

`motor/texto/preprocesamiento.h` implementa `tokenizar(texto)` para el dataset ABO en
inglés:

1. Convierte a minúsculas y translitera vocales latinas acentuadas, `ñ` y `ç`.
2. Usa los signos y cualquier carácter no ASCII restante como separadores.
3. Quita las palabras de `datos/stopwords_en.txt` (una por línea).
4. Aplica el algoritmo clásico de stemming de Porter (1980), pasos 1a–5b.

La lista de stopwords se carga una vez por proceso. Por defecto se busca desde la raíz del
repositorio; `BD2_STOPWORDS_EN` permite indicar otra ruta. Un archivo ausente es un error
explícito, no una lista vacía. Ejemplo: `The CARS are running, fast!` produce
`car run fast`.

## Construcción SPIMI

```cpp
motor::texto::IndiceInvertido indice(".build/indice_demo", 4 * 1024 * 1024);
indice.agregar(0, "running shoes");
indice.agregar(1, "waterproof running shoes");
indice.construir();
```

Cada bloque tiene su propio `unordered_map<string, vector<Posting>>`; no hay asignación
global de termID. Se cuentan términos por documento antes de agregarlos y cada posting se
añade al final, en orden creciente de `doc_id`. `vector` administra el crecimiento de las
listas. El estimador del bloque incluye cadenas, postings y una aproximación del overhead
de la tabla hash; se vacía antes o después de añadir un documento al cruzar el límite. Un
documento individual más grande que el límite se mantiene entero para no fragmentar sus
postings.

Al vaciar un bloque, se ordenan sus términos y se escribe un archivo binario temporal.
`construir()` hace un merge k-way de los archivos ordenados y escribe `indice_spimi.bin`.
El directorio final en memoria contiene término → offset/df (no termIDs); `postings(t)`
lee desde ese offset y devuelve la lista en orden de `doc_id`. Los bloques temporales se
eliminan al terminar el merge.

`agregar` exige IDs estrictamente crecientes y `construir` cierra la carga. Los metadatos
de largo de documento y norma lnc se mantienen para calcular ranking. Las estadísticas
`paginas_leidas()` y `tamano_en_disco()` reflejan lecturas de postings y el archivo final,
respectivamente.

## Ranking

- **TF-IDF + coseno (lnc.ltc):** peso del documento `1 + log10(tf)`; peso de consulta
  `(1 + log10(tf_query)) * log10(N / df)`. Ambos vectores se normalizan antes de comparar.
- **BM25:** `idf = ln((N - df + 0.5) / (df + 0.5) + 1)`, con `k1=1.2` y `b=0.75`.
  La normalización de longitud usa el tamaño del documento y el promedio de la colección.

`buscar(indice, consulta, k, Ranking::TF_IDF|Ranking::BM25)` retorna solo scores positivos,
ordenados de mayor a menor; los empates se rompen por `doc_id`.

## SQL

```sql
CREATE INDEX idx_contenido ON documentos (contenido) USING SPIMI;

SELECT * FROM documentos
WHERE MATCH(contenido, 'base de datos vectorial')
USING TF_IDF LIMIT 10;

SELECT *, SCORE() AS relevancia FROM articulos
WHERE MATCH(texto, 'machine learning')
USING BM25
ORDER BY relevancia DESC;
```

`CREATE INDEX ... USING SPIMI` construye el índice invertido de una columna `VARCHAR` y lo guarda
en el directorio `<tabla>__<indice>.spimi`. Ahí quedan `indice_spimi.bin` (postings) e
`indice_meta.bin` (diccionario, largos y normas), así que el índice se puede reabrir sin
volver a construirlo. `MATCH` usa ese índice (vía `Text Scan` del planificador): rankea con
`buscar` y después busca cada fila por su posición. Si la columna no tiene índice SPIMI,
`MATCH` falla con un error que dice cómo crearlo. Las demás condiciones del `WHERE` se
aplican como filtro sobre las filas ya rankeadas.

Con `LIMIT`, sin otros filtros y ordenando por `SCORE()` descendente (o sin `ORDER BY`), el
ranking solo pide los `k` mejores documentos.

### Qué pasa con INSERT y DELETE

El índice no se actualiza fila a fila: un `INSERT` o `DELETE` lo invalida (borra sus
archivos) y se reconstruye en la siguiente búsqueda con `MATCH`, que lo indica en el plan.
Así varias inserciones seguidas cuestan una sola reconstrucción.

### EXPLAIN ANALYZE

```
Text Scan using idx_contenido on documentos (contenido)
  Index Cond: MATCH(contenido, 'machine learning') USING BM25
```

Con los detalles `ranking`, `postings_leidos`, `paginas_indice`, las páginas de datos leídas,
las filas y el tiempo.

## Pruebas

Desde la raíz, en Windows:

```powershell
mingw32-make .build/texto_test.exe .build/sql_test.exe
.\.build\texto_test.exe
.\.build\sql_test.exe
```

`texto_test` fuerza múltiples bloques con un límite de 256 bytes y compara los postings y
scores contra cálculos en memoria para consultas de 1, 3 y 5 o más palabras. `sql_test`
verifica parser y búsquedas SQL con las dos técnicas.

## Comparación experimental pendiente

La comparación de 1k/10k/100k documentos, longitudes de consulta 1/3/5+, juicio de
relevancia y PostgreSQL GIN no se ejecuta todavía desde este cambio. Requiere un benchmark
dedicado que controle el servidor PostgreSQL y un conjunto de relevancia anotado; los tests
unitarios de ranking no sustituyen esas mediciones.
