-- Pruebas de la Entrega Final: INNER JOIN con los datos de la tienda (docs/tienda.md).
--
--     python tools/probar_sql.py docs/pruebas_entrega_final.sql
--
-- Crea en una base temporal las tablas que usa el join (inventario, sedes y compras), con los
-- mismos indices que docs/tienda.sql. Cada JOIN debe dar lo mismo que las dos consultas sin
-- JOIN que usa la API de la tienda (api/tienda.py), que tambien estan aqui.


-- [1] inventario, sedes y compras, con sus indices
CREATE TABLE sedes (id INT PRIMARY KEY, nombre VARCHAR(40), distrito VARCHAR(30), ubicacion POINT);
COPY sedes FROM FILE 'datos/tienda/sedes.csv';
CREATE TABLE inventario (id INT PRIMARY KEY, sede_id INT, producto_id INT, stock INT);
COPY inventario FROM FILE 'datos/tienda/inventario.csv';
CREATE TABLE compras (id INT PRIMARY KEY, usuario_id INT, producto_id INT);
COPY compras FROM FILE 'datos/tienda/compras.csv';
CREATE INDEX inventario_producto ON inventario (producto_id) USING BPLUS;
CREATE INDEX compras_producto ON compras (producto_id) USING BPLUS;
CREATE INDEX compras_usuario ON compras (usuario_id) USING HASH;
CREATE INDEX sedes_ubicacion ON sedes (ubicacion) USING RTREE;
-- espera: ok


-- ========== sedes mas cercanas con stock ==========

-- [2] sin JOIN (como la API): las sedes con stock del producto 42...
SELECT sede_id, stock FROM inventario WHERE producto_id = 42 AND stock > 0;
-- espera: ids 13, 19

-- [3] ...y todas las sedes por distancia al centro de Lima; de ellas, la 13 va antes que la 19
SELECT id FROM sedes ORDER BY distancia(ubicacion, POINT(-12.0464, -77.0428));
-- espera: filas 25

-- [4] con JOIN: la consulta de aceptacion de #100 (Brena es la 13 y Ate la 19)
SELECT s.nombre, i.stock FROM inventario i JOIN sedes s ON i.sede_id = s.id
  WHERE i.producto_id = 42 AND i.stock > 0
  ORDER BY distancia(s.ubicacion, POINT(-12.0464, -77.0428)) LIMIT 3;
-- espera: ids Tienda Breña, Tienda Ate

-- [5] el producto 5 esta en tres sedes con stock
SELECT s.id, i.stock FROM inventario i JOIN sedes s ON i.sede_id = s.id
  WHERE i.producto_id = 5 AND i.stock > 0
  ORDER BY distancia(s.ubicacion, POINT(-12.0464, -77.0428)) LIMIT 3;
-- espera: ids 14, 8, 4

-- [6] sedes no tiene indice sobre id: hash join, con el inventario ya filtrado por el B+
EXPLAIN SELECT s.nombre, i.stock FROM inventario i JOIN sedes s ON i.sede_id = s.id
  WHERE i.producto_id = 42 AND i.stock > 0;
-- espera: contiene "Hash Join on inventario JOIN sedes"


-- ========== quienes compraron X tambien compraron Y ==========

-- [7] sin JOIN (como la API): nadie compro el producto 42
SELECT usuario_id FROM compras WHERE producto_id = 42;
-- espera: filas 0

-- [8] con JOIN: la consulta de aceptacion de #100, tambien vacia
SELECT c2.producto_id, COUNT(*) AS veces FROM compras c1 JOIN compras c2 ON c1.usuario_id = c2.usuario_id
  WHERE c1.producto_id = 42 AND c2.producto_id != 42
  GROUP BY c2.producto_id ORDER BY veces DESC LIMIT 10;
-- espera: filas 0

-- [9] el producto 1 lo compraron los usuarios 231 y 444
SELECT usuario_id FROM compras WHERE producto_id = 1;
-- espera: ids 231, 444

-- [10] lo que compraron esos dos usuarios, sin el producto 1: 31 productos, de los que quedan 10
SELECT c2.producto_id, COUNT(*) AS veces FROM compras c1 JOIN compras c2 ON c1.usuario_id = c2.usuario_id
  WHERE c1.producto_id = 1 AND c2.producto_id != 1
  GROUP BY c2.producto_id ORDER BY veces DESC LIMIT 10;
-- espera: filas 10

-- [11] c2 tiene un HASH sobre usuario_id: index nested loop, una busqueda por usuario de c1
EXPLAIN ANALYZE SELECT c2.producto_id, COUNT(*) AS veces FROM compras c1 JOIN compras c2 ON c1.usuario_id = c2.usuario_id
  WHERE c1.producto_id = 1 AND c2.producto_id != 1
  GROUP BY c2.producto_id ORDER BY veces DESC LIMIT 10;
-- espera: contiene "Index Nested Loop Join on compras JOIN compras"
