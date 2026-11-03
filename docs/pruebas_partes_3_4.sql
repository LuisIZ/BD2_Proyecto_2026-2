-- Recorrido de las Partes 3 (texto) y 4 (vectores) del minigestor.
--
-- Se puede pegar bloque por bloque en la web (panel de consulta) o correr entero con
--     python tools/probar_sql.py docs/pruebas_partes_3_4.sql
-- que manda cada bloque a la API, como la web, sobre una base temporal y revisa las lineas
-- "-- espera:". Formas: ok | error "texto" | filas N | ids a, b, c | contiene "texto" | igual N | faiss "indice".
-- Todas miran la ultima sentencia del bloque.
--
-- La Parte 3 usa datos/productos_1k.csv (en el repo). La Parte 4 usa vectores de 4 dimensiones
-- escritos a mano, faciles de seguir: 1-4 son calzado, 5-7 ropa, 8-10 muebles y 11-12 decoracion.


-- ===================== PARTE 3: TEXTO =====================

-- [1] carga de 1000 productos e indice invertido SPIMI sobre la descripcion
CREATE TABLE productos FROM FILE 'datos/productos_1k.csv';
CREATE INDEX productos_desc ON productos (descripcion) USING SPIMI;
-- espera: ok

-- [2] DESCRIBE muestra el indice SPIMI de la columna
DESCRIBE productos;
-- espera: filas 9

-- [3] BM25: el cuadro de la puerta de madera sale primero
SELECT id, nombre, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'wooden door photo') USING BM25 ORDER BY relevancia DESC LIMIT 5;
-- espera: filas 5

-- [4] TF-IDF (coseno lnc.ltc) con la misma consulta
SELECT id, SCORE() FROM productos WHERE MATCH(descripcion, 'wooden door photo') USING TF_IDF ORDER BY score DESC LIMIT 5;
-- espera: filas 5

-- [5] MATCH combinado con otra condicion
SELECT id, SCORE() FROM productos WHERE MATCH(descripcion, 'door') AND id < 300 USING BM25 ORDER BY score DESC LIMIT 3;
-- espera: ok

-- [6] el plan usa el indice invertido
EXPLAIN ANALYZE SELECT id FROM productos WHERE MATCH(descripcion, 'wooden door') USING BM25 LIMIT 3;
-- espera: contiene "Text Scan using productos_desc"

-- [7] un INSERT invalida el indice y el siguiente MATCH lo reconstruye: el producto nuevo aparece
INSERT INTO productos VALUES (5000, 'X1', 'Puerta de prueba', 'DOOR', '', 'Ninguna', 'Negro', 'zzzunico wooden door prueba', 'x.jpg');
SELECT id FROM productos WHERE MATCH(descripcion, 'zzzunico') USING BM25;
-- espera: ids 5000

-- [8] MATCH sin USING es un error de sintaxis
SELECT id FROM productos WHERE MATCH(descripcion, 'door');
-- espera: error "MATCH requiere USING"

-- [9] MATCH sin indice SPIMI sobre la columna
SELECT id FROM productos WHERE MATCH(nombre, 'door') USING BM25;
-- espera: error "necesita un indice SPIMI"


-- ===================== PARTE 4: VECTORES =====================

-- [10] FLOAT y VECTOR(4)
CREATE TABLE catalogo (id INT PRIMARY KEY, nombre VARCHAR(20), precio FLOAT, v VECTOR(4));
INSERT INTO catalogo VALUES (1, 'zapato rojo', 45.5, VECTOR(1, 0, 0, 0));
INSERT INTO catalogo VALUES (2, 'zapato azul', 80, VECTOR(0.9, 0.1, 0, 0));
INSERT INTO catalogo VALUES (3, 'zapatilla', 30, VECTOR(0.8, 0.2, 0.1, 0));
INSERT INTO catalogo VALUES (4, 'bota', 120, VECTOR(0.7, 0, 0.3, 0));
INSERT INTO catalogo VALUES (5, 'camisa', 25, VECTOR(0, 1, 0, 0));
INSERT INTO catalogo VALUES (6, 'polo', 15, VECTOR(0.1, 0.9, 0, 0));
INSERT INTO catalogo VALUES (7, 'casaca', 150, VECTOR(0, 0.8, 0.2, 0));
INSERT INTO catalogo VALUES (8, 'lampara', 60, VECTOR(0, 0, 1, 0));
INSERT INTO catalogo VALUES (9, 'mesa', 300, VECTOR(0, 0, 0.9, 0.1));
INSERT INTO catalogo VALUES (10, 'silla', 90, VECTOR(0, 0.1, 0.8, 0.2));
INSERT INTO catalogo VALUES (11, 'cuadro', 70, VECTOR(0, 0, 0, 1));
INSERT INTO catalogo VALUES (12, 'espejo', 110, VECTOR(0, 0, 0.1, 0.9));
SELECT COUNT(*) FROM catalogo;
-- espera: ids 12

