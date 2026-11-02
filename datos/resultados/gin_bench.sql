-- Línea base de la sección 2.3.2: índice GIN de PostgreSQL sobre tsvector, para comparar
-- con el índice invertido SPIMI del motor (motor/pruebas/texto_bench.cpp, make bench-texto).
-- Igual que el ejemplo de la semana 10: columna tsvector generada, índice GIN, @@ y ts_rank.
--
-- Mide, para 1k, 10k y 100k productos de ABO (docs/dataset.md):
--   - carga (incluye calcular el tsvector) y construcción del índice GIN
--   - tamaño de la tabla y del índice (pg_relation_size)
--   - tiempo promedio de las mismas 30 consultas que texto_bench (1, 3 y 5+ palabras)
--
-- Las consultas usan OR entre palabras: plainto_tsquery las une con AND y con 5 palabras casi
-- ningún producto las tiene todas, mientras que TF-IDF y BM25 rankean cualquier documento que
-- tenga al menos una. Así las tres técnicas responden la misma pregunta.
--
-- Uso:
--   1. Copiar datos/productos_1k.csv, _10k.csv y _100k.csv a una carpeta que el servidor pueda
--      leer y poner esa carpeta abajo, en bd2.datos.
--        pgAdmin (Windows): por ejemplo C:/datos_bd2/productos (fuera de OneDrive, porque el
--                           servicio de PostgreSQL no puede leer la carpeta del usuario)
--        Docker:            docker run --name pg-bd2 -e POSTGRES_PASSWORD=bd2 -p 5433:5432
--                             -v "%cd%/datos:/datos" -d postgres:18
--                           y bd2.datos = '/datos'
--      Si falta el de 100k, se omite ese tamaño.
--   2. pgAdmin > Query Tool > abrir este archivo > F5 (o psql -f datos/resultados/gin_bench.sql).
--   3. Exportar el último SELECT como datos/resultados/gin_bench.csv
--      (en psql: \copy (SELECT * FROM gin_resultados ORDER BY n, operacion, palabras) TO 'datos/resultados/gin_bench.csv' CSV HEADER).

SET bd2.datos = 'C:/datos_bd2/productos';

DROP TABLE IF EXISTS gin_resultados;
CREATE TABLE gin_resultados (
    estructura text,
    n integer,
    operacion text,
    palabras integer,
    tiempo_ms numeric,
    resultados numeric,
    bytes bigint
);

DROP TABLE IF EXISTS gin_consultas;
CREATE TABLE gin_consultas (palabras integer, texto text);
INSERT INTO gin_consultas VALUES
    (1, 'shoes'), (1, 'sofa'), (1, 'coffee'), (1, 'lamp'), (1, 'backpack'),
    (1, 'rug'), (1, 'chair'), (1, 'bottle'), (1, 'shirt'), (1, 'pillow'),
    (3, 'leather office chair'), (3, 'stainless steel bottle'), (3, 'wooden coffee table'),
    (3, 'running shoes men'), (3, 'cotton bath towel'), (3, 'led desk lamp'),
    (3, 'printed phone case'), (3, 'memory foam pillow'), (3, 'glass storage jar'),
    (3, 'women ankle boots'),
    (5, 'soft back case mobile cover'), (5, 'modern upholstered sofa living room'),
    (5, 'stainless steel insulated water bottle'), (5, 'solid wood dining table with chairs'),
    (5, 'women casual canvas sneakers lace up'), (5, 'organic ground coffee medium roast blend'),
    (5, 'queen size memory foam mattress topper'), (5, 'outdoor patio lounge chair with cushion'),
    (5, 'kids printed cotton t shirt'), (5, 'non slip bath mat for bathroom floor');

