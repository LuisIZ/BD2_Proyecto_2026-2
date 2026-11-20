-- Demo de la presentacion final (Parte 5 y pendientes de la Parte 1): una consulta de cada tipo
-- sobre los datos de la tienda y de copyright. Sus bloques [1] y [2] cargan las tablas que usa
-- "demo - final.sql", y los bloques [3] a [7] muestran el JOIN y los algoritmos externos.
--
-- Antes de la demo (una sola vez):
--     python docs/presentacion/preparar_demo_p5.py --foto <una foto de un taburete o una silla>
--         -> datos/audio/remix_demo.wav y .build/subidas/foto.jpg
--     cd api && python -m multimedia.segmentos_audio --hop 5 --duracion 10    (si falta datos/audio/segmentos.csv)
-- En la web: si las tablas ya estan cargadas, saltar los bloques [1] y [2].
--
-- Para probarla entera sobre una base temporal:
--     python tools/probar_sql.py "docs/presentacion/demo - p5.sql"
-- Las lineas "-- espera:" las revisa ese script; en la web son solo comentarios.


-- ===================== CARGA (antes de la presentacion) =====================

-- [1] la tienda: productos con precio, vectores, sedes, inventario y compras, con sus indices
CREATE TABLE productos (id INT PRIMARY KEY, item_id VARCHAR(20), nombre VARCHAR(200), tipo VARCHAR(50), categoria VARCHAR(200), marca VARCHAR(80), color VARCHAR(40), descripcion VARCHAR(1000), imagen VARCHAR(80), precio FLOAT) USING BPLUS;
COPY productos FROM FILE 'datos/tienda/productos.csv';
CREATE TABLE catalogo (id INT PRIMARY KEY, nombre VARCHAR(80), v VECTOR(256)) USING BPLUS;
COPY catalogo FROM FILE 'datos/tienda/catalogo.csv';
CREATE TABLE sedes (id INT PRIMARY KEY, nombre VARCHAR(40), distrito VARCHAR(30), ubicacion POINT);
COPY sedes FROM FILE 'datos/tienda/sedes.csv';
CREATE TABLE inventario (id INT PRIMARY KEY, sede_id INT, producto_id INT, stock INT);
COPY inventario FROM FILE 'datos/tienda/inventario.csv';
CREATE TABLE usuarios (id INT PRIMARY KEY, nombre VARCHAR(40));
COPY usuarios FROM FILE 'datos/tienda/usuarios.csv';
CREATE TABLE compras (id INT PRIMARY KEY, usuario_id INT, producto_id INT);
COPY compras FROM FILE 'datos/tienda/compras.csv';
CREATE INDEX inventario_producto ON inventario (producto_id) USING BPLUS;
CREATE INDEX compras_producto ON compras (producto_id) USING BPLUS;
CREATE INDEX compras_usuario ON compras (usuario_id) USING HASH;
CREATE INDEX sedes_ubicacion ON sedes (ubicacion) USING RTREE;
CREATE INDEX productos_desc ON productos (descripcion) USING SPIMI;
CREATE INDEX catalogo_hnsw ON catalogo (v) USING HNSW;
-- espera: ok

-- [2] copyright: 999 canciones de GTZAN y sus 4 995 tramos de 10 s, con HNSW y B+
CREATE TABLE canciones (id INT PRIMARY KEY, archivo VARCHAR(40), genero VARCHAR(12));
COPY canciones FROM FILE 'datos/audio/canciones.csv';
CREATE TABLE segmentos (id INT PRIMARY KEY, cancion_id INT, inicio FLOAT, fin FLOAT, v VECTOR(256));
COPY segmentos FROM FILE 'datos/audio/segmentos.csv';
CREATE INDEX segmentos_hnsw ON segmentos (v) USING HNSW;
CREATE INDEX segmentos_cancion ON segmentos (cancion_id) USING BPLUS;
-- espera: ok


-- ===================== RELACIONAL: INNER JOIN (Luis) =====================

-- [3] las 3 sedes mas cercanas al centro de Lima que tienen stock del producto 42
SELECT s.nombre, i.stock FROM inventario i JOIN sedes s ON i.sede_id = s.id
  WHERE i.producto_id = 42 AND i.stock > 0
  ORDER BY distancia(s.ubicacion, POINT(-12.0464, -77.0428)) LIMIT 3;
-- espera: ids Tienda Breña, Tienda Ate

-- [4] sedes no tiene indice sobre id: el planificador elige hash join
EXPLAIN SELECT s.nombre, i.stock FROM inventario i JOIN sedes s ON i.sede_id = s.id
  WHERE i.producto_id = 42 AND i.stock > 0;
-- espera: contiene "Hash Join on inventario JOIN sedes"

-- [5] quienes compraron el producto 1 tambien compraron...: c2 tiene HASH, index nested loop
EXPLAIN ANALYZE SELECT c2.producto_id, COUNT(*) AS veces FROM compras c1 JOIN compras c2 ON c1.usuario_id = c2.usuario_id
  WHERE c1.producto_id = 1 AND c2.producto_id != 1
  GROUP BY c2.producto_id ORDER BY veces DESC LIMIT 10;
-- espera: contiene "Index Nested Loop Join on compras JOIN compras"


-- ===================== RELACIONAL: ALGORITMOS EXTERNOS EN DISCO =====================

-- [6] GROUP BY con external hashing (Dayane): 328 tipos repartidos en 9 particiones
EXPLAIN ANALYZE SELECT tipo, COUNT(*), AVG(precio) FROM productos GROUP BY tipo;
-- espera: contiene "particiones=9"

-- [7] ORDER BY con external merge sort (Noe): 10 000 filas en corridas de 10 paginas
EXPLAIN ANALYZE SELECT id, nombre, precio FROM productos ORDER BY precio DESC LIMIT 5;
-- espera: contiene "initial_runs="


-- ===================== TEXTO, ESPACIAL E IMAGEN =====================

-- [8] texto: BM25 sobre la descripcion, con el precio
SELECT id, nombre, precio, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'bar stool') USING BM25 ORDER BY relevancia DESC LIMIT 5;
-- espera: filas 5

-- [9] espacial: las 3 sedes mas cercanas a UTEC con el R-Tree
SELECT id, nombre, distrito FROM sedes ORDER BY distancia(ubicacion, POINT(-12.1353, -77.0220)) LIMIT 3;
-- espera: ids 2, 1, 15

-- [10] imagen, escrita a mano en la demo: los 10 productos que mas se parecen a una foto nueva
SELECT id, nombre, SIMILARITY_SCORE() FROM catalogo
WHERE SIMILAR_TO('foto.jpg', k = 10) USING HNSW;
-- espera: faiss "catalogo_hnsw"


-- ===================== AUDIO: BASE DE COPYRIGHT (Noe) =====================

-- [11] los 5 tramos de blues.00000 con el B+ sobre cancion_id
SELECT id, inicio, fin FROM segmentos WHERE cancion_id = 1;
-- espera: filas 5
-- La busqueda de audio (SIMILAR_TO(VECTOR(...), k = 5) USING HNSW sobre segmentos, una por tramo)
-- la hace la vista Copyright con el audio subido, y la muestra en "Consultas SQL".


-- ===================== LIMPIEZA (solo si hay que volver a cargar) =====================
-- DROP TABLE productos; DROP TABLE catalogo; DROP TABLE sedes; DROP TABLE inventario;
-- DROP TABLE usuarios; DROP TABLE compras; DROP TABLE canciones; DROP TABLE segmentos;
