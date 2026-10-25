-- paso 0
DROP TABLE org_heap;
DROP TABLE org_seq;
DROP TABLE org_bp;
DROP TABLE sedes;
DROP TABLE org_bplus;
DROP TABLE org_idx;

-- paso 1
CREATE TABLE org_heap FROM FILE 'datos/organizations-10000.csv' USING HEAP PRIMARY KEY Index;
CREATE TABLE org_seq FROM FILE 'datos/organizations-10000.csv' USING SEQUENTIAL;
CREATE TABLE org_bp FROM FILE 'datos/organizations-10000.csv' USING BPLUS;

-- paso 2
EXPLAIN ANALYZE SELECT Index, Name, Country FROM org_heap WHERE Index = 5000;
EXPLAIN ANALYZE SELECT Index, Name, Country FROM org_seq WHERE Index = 5000;
EXPLAIN ANALYZE SELECT Index, Name, Country FROM org_bp WHERE Index = 5000;

-- paso 3
EXPLAIN SELECT Name, Founded FROM org_heap WHERE Founded = 2000;
CREATE INDEX idx_founded ON org_heap (Founded);
EXPLAIN ANALYZE SELECT Name, Founded FROM org_heap WHERE Founded = 2000;

-- paso 4
EXPLAIN ANALYZE SELECT Country, COUNT(*), AVG(Number_of_employees) FROM org_bp GROUP BY Country ORDER BY Country LIMIT 10;

-- paso 5
BEGIN TRANSACTION;
DELETE FROM org_bp WHERE Index <= 100;
SELECT COUNT(*) FROM org_bp;
ROLLBACK;
SELECT COUNT(*) FROM org_bp;

-- paso 6
CREATE TABLE sedes (id INT PRIMARY KEY, nombre VARCHAR(30), distrito VARCHAR(20), ubicacion POINT);
INSERT INTO sedes VALUES (1, 'Plaza de Armas', 'Cercado', POINT(-12.0464, -77.0300));
INSERT INTO sedes VALUES (2, 'Parque Kennedy', 'Miraflores', POINT(-12.1211, -77.0297));
INSERT INTO sedes VALUES (3, 'El Olivar', 'San Isidro', POINT(-12.0990, -77.0360));
INSERT INTO sedes VALUES (4, 'Puente de los Suspiros', 'Barranco', POINT(-12.1490, -77.0210));
INSERT INTO sedes VALUES (5, 'UTEC', 'Barranco', POINT(-12.1353, -77.0220));
INSERT INTO sedes VALUES (6, 'Jockey Plaza', 'Surco', POINT(-12.0860, -76.9760));
INSERT INTO sedes VALUES (7, 'Universidad Agraria', 'La Molina', POINT(-12.0820, -76.9450));
INSERT INTO sedes VALUES (8, 'Plaza San Miguel', 'San Miguel', POINT(-12.0770, -77.0830));
INSERT INTO sedes VALUES (9, 'Real Plaza Callao', 'Callao', POINT(-12.0560, -77.1180));
INSERT INTO sedes VALUES (10, 'Plaza Norte', 'Independencia', POINT(-12.0070, -77.0590));
INSERT INTO sedes VALUES (11, 'Campo de Marte', 'Jesus Maria', POINT(-12.0710, -77.0430));
INSERT INTO sedes VALUES (12, 'Parque Lince', 'Lince', POINT(-12.0850, -77.0360));
INSERT INTO sedes VALUES (13, 'Biblioteca Nacional', 'San Borja', POINT(-12.0870, -77.0010));
INSERT INTO sedes VALUES (14, 'Malecon de Chorrillos', 'Chorrillos', POINT(-12.1690, -77.0250));
INSERT INTO sedes VALUES (15, 'Museo Larco', 'Pueblo Libre', POINT(-12.0720, -77.0710));

-- paso 7
EXPLAIN ANALYZE SELECT nombre, distrito FROM sedes WHERE distancia(ubicacion, POINT(-12.1353, -77.0220)) < 3000;

--paso 8
CREATE INDEX idx_geo ON sedes (ubicacion) USING RTREE;
EXPLAIN ANALYZE SELECT nombre, distrito FROM sedes WHERE distancia(ubicacion, POINT(-12.1353, -77.0220)) < 3000;
SELECT id, nombre, distrito FROM sedes WHERE distancia(ubicacion, POINT(-12.1353, -77.0220)) < 3000;
SELECT id, nombre, distrito FROM sedes WHERE distancia(ubicacion, POINT(-12.1353, -77.0220), 'euclidiana') < 3000;

-- paso 9
SELECT id, nombre, distrito FROM sedes ORDER BY distancia(ubicacion, POINT(-12.1353, -77.0220)) LIMIT 4;

-- paso 10
SELECT id, nombre, distrito FROM sedes WHERE dentro(ubicacion, POLYGON((-12.13 -77.05, -12.13 -77.02, -12.08 -77.02, -12.08 -77.05)));