-- [11] FLOAT en WHERE, ORDER BY y agregados
SELECT id FROM catalogo WHERE precio BETWEEN 20 AND 50 ORDER BY precio;
-- espera: ids 5, 3, 1

-- [12] k-NN secuencial con distancia(): las tres metricas
SELECT id FROM catalogo ORDER BY distancia(v, VECTOR(1, 0.05, 0, 0), 'euclidiana') LIMIT 3;
-- espera: ids 1, 2, 3

-- [13]
SELECT id FROM catalogo ORDER BY distancia(v, VECTOR(1, 0.05, 0, 0), 'coseno') LIMIT 3;
-- espera: ids 1, 2, 3

-- [14] con producto punto pesa la magnitud: el vector mas largo hacia (1, 0, 0, 0) gana
SELECT id FROM catalogo ORDER BY distancia(v, VECTOR(1, 0.05, 0, 0), 'producto_punto') LIMIT 1;
-- espera: ids 1

-- [15] rango: todo lo que esta a menos de 0.1 de coseno
SELECT id FROM catalogo WHERE distancia(v, VECTOR(1, 0.05, 0, 0), 'coseno') < 0.1 ORDER BY id;
-- espera: ids 1, 2, 3, 4

-- [16] SIMILAR_TO sin indice: k-NN secuencial del motor, coseno por omision, con SIMILARITY_SCORE
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3);
-- espera: ids 1, 2, 3

-- [17] ORDER BY score ASC invierte los k; LIMIT corta despues
SELECT id, SIMILARITY_SCORE() FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) ORDER BY score ASC LIMIT 2;
-- espera: ids 3, 2

-- [18] con WHERE y sin indice: filtra primero y despues elige los 3 mas cercanos entre los baratos
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) AND precio < 50;
-- espera: ids 1, 3, 6

-- [19] otra metrica
SELECT id FROM catalogo WHERE SIMILAR_TO(v, VECTOR(0, 0, 0, 1), k=2) WITH METRIC=euclidiana;
-- espera: ids 11, 12

-- [20] planes sin indice
EXPLAIN SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3);
-- espera: contiene "Seq Vector Scan on catalogo (coseno)"

-- [21]
EXPLAIN SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) AND precio < 50;
-- espera: contiene "Top-k"

-- [22] indices de FAISS: el motor los registra y la API los construye
CREATE INDEX catalogo_hnsw ON catalogo (v) USING HNSW WITH (M = 16, ef_busqueda = 64);
CREATE INDEX catalogo_ivf ON catalogo (v) USING IVF WITH (nlist = 2, nprobe = 2);
-- espera: faiss "catalogo_ivf"

-- [23] DESCRIBE muestra los dos indices de v
DESCRIBE catalogo;
-- espera: contiene "catalogo_hnsw (HNSW), catalogo_ivf (IVF)"

-- [24] la misma consulta con HNSW y con IVF da los vecinos del k-NN secuencial (bloque 16)
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) USING HNSW;
-- espera: igual 16

-- [25]
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) USING IVF WITH METRIC=cosine;
-- espera: igual 16

-- [26] EXPLAIN con indice (sin ANALYZE no busca en FAISS) y EXPLAIN ANALYZE (si busca)
EXPLAIN SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) USING HNSW;
-- espera: contiene "Vector Scan using catalogo_hnsw on catalogo (HNSW, coseno, ef=64)"

-- [27]
EXPLAIN ANALYZE SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) USING IVF;
-- espera: contiene "Vector Scan using catalogo_ivf on catalogo (IVF, coseno, nprobe=2)"

