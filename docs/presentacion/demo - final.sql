-- Demo de la presentacion final (Partes 3, 4 y 5). El guion esta en "guion - final.md".
-- En la presentacion cada consulta se escribe a mano; este archivo es el respaldo si algo falla.
--
-- Requisitos: las tablas de la tienda y de copyright cargadas (docs/tienda.sql y docs/copyright.sql,
-- o los bloques [1] y [2] de "demo - p5.sql") y .build/subidas/foto.jpg
-- (python docs/presentacion/preparar_demo_p5.py --foto <foto de un taburete>).
--
-- Para probarlo sobre una base temporal:
--     python tools/probar_sql.py "docs/presentacion/demo - final.sql"


-- ===================== PREPARACION (antes de la presentacion) =====================

-- [0] el IVF de la foto, una sola vez: no hay DROP INDEX, asi que la segunda vez dice que ya existe
CREATE INDEX catalogo_ivf ON catalogo (v) USING IVF WITH (nlist = 100, nprobe = 8);
-- espera: ok


-- ===================== PARTE 3: TEXTO (Dayane) =====================

-- [1] bar stool con BM25 sobre el indice SPIMI
SELECT id, nombre, precio, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'bar stool') USING BM25 ORDER BY relevancia DESC LIMIT 5;
-- espera: filas 5

-- [2] la misma consulta con TF-IDF y coseno (lnc.ltc), con la sintaxis del enunciado: solo cambia BM25 por TF_IDF
SELECT id, nombre, precio, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'bar stool') USING TF_IDF ORDER BY relevancia DESC LIMIT 5;
-- espera: filas 5

-- [3] el plan: Text Scan lee solo las listas de las dos palabras (las mismas con TF-IDF y con BM25)
EXPLAIN ANALYZE SELECT id, nombre, precio, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'bar stool') USING TF_IDF ORDER BY relevancia DESC LIMIT 5;
-- espera: contiene "Text Scan"


-- ===================== PARTE 4: IMAGEN (Luis) =====================

-- [4] sin indice: compara la foto con las 10 000 filas
EXPLAIN ANALYZE SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10);
-- espera: contiene "Seq Vector Scan"

-- [5] con HNSW y la sintaxis del enunciado: FAISS da los 10 ids y el motor lee esas filas
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10) USING HNSW WITH METRIC = cosine;
-- espera: faiss "catalogo_hnsw"

-- [6] la misma foto con IVF: revisa 8 de las 100 celdas
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10) USING IVF;
-- espera: faiss "catalogo_ivf"

-- [7] el plan del IVF
EXPLAIN ANALYZE SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10) USING IVF;
-- espera: contiene "nprobe=8"


-- ===================== PARTE 5: COPYRIGHT (Noe) =====================

-- [8] los 5 tramos de blues.00000 con el B+ sobre cancion_id
SELECT id, inicio, fin FROM segmentos WHERE cancion_id = 1;
-- espera: filas 5
-- Despues: vista Copyright, subir datos/audio/remix_demo.wav.
-- La tienda (bloque 3 del guion) se muestra en la vista Tienda, sin consultas a mano.


-- ===================== RESPALDO PARA LAS PREGUNTAS (no se ejecuta en la exposicion) =====================

-- [9] sin indice, la metrica se elige en cada consulta; con norma L2 las tres ordenan igual
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10) WITH METRIC = euclidean;
-- espera: igual 5

-- [10] producto punto (en el motor: producto_punto, inner_product o ip; "dot" no existe)
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10) WITH METRIC = inner_product;
-- espera: igual 5
