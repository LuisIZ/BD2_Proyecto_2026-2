-- Benchmark de la seccion 2.2.4: PostGIS geography(Point,4326) con indice GiST.
-- Genera los mismos puntos y centros que espacial_bench.cpp, semilla 42.
-- Ejecutar en una base PostgreSQL con PostGIS instalado.
--
-- pgAdmin: abrir este archivo en Query Tool y ejecutarlo completo.
-- La tabla postgis_resultados queda disponible para exportar a CSV.

CREATE EXTENSION IF NOT EXISTS postgis;

DROP TABLE IF EXISTS postgis_resultados;
CREATE TABLE postgis_resultados (
    estructura text,
    n integer,
    operacion text,
    parametro integer,
    tiempo_ms numeric,
    filas numeric,
    paginas numeric,
    bytes bigint
);

DROP TABLE IF EXISTS postgis_almacenamiento;
CREATE TABLE postgis_almacenamiento (
    n integer PRIMARY KEY,
    bytes_heap bigint NOT NULL,
    bytes_gist bigint NOT NULL
);

DO $$
DECLARE
    cantidad integer;
    x bigint;
    i integer;
    j integer;
    lat bigint;
    lon bigint;
    inicio timestamptz;
    total numeric;
    filas numeric;
    cuenta bigint;
    centro geography;
    radio integer;
    k integer;
    estrategia text;
    plan_text text;
    resultado integer[];
    resultado_referencia integer[];
