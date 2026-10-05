-- Línea base de la sección 2.2.4: índice GiST de PostgreSQL sobre el tipo point nativo.
-- Genera los mismos puntos que motor/pruebas/espacial_bench.cpp (congruencial, semilla 42),
-- mide la construcción del índice y el promedio de 100 consultas de rango y de k-NN.
-- Con el tipo point, GiST usa distancia euclidiana en grados; el radio en metros se pasa a
-- grados con 111 194.9 m por grado, igual que la métrica euclidiana del motor.
--
-- Uso: pgAdmin > Query Tool > abrir este archivo > F5. No necesita PostGIS.

DROP TABLE IF EXISTS gist_resultados;
CREATE TABLE gist_resultados (
    n integer,
    operacion text,
    parametro integer,
    tiempo_ms numeric,
    filas numeric,
    bytes bigint,
    suma_lat bigint,
    suma_lon bigint
);

DO $$
DECLARE
    n integer;
    x bigint;
    i integer;
    j integer;
    lat bigint;
    lon bigint;
    suma_lat bigint;
    suma_lon bigint;
    inicio timestamptz;
    total numeric;
    filas numeric;
    cuenta bigint;
    centro point;
    radio integer;
    k integer;
BEGIN
    SET LOCAL enable_seqscan = off;
    FOREACH n IN ARRAY ARRAY[1000, 10000, 100000] LOOP
        DROP TABLE IF EXISTS puntos_gist;
        CREATE TABLE puntos_gist (id integer PRIMARY KEY, p point);
        x := 42;
        suma_lat := 0;
        suma_lon := 0;
        FOR i IN 1..n LOOP
            x := (x * 1103515245 + 12345) % 2147483648;
            lat := -12300000 + x % 500000;
            x := (x * 1103515245 + 12345) % 2147483648;
            lon := -77200000 + x % 400000;
            suma_lat := suma_lat + lat;
            suma_lon := suma_lon + lon;
            INSERT INTO puntos_gist VALUES (i, point(lat / 1000000.0, lon / 1000000.0));
        END LOOP;
        ANALYZE puntos_gist;

        inicio := clock_timestamp();
        CREATE INDEX puntos_gist_idx ON puntos_gist USING gist (p);
        INSERT INTO gist_resultados VALUES (n, 'construccion', 0,
            extract(epoch FROM clock_timestamp() - inicio) * 1000, n,
            pg_relation_size('puntos_gist_idx'), suma_lat, suma_lon);

        FOREACH radio IN ARRAY ARRAY[1000, 5000, 10000] LOOP
            total := 0;
            filas := 0;
            FOR j IN 0..99 LOOP
                SELECT p INTO centro FROM puntos_gist WHERE id = (j::bigint * 7919) % n + 1;
                inicio := clock_timestamp();
                SELECT count(*) INTO cuenta FROM puntos_gist WHERE p <@ circle(centro, radio / 111194.9);
                total := total + extract(epoch FROM clock_timestamp() - inicio) * 1000;
                filas := filas + cuenta;
            END LOOP;
            INSERT INTO gist_resultados VALUES (n, 'rango', radio, total / 100, filas / 100, NULL, NULL, NULL);
        END LOOP;

        FOREACH k IN ARRAY ARRAY[10, 50, 100] LOOP
            total := 0;
            filas := 0;
            FOR j IN 0..99 LOOP
                SELECT p INTO centro FROM puntos_gist WHERE id = (j::bigint * 7919) % n + 1;
                inicio := clock_timestamp();
                SELECT count(*) INTO cuenta FROM (SELECT id FROM puntos_gist ORDER BY p <-> centro LIMIT k) s;
                total := total + extract(epoch FROM clock_timestamp() - inicio) * 1000;
                filas := filas + cuenta;
            END LOOP;
            INSERT INTO gist_resultados VALUES (n, 'knn', k, total / 100, filas / 100, NULL, NULL, NULL);
        END LOOP;
    END LOOP;
    DROP TABLE puntos_gist;
END $$;

SELECT n, operacion, parametro, round(tiempo_ms, 4) AS tiempo_ms, filas, bytes, suma_lat, suma_lon
FROM gist_resultados
ORDER BY n, operacion, parametro;