DO $$
DECLARE
    tam record;
    tabla text;
    archivo text;
    inicio timestamptz;
    consulta record;
    filas integer;
    total integer;
    veces integer;
    repeticiones constant integer := 5;
    -- top 10 por ts_rank entre los documentos con al menos una palabra (OR)
    sql_consulta constant text :=
        'SELECT count(*) FROM (
             SELECT id FROM %I,
                  (SELECT replace(plainto_tsquery(''english'', $1)::text, ''&'', ''|'')::tsquery) AS c(q)
             WHERE doc @@ q ORDER BY ts_rank(doc, q) DESC LIMIT 10) r';
BEGIN
    FOR tam IN SELECT * FROM (VALUES (1000, '1k'), (10000, '10k'), (100000, '100k')) AS t(n, nombre) LOOP
        tabla := 'gin_productos_' || tam.nombre;
        archivo := current_setting('bd2.datos') || '/productos_' || tam.nombre || '.csv';
        EXECUTE format('DROP TABLE IF EXISTS %I', tabla);
        EXECUTE format(
            'CREATE TABLE %I (
                id integer PRIMARY KEY, item_id varchar(20), nombre varchar(200), tipo varchar(50),
                categoria varchar(200), marca varchar(80), color varchar(40),
                descripcion varchar(1000), imagen varchar(80),
                doc tsvector GENERATED ALWAYS AS (to_tsvector(''english'', descripcion)) STORED)', tabla);

        -- carga: incluye calcular el tsvector de cada fila
        inicio := clock_timestamp();
        BEGIN
            EXECUTE format(
                'COPY %I (id, item_id, nombre, tipo, categoria, marca, color, descripcion, imagen)
                 FROM %L WITH (FORMAT csv, HEADER true, ENCODING ''UTF8'')', tabla, archivo);
        EXCEPTION WHEN undefined_file THEN
            RAISE NOTICE 'se omite n = %: no existe %', tam.n, archivo;
            EXECUTE format('DROP TABLE %I', tabla);
            CONTINUE;
        END;
        INSERT INTO gin_resultados VALUES ('gin', tam.n, 'carga', NULL,
            extract(epoch FROM clock_timestamp() - inicio) * 1000, NULL, pg_relation_size(tabla));

        inicio := clock_timestamp();
        EXECUTE format('CREATE INDEX %I ON %I USING GIN (doc)', tabla || '_gin', tabla);
        INSERT INTO gin_resultados VALUES ('gin', tam.n, 'construccion', NULL,
            extract(epoch FROM clock_timestamp() - inicio) * 1000, NULL,
            pg_relation_size(tabla || '_gin'));
        EXECUTE format('ANALYZE %I', tabla);

        -- una pasada sin medir, como en texto_bench, para no medir la caché fría
        FOR consulta IN SELECT * FROM gin_consultas LOOP
            EXECUTE format(sql_consulta, tabla)
                INTO filas USING consulta.texto;
        END LOOP;

        FOR consulta IN SELECT DISTINCT palabras FROM gin_consultas ORDER BY palabras LOOP
            total := 0;
            veces := 0;
            inicio := clock_timestamp();
            FOR i IN 1..repeticiones LOOP
                DECLARE
                    c record;
                BEGIN
                    FOR c IN SELECT texto FROM gin_consultas WHERE palabras = consulta.palabras LOOP
                        EXECUTE format(sql_consulta, tabla)
                            INTO filas USING c.texto;
                        total := total + filas;
                        veces := veces + 1;
                    END LOOP;
                END;
            END LOOP;
            INSERT INTO gin_resultados VALUES ('gin', tam.n, 'consulta', consulta.palabras,
                extract(epoch FROM clock_timestamp() - inicio) * 1000 / veces, total::numeric / veces, NULL);
        END LOOP;
    END LOOP;
END $$;

SELECT estructura, n, operacion, palabras, round(tiempo_ms, 4) AS tiempo_ms,
       round(resultados, 2) AS resultados, bytes
FROM gin_resultados
ORDER BY n, operacion DESC, palabras;