BEGIN
    CREATE TEMP TABLE puntos_referencia (
        consulta integer PRIMARY KEY,
        ids integer[] NOT NULL
    ) ON COMMIT DROP;

    FOREACH cantidad IN ARRAY ARRAY[1000, 10000, 100000] LOOP
        DROP TABLE IF EXISTS puntos_postgis;
        CREATE TABLE puntos_postgis (
            id integer PRIMARY KEY,
            geog geography(Point, 4326) NOT NULL
        );
        x := 42;
        FOR i IN 1..cantidad LOOP
            x := (x * 1103515245 + 12345) % 2147483648;
            lat := -12300000 + x % 500000;
            x := (x * 1103515245 + 12345) % 2147483648;
            lon := -77200000 + x % 400000;
            INSERT INTO puntos_postgis
            VALUES (i, ST_SetSRID(ST_MakePoint(lon / 1000000.0, lat / 1000000.0), 4326)::geography);
        END LOOP;
        ANALYZE puntos_postgis;

        inicio := clock_timestamp();
        CREATE INDEX puntos_postgis_gix ON puntos_postgis USING gist (geog);
        INSERT INTO postgis_resultados
        VALUES ('postgis', cantidad, 'construccion', 0,
                extract(epoch FROM clock_timestamp() - inicio) * 1000,
                cantidad, 0, pg_relation_size('puntos_postgis_gix'));

        PERFORM set_config('enable_seqscan', 'off', true);
        PERFORM set_config('enable_indexscan', 'on', true);
        PERFORM set_config('enable_bitmapscan', 'off', true);
        SELECT geog INTO centro FROM puntos_postgis WHERE id = 1;
        EXECUTE 'EXPLAIN SELECT id FROM puntos_postgis WHERE ST_DWithin(geog, $1, 5000, false)'
        INTO plan_text USING centro;
        IF plan_text NOT LIKE '%puntos_postgis_gix%' THEN
            RAISE EXCEPTION 'PostGIS no selecciono GiST para ST_DWithin (n=%)', cantidad;
        END IF;
        EXECUTE 'EXPLAIN SELECT id FROM puntos_postgis ORDER BY geog <-> $1 LIMIT 10'
        INTO plan_text USING centro;
        IF plan_text NOT LIKE '%puntos_postgis_gix%' THEN
            RAISE EXCEPTION 'PostGIS no selecciono GiST para k-NN (n=%)', cantidad;
        END IF;

        FOREACH radio IN ARRAY ARRAY[1000, 5000, 10000] LOOP
            FOREACH estrategia IN ARRAY ARRAY['secuencial', 'postgis'] LOOP
                PERFORM set_config('enable_seqscan', CASE WHEN estrategia = 'secuencial' THEN 'on' ELSE 'off' END, true);
                PERFORM set_config('enable_indexscan', CASE WHEN estrategia = 'postgis' THEN 'on' ELSE 'off' END, true);
                PERFORM set_config('enable_bitmapscan', CASE WHEN estrategia = 'postgis' THEN 'on' ELSE 'off' END, true);
                total := 0;
                filas := 0;
                TRUNCATE puntos_referencia;
                FOR j IN 0..99 LOOP
                    SELECT geog INTO centro
                    FROM puntos_postgis
                    WHERE id = (j::bigint * 7919) % cantidad + 1;
                    inicio := clock_timestamp();
                    EXECUTE 'SELECT count(*) FROM puntos_postgis WHERE ST_DWithin(geog, $1, $2, false)'
                    INTO cuenta USING centro, radio;
                    total := total + extract(epoch FROM clock_timestamp() - inicio) * 1000;
                    filas := filas + cuenta;

                    EXECUTE 'SELECT array_agg(id ORDER BY id) FROM puntos_postgis WHERE ST_DWithin(geog, $1, $2, false)'
                    INTO resultado USING centro, radio;
                    IF estrategia = 'secuencial' THEN
                        INSERT INTO puntos_referencia VALUES (j, resultado);
                    ELSE
                        SELECT ids INTO resultado_referencia FROM puntos_referencia WHERE consulta = j;
                        IF resultado IS DISTINCT FROM resultado_referencia THEN
                            RAISE EXCEPTION 'PostGIS GiST y recorrido secuencial discrepan (n=%, radio=%, consulta=%)',
                                cantidad, radio, j;
                        END IF;
                    END IF;
                END LOOP;
                INSERT INTO postgis_resultados
                VALUES (estrategia, cantidad, 'rango', radio, total / 100, filas / 100, 0, NULL);
            END LOOP;
        END LOOP;

        FOREACH k IN ARRAY ARRAY[10, 50, 100] LOOP
            FOREACH estrategia IN ARRAY ARRAY['secuencial', 'postgis'] LOOP
                PERFORM set_config('enable_seqscan', CASE WHEN estrategia = 'secuencial' THEN 'on' ELSE 'off' END, true);
                PERFORM set_config('enable_indexscan', CASE WHEN estrategia = 'postgis' THEN 'on' ELSE 'off' END, true);
                PERFORM set_config('enable_bitmapscan', CASE WHEN estrategia = 'postgis' THEN 'on' ELSE 'off' END, true);
                total := 0;
                filas := 0;
                TRUNCATE puntos_referencia;
                FOR j IN 0..99 LOOP
                    SELECT geog INTO centro
                    FROM puntos_postgis
                    WHERE id = (j::bigint * 7919) % cantidad + 1;
                    inicio := clock_timestamp();
                    IF estrategia = 'secuencial' THEN
                        EXECUTE 'SELECT count(*) FROM (SELECT id FROM puntos_postgis ORDER BY ST_Distance(geog, $1, false), id LIMIT $2) q'
                        INTO cuenta USING centro, k;
                    ELSE
                        EXECUTE 'SELECT count(*) FROM (SELECT id FROM puntos_postgis ORDER BY geog <-> $1, id LIMIT $2) q'
                        INTO cuenta USING centro, k;
                    END IF;
                    total := total + extract(epoch FROM clock_timestamp() - inicio) * 1000;
                    filas := filas + cuenta;

                    IF estrategia = 'secuencial' THEN
                        EXECUTE 'SELECT array_agg(id ORDER BY ST_Distance(geog, $1, false), id) FROM (SELECT id, geog FROM puntos_postgis ORDER BY ST_Distance(geog, $1, false), id LIMIT $2) q'
                        INTO resultado USING centro, k;
                        INSERT INTO puntos_referencia VALUES (j, resultado);
                    ELSE
                        EXECUTE 'SELECT array_agg(id ORDER BY geog <-> $1, id) FROM (SELECT id, geog FROM puntos_postgis ORDER BY geog <-> $1, id LIMIT $2) q'
                        INTO resultado USING centro, k;
                        SELECT ids INTO resultado_referencia FROM puntos_referencia WHERE consulta = j;
                        IF resultado IS DISTINCT FROM resultado_referencia THEN
                            RAISE EXCEPTION 'PostGIS GiST y recorrido secuencial discrepan (n=%, k=%, consulta=%)',
                                cantidad, k, j;
                        END IF;
                    END IF;
                END LOOP;
                INSERT INTO postgis_resultados
                VALUES (estrategia, cantidad, 'knn', k, total / 100, filas / 100, 0, NULL);
            END LOOP;
        END LOOP;

        INSERT INTO postgis_almacenamiento
        VALUES (cantidad, pg_relation_size('puntos_postgis'), pg_relation_size('puntos_postgis_gix'));
        DROP TABLE puntos_postgis;
    END LOOP;
END $$;

SELECT estructura, n, operacion, parametro, round(tiempo_ms, 4) AS tiempo_ms,
       filas, paginas, bytes
FROM postgis_resultados
ORDER BY n, operacion, parametro, estructura;

-- Espacio persistente: tamaño de heap y del índice, en bytes, antes de borrar cada conjunto.
SELECT n, bytes_heap, bytes_gist
FROM postgis_almacenamiento
ORDER BY n;