-- [28] post-filtro con indice: FAISS trae los 3 mas parecidos (1, 2, 3) y el filtro deja 2 (menos que k)
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=3) AND precio < 50 USING HNSW;
-- espera: ids 1, 3

-- [29] INSERT: el indice se actualiza sin reconstruir y el nuevo sale primero
INSERT INTO catalogo VALUES (13, 'zapato verde', 50, VECTOR(1, 0.05, 0, 0));
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=2) USING HNSW;
-- espera: ids 13, 1

-- [30] DELETE: ya no sale
DELETE FROM catalogo WHERE id = 13;
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=2) USING HNSW;
-- espera: ids 1, 2

-- [31] ROLLBACK: la fila 1 vuelve a la tabla y FAISS nunca se entero del DELETE
BEGIN;
DELETE FROM catalogo WHERE id = 1;
ROLLBACK;
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=2) USING HNSW;
-- espera: ids 1, 2

-- [32] lote que termina con la transaccion abierta: el motor hace ROLLBACK solo
BEGIN;
DELETE FROM catalogo WHERE id = 1;
-- espera: ok

-- [33]
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=2) USING IVF;
-- espera: ids 1, 2

-- [34] COMMIT: ahora si se borra del indice
BEGIN;
DELETE FROM catalogo WHERE id = 1;
COMMIT;
-- espera: faiss "catalogo_hnsw"

-- [35]
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0.05, 0, 0), k=2) USING HNSW;
-- espera: ids 2, 3

-- [36] la metrica de la consulta tiene que ser la del indice
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0, 0, 0)) USING HNSW WITH METRIC=euclidiana;
-- espera: error "usa la metrica coseno"

-- [37] USING sin indice de ese tipo
CREATE TABLE sin_indices (id INT PRIMARY KEY, v VECTOR(4));
SELECT id FROM sin_indices WHERE SIMILAR_TO(VECTOR(1, 0, 0, 0)) USING HNSW;
-- espera: error "no hay un indice HNSW"

-- [38] el IVF necesita filas para entrenar el k-means
CREATE INDEX sin_ivf ON sin_indices (v) USING IVF;
-- espera: error "necesita al menos 100 filas"

-- [39] parametros invalidos
CREATE INDEX malo ON catalogo (nombre) USING HNSW WITH (nlist = 4);
-- espera: error "solo se crea sobre columnas VECTOR"

-- [40] dimension equivocada
SELECT id FROM catalogo WHERE SIMILAR_TO(VECTOR(1, 0)) ;
-- espera: error "tiene 2 componentes"

-- [41] SIMILAR_TO con una imagen: la API la busca entre las subidas y en las carpetas del dataset
SELECT id FROM catalogo WHERE SIMILAR_TO('no_existe.jpg', k=3);
-- espera: error "no se encontro la imagen"

-- [42] DROP TABLE borra la tabla, sus indices y los archivos de FAISS
DROP TABLE catalogo;
DROP TABLE sin_indices;
-- espera: ok


-- ===================== PARTE 4: AUDIO =====================
-- Los vectores de las canciones salen de api/multimedia/extraer_audio.py (MFCC + palabras de audio)
-- y se cargan con COPY (ver docs/extraccion_audio.md). La busqueda es la misma que con imagenes.

-- [43]
CREATE TABLE canciones (id INT PRIMARY KEY, genero VARCHAR(10), vector VECTOR(4));
INSERT INTO canciones VALUES (1, 'blues', VECTOR(0.9, 0.1, 0, 0));
INSERT INTO canciones VALUES (2, 'blues', VECTOR(0.8, 0.2, 0, 0));
INSERT INTO canciones VALUES (3, 'rock', VECTOR(0, 0.9, 0.1, 0));
INSERT INTO canciones VALUES (4, 'jazz', VECTOR(0, 0, 0.2, 0.8));
CREATE INDEX canciones_hnsw ON canciones (vector) USING HNSW;
SELECT id, genero, SIMILARITY_SCORE() FROM canciones WHERE SIMILAR_TO(VECTOR(1, 0.1, 0, 0), k=2) USING HNSW;
-- espera: ids 1, 2

-- [44]
DROP TABLE canciones;
-- espera: ok
