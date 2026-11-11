-- Tienda (Parte 5): crea y carga las 6 tablas de la tienda y sus indices.
--
-- Los CSV ya estan en datos/tienda/. Para regenerarlos (ver docs/tienda.md):
--     python tools/tienda/generar_datos.py --n 10000 --semilla 7
--     python tools/dataset/preparar_catalogo.py --n 10000   (y copiarlo a datos/tienda/catalogo.csv)
--
-- Se ejecuta entero desde la vista Consultas de la web. Para probarlo sobre una base temporal:
--     python tools/probar_sql.py docs/tienda.sql
-- Las lineas "-- espera:" las revisa ese script; en la web son solo comentarios.
-- Si las tablas ya existen, borrarlas antes con el bloque del final (esta comentado).


-- [1] productos: las columnas de productos_10k.csv mas el precio. BPLUS para leer la ficha por id
CREATE TABLE productos (id INT PRIMARY KEY, item_id VARCHAR(20), nombre VARCHAR(200), tipo VARCHAR(50), categoria VARCHAR(200), marca VARCHAR(80), color VARCHAR(40), descripcion VARCHAR(1000), imagen VARCHAR(80), precio FLOAT) USING BPLUS;
COPY productos FROM FILE 'datos/tienda/productos.csv';
SELECT COUNT(*) FROM productos;
-- espera: ids 10000

-- [2] catalogo: el vector de 256 palabras visuales de cada producto, con los mismos id
CREATE TABLE catalogo (id INT PRIMARY KEY, nombre VARCHAR(80), v VECTOR(256)) USING BPLUS;
COPY catalogo FROM FILE 'datos/tienda/catalogo.csv';
SELECT COUNT(*) FROM catalogo;
-- espera: ids 10000

-- [3] sedes: 25 tiendas en distritos de Lima; ubicacion es POINT(lat, lon)
CREATE TABLE sedes (id INT PRIMARY KEY, nombre VARCHAR(40), distrito VARCHAR(30), ubicacion POINT);
COPY sedes FROM FILE 'datos/tienda/sedes.csv';
SELECT COUNT(*) FROM sedes;
-- espera: ids 25

-- [4] inventario: el stock de cada producto en cada sede (stock 0 = agotado ahi)
CREATE TABLE inventario (id INT PRIMARY KEY, sede_id INT, producto_id INT, stock INT);
COPY inventario FROM FILE 'datos/tienda/inventario.csv';
SELECT COUNT(*) FROM inventario;
-- espera: ids 29915

-- [5] usuarios y compras: cada usuario prefiere 2 o 3 tipos de producto
CREATE TABLE usuarios (id INT PRIMARY KEY, nombre VARCHAR(40));
COPY usuarios FROM FILE 'datos/tienda/usuarios.csv';
CREATE TABLE compras (id INT PRIMARY KEY, usuario_id INT, producto_id INT);
COPY compras FROM FILE 'datos/tienda/compras.csv';
SELECT COUNT(*) FROM compras;
-- espera: ids 7458


-- ===================== INDICES =====================

-- [6] B+ para "donde hay stock de X" y "quienes compraron X"; HASH para "que compro el usuario u"
CREATE INDEX inventario_producto ON inventario (producto_id) USING BPLUS;
CREATE INDEX compras_producto ON compras (producto_id) USING BPLUS;
CREATE INDEX compras_usuario ON compras (usuario_id) USING HASH;
-- espera: ok

-- [7] R-Tree para las sedes mas cercanas
CREATE INDEX sedes_ubicacion ON sedes (ubicacion) USING RTREE;
-- espera: ok

-- [8] indice invertido para buscar en la descripcion
CREATE INDEX productos_desc ON productos (descripcion) USING SPIMI;
-- espera: ok

-- [9] HNSW (FAISS) para "productos que se ven parecidos"
CREATE INDEX catalogo_hnsw ON catalogo (v) USING HNSW;
-- espera: ok


-- ===================== CONSULTAS DE PRUEBA =====================

-- [10] buscar por texto, con el precio
SELECT id, nombre, precio, SCORE() AS relevancia FROM productos
WHERE MATCH(descripcion, 'bar stool') USING BM25 ORDER BY relevancia DESC LIMIT 5;
-- espera: filas 5

-- [11] sedes que tienen el producto 5 (con el B+ de inventario)
SELECT sede_id, stock FROM inventario WHERE producto_id = 5;
-- espera: filas 3

-- [12] las 3 sedes mas cercanas a UTEC (con el R-Tree)
SELECT id, nombre, distrito FROM sedes ORDER BY distancia(ubicacion, POINT(-12.1353, -77.0220)) LIMIT 3;
-- espera: ids 2, 1, 15

-- [13] lo que compro el usuario 1 (con el HASH)
SELECT producto_id FROM compras WHERE usuario_id = 1;
-- espera: filas 12


-- ===================== BORRAR =====================
-- Para volver a cargar, quitar los "--" de estas lineas y ejecutarlas primero:
-- DROP TABLE productos;
-- DROP TABLE catalogo;
-- DROP TABLE sedes;
-- DROP TABLE inventario;
-- DROP TABLE usuarios;
-- DROP TABLE compras;
