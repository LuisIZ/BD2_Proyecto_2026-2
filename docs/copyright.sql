-- [1] canciones originales de gtzan
CREATE TABLE canciones (id INT PRIMARY KEY, archivo VARCHAR(40), genero VARCHAR(12));
COPY canciones FROM FILE 'datos/audio/canciones.csv';

-- [2] tramos de 10 s cada 5 s con su vector
CREATE TABLE segmentos (id INT PRIMARY KEY, cancion_id INT, inicio FLOAT, fin FLOAT, v VECTOR(256));
COPY segmentos FROM FILE 'datos/audio/segmentos.csv';

-- [3] indice hnsw sobre los vectores y b+ sobre la cancion
CREATE INDEX segmentos_hnsw ON segmentos (v) USING HNSW;
CREATE INDEX segmentos_cancion ON segmentos (cancion_id) USING BPLUS;
-- espera faiss "segmentos_hnsw"

-- [4] cantidad de canciones
SELECT COUNT(*) FROM canciones;
-- contiene "999"

-- [5] cantidad de tramos
SELECT COUNT(*) FROM segmentos;
-- contiene "4995"

-- [6] los 5 tramos de una cancion por el indice b+
SELECT id, inicio, fin FROM segmentos WHERE cancion_id = 1;

-- [7] tramos mas parecidos a un clip completo usando hnsw
SELECT id, cancion_id, inicio, SIMILARITY_SCORE() FROM segmentos
WHERE SIMILAR_TO('blues/blues.00000.wav', k = 5) USING HNSW;
