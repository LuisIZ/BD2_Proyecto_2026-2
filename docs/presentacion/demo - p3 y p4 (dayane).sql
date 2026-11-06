-- Demo de la presentacion, partes de Dayane: dataset ABO, comparacion TF-IDF vs BM25,
-- indice IVF y resultados en la web (score, galeria y k-NN en el mapa).
--
-- Antes de la demo (una sola vez):
--     python docs/presentacion/preparar_demo.py      -> datos/productos/demo_10k.csv (id, nombre, tipo, v)
-- Necesita datos/productos/vectores_100000.csv, vocabulario_256.npy e idf_256.npy, y las fotos en
-- C:\datos_bd2\imagenes (ver docs/comparacion_vectorial/README.md).
--
-- En la web se ejecuta bloque por bloque: seleccionar el bloque y Ejecutar, o Shift + Enter
-- sobre cada sentencia. Para probarla entera, sobre una base temporal:
--     python tools/probar_sql.py "docs/presentacion/demo - p3 y p4 (dayane).sql"
-- Las lineas "-- espera:" las revisa ese script; en la web son solo comentarios.


-- ===================== DATASET ABO =====================

-- [1] 10 000 productos de Amazon Berkeley Objects: texto en ingles y una foto por producto
CREATE TABLE productos FROM FILE 'datos/productos_10k.csv';
SELECT COUNT(*) FROM productos;
-- espera: ids 10000

-- [2] columnas: nombre, tipo, categoria, marca, color, descripcion (hasta 1000 bytes) e imagen
SELECT id, nombre, tipo, categoria, imagen FROM productos LIMIT 5;
-- espera: filas 5


-- ===================== PARTE 3: TF-IDF vs BM25 =====================

-- [3] indice invertido SPIMI sobre la descripcion
CREATE INDEX productos_desc ON productos (descripcion) USING SPIMI;
-- espera: ok

-- [4] TF-IDF (coseno) premia los textos cortos: despues de "White Table Lamp" salen mesas
--     ("Rivet End Tables", "Coffee Table") que solo dicen "table"
SELECT id, nombre, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'table lamp') USING TF_IDF ORDER BY relevancia DESC LIMIT 5;
-- espera: contiene "Rivet End Tables"

-- [5] BM25 con la misma consulta: lamparas que tienen las dos palabras
SELECT id, nombre, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'table lamp') USING BM25 ORDER BY relevancia DESC LIMIT 5;
-- espera: contiene "Lamp"

-- [6] "bar stool": BM25 acierta casi todo el top 10 (el mismo banco que la galeria del bloque 10)
SELECT id, nombre, tipo, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'bar stool') USING BM25 ORDER BY relevancia DESC LIMIT 10;
-- espera: contiene "STOOL_SEATING"

-- [7] el plan usa el indice invertido y no recorre la tabla
EXPLAIN ANALYZE SELECT id, nombre, SCORE() FROM productos
WHERE MATCH(descripcion, 'bar stool') USING BM25 ORDER BY score DESC LIMIT 10;
-- espera: contiene "Text Scan using productos_desc"

-- [8] MATCH combinado con un filtro normal
SELECT id, nombre, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'running shoes') AND tipo = 'SHOES' USING BM25 ORDER BY relevancia DESC LIMIT 5;
-- espera: filas 5


-- ===================== PARTE 4: IMAGENES E IVF =====================

-- [9] los mismos productos con el vector de su foto: SIFT -> 256 palabras visuales -> TF-IDF.
--     BPLUS para que, con el indice, las filas que devuelve FAISS se lean por clave y no recorriendo la tabla
CREATE TABLE fotos (id INT PRIMARY KEY, nombre VARCHAR(200), tipo VARCHAR(50), v VECTOR(256)) USING BPLUS;
COPY fotos FROM FILE 'datos/productos/demo_10k.csv';
SELECT COUNT(*) FROM fotos;
-- espera: ids 10000

