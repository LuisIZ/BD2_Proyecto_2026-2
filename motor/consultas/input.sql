-- carga de datos y consultas de prueba
CREATE TABLE alumnos (
    codigo VARCHAR(8) PRIMARY KEY,
    nombre VARCHAR(40),
    ciclo  INT INDEX BTREE
);

CREATE TABLE matriculas FROM FILE 'matriculas.csv' USING INDEX HASH (codigo);

INSERT INTO alumnos (codigo, nombre, ciclo) VALUES ('A100', 'Ana', 3), ('A101', 'Luis', 4);

SELECT codigo, nombre FROM alumnos WHERE ciclo BETWEEN 3 AND 7 ORDER BY nombre DESC LIMIT 10;

SELECT ciclo, COUNT(*) AS total FROM alumnos GROUP BY ciclo HAVING COUNT(*) > 1;

SELECT a.nombre, m.curso FROM alumnos AS a JOIN matriculas m ON a.codigo = m.codigo;

BEGIN TRANSACTION;
UPDATE alumnos SET ciclo = ciclo + 1 WHERE codigo = 'A100';
DELETE FROM alumnos WHERE ciclo > 10;
COMMIT;