-- [10] sin indice: k-NN secuencial del motor. Consulta con la foto del banco de bar (producto 5);
--      la web muestra la galeria de miniaturas
SELECT id, nombre, tipo, SIMILARITY_SCORE() AS score FROM fotos
WHERE SIMILAR_TO('4d/4dcbf99d.jpg', k=6);
-- espera: ids 5, 8406, 5423, 2142, 7633, 3232

-- [11] el plan: Seq Vector Scan lee toda la tabla (unas 5 000 paginas) y compara los 10 000 vectores
EXPLAIN ANALYZE SELECT id FROM fotos WHERE SIMILAR_TO('4d/4dcbf99d.jpg', k=6);
-- espera: contiene "Seq Vector Scan on fotos"

-- [12] indice IVF: k-means con 100 listas (raiz de 10 000), revisa las 8 mas cercanas
CREATE INDEX fotos_ivf ON fotos (v) USING IVF WITH (nlist = 100, nprobe = 8);
-- espera: faiss "fotos_ivf"

-- [13] la misma consulta con IVF: los mismos vecinos que el k-NN secuencial del bloque 10
SELECT id, nombre, tipo, SIMILARITY_SCORE() AS score FROM fotos
WHERE SIMILAR_TO('4d/4dcbf99d.jpg', k=6) USING IVF;
-- espera: igual 10

-- [14] el plan con IVF: comparar el tiempo y las paginas del nodo con el bloque 11 (unas 14 paginas)
EXPLAIN ANALYZE SELECT id FROM fotos WHERE SIMILAR_TO('4d/4dcbf99d.jpg', k=6) USING IVF;
-- espera: contiene "Vector Scan using fotos_ivf on fotos (IVF, coseno, nprobe=8)"

-- [15] exito: un mocasin (producto 133) devuelve zapatos
SELECT id, nombre, tipo, SIMILARITY_SCORE() AS score FROM fotos
WHERE SIMILAR_TO('08/086beb45.jpg', k=6) USING IVF;
-- espera: contiene "SHOES"

-- [16] error: una zapatilla gris lisa sobre fondo blanco (producto 75) devuelve galletas y
--      vitaminas. SIFT casi no encuentra puntos en superficies lisas y se parecen por el fondo
SELECT id, nombre, tipo, SIMILARITY_SCORE() AS score FROM fotos
WHERE SIMILAR_TO('d1/d16a576a.jpg', k=6) USING IVF;
-- espera: contiene "GROCERY"

-- [17] imagen + filtro: los 6 mas parecidos al banco, pero solo entre los STOOL_SEATING (k-NN secuencial con WHERE)
SELECT id, nombre, tipo, SIMILARITY_SCORE() AS score FROM fotos
WHERE SIMILAR_TO('4d/4dcbf99d.jpg', k=6) AND tipo = 'STOOL_SEATING';
-- espera: filas 6


-- ===================== INTERFAZ: k-NN EN EL MAPA =====================

-- [18] k vecinos espaciales: el mapa numera los vecinos y traza una linea desde el centro (UTEC)
CREATE TABLE tiendas (id INT PRIMARY KEY, nombre VARCHAR(30), lugar POINT);
INSERT INTO tiendas VALUES (1, 'Barranco', POINT(-12.1490, -77.0210));
INSERT INTO tiendas VALUES (2, 'Miraflores', POINT(-12.1211, -77.0297));
INSERT INTO tiendas VALUES (3, 'San Isidro', POINT(-12.0990, -77.0360));
INSERT INTO tiendas VALUES (4, 'Chorrillos', POINT(-12.1690, -77.0250));
INSERT INTO tiendas VALUES (5, 'Surco', POINT(-12.0860, -76.9760));
INSERT INTO tiendas VALUES (6, 'Cercado', POINT(-12.0464, -77.0300));
SELECT id, nombre FROM tiendas ORDER BY distancia(lugar, POINT(-12.1353, -77.0220)) LIMIT 3;
-- espera: ids 1, 2, 4


-- ===================== LIMPIEZA =====================

-- [19] al terminar (o antes de repetir la demo)
DROP TABLE fotos;
DROP TABLE productos;
DROP TABLE tiendas;
-- espera: ok
