#pragma once

#include "valor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace motor {
namespace sql {

// Gramática soportada (palabras clave sin distinguir mayúsculas):
//
//   CREATE TABLE t (col INT [PRIMARY KEY], col FLOAT, col VARCHAR(n), col POINT, col VECTOR(k), ...) [USING HEAP|SEQUENTIAL|BPLUS]
//   CREATE TABLE t FROM FILE 'ruta.csv' [USING ...] [PRIMARY KEY col] [INDEX (col, ...)]
//   COPY t FROM FILE 'ruta.csv'
//   CREATE INDEX nombre ON t (col) [USING BPLUS|HASH|RTREE|SPIMI]
//   CREATE INDEX nombre ON t (col) USING IVF|HNSW [WITH (parametro = valor, ...)]
//   DROP TABLE t
//   INSERT INTO t VALUES (v1, v2, ...)
//   DELETE FROM t [WHERE cond]
//   SELECT * | col, COUNT(*), SUM(col), AVG(col), MIN(col), MAX(col)
//     FROM t [WHERE cond [AND cond]... [AND MATCH(columna, 'consulta')]]
//     [USING TF_IDF|BM25] [GROUP BY col] [ORDER BY col [ASC|DESC]] [LIMIT n]
//   SCORE() se proyecta como columna; admite AS alias.
//   SELECT ... FROM t WHERE SIMILAR_TO([col,] 'archivo' | VECTOR(...) [, k = n]) [AND cond]...
//     [USING IVF|HNSW [CANDIDATES (pk, ...)]] [WITH METRIC = euclidiana|producto_punto|coseno]
//   SIMILARITY_SCORE() se proyecta como columna (mayor = más parecido); admite AS alias.
//   La API cambia 'archivo' por VECTOR(...) y, con USING IVF|HNSW, agrega CANDIDATES
//   con las pk que encontró FAISS.
//   SHOW TABLES
//   DESCRIBE t
//   EXPLAIN [ANALYZE] <sentencia>
//   BEGIN [TRANSACTION] | END [TRANSACTION] | COMMIT | ROLLBACK
//
//   cond  := col (= | != | <> | < | <= | > | >=) valor | col BETWEEN a AND b
//          | distancia(col, POINT(lat, lon) [, 'haversine' | 'euclidiana']) (< | <= | > | >=) metros
//          | distancia(col, VECTOR(x1, ..., xk) [, 'euclidiana' | 'producto_punto' | 'coseno']) (< | <= | > | >=) numero
//          | dentro(col, POLYGON((lat lon, lat lon, lat lon, ...)))
//   valor := entero | decimal | 'texto' | POINT(lat, lon) | VECTOR(x1, x2, ..., xk)
//   ORDER BY también acepta distancia(col, POINT(...) | VECTOR(...) [, metrica]) para los k vecinos más cercanos.
//
// EXPLAIN devuelve el plan sin tocar los datos; con ANALYZE lo ejecuta y añade
// las filas, páginas y tiempos reales de cada nodo.
//
// COPY carga un CSV en una tabla que ya existe: separa la definición del
// esquema de la carga de datos. El CSV debe traer encabezados que coincidan con
// las columnas de la tabla, en el mismo orden. `CREATE TABLE ... FROM FILE` es
// el atajo que hace las dos cosas de una vez, infiriendo el esquema.

enum class TipoSentencia {
    EXPLAIN,
    CREATE_TABLE,
    CREATE_TABLE_FROM_FILE,
    COPY_FROM_FILE,
    CREATE_INDEX,
    DROP_TABLE,
    INSERT,
    DELETE_FROM,
    SELECT,
    SHOW_TABLES,
    DESCRIBE,
    BEGIN_TRANSACTION,
    COMMIT,
    ROLLBACK
};

struct ColumnaDef {
    std::string nombre;
    std::string tipo;  // INT | FLOAT | VARCHAR | POINT | VECTOR
    int tam = 0;       // largo máximo de VARCHAR o dimensiones de VECTOR
    bool pk = false;
};

struct Condicion {
    std::string columna;
    std::string op;  // = != < <= > >= BETWEEN
    Valor valor;
    Valor hasta;     // solo BETWEEN
    std::string funcion;  // DISTANCIA | DENTRO, vacía si se compara la columna
    Valor punto;          // solo DISTANCIA: POINT o VECTOR
    std::string metrica;  // POINT: HAVERSINE | EUCLIDIANA; VECTOR: EUCLIDIANA | PRODUCTO_PUNTO | COSENO
    std::vector<Valor> poligono;  // solo DENTRO: vértices en orden
};

struct ItemSelect {
    std::string columna;   // vacía para COUNT(*)
    std::string agregado;  // vacía, COUNT, SUM, AVG, MIN, MAX
    std::string alias;
    bool score = false;      // SCORE() o SIMILARITY_SCORE()
    bool similitud = false;  // SIMILARITY_SCORE()
    std::string etiqueta() const {
        if (!alias.empty()) return alias;
        if (score) return "score";
        if (agregado.empty()) return columna;
        return agregado + "(" + (columna.empty() ? "*" : columna) + ")";
    }
};

struct Sentencia {
    TipoSentencia tipo = TipoSentencia::SHOW_TABLES;
    std::string tabla;
    std::string organizacion;  // HEAP | SEQUENTIAL | BPLUS (vacía = por defecto)
    std::string archivo_csv;
    std::string pk;            // PRIMARY KEY col en FROM FILE
    std::vector<ColumnaDef> columnas;

    std::string indice_nombre;
    std::string indice_columna;
    std::string indice_tipo;   // BPLUS | HASH | RTREE | SPIMI | IVF | HNSW
    std::vector<std::pair<std::string, std::string>> indice_parametros;  // WITH (nlist = 100, ...)
    std::vector<std::string> indices;  // INDEX (a, b) en CREATE TABLE FROM FILE

    // EXPLAIN [ANALYZE]: la sentencia explicada viaja en `explicada`
    bool explain = false;
    bool analyze = false;
    std::shared_ptr<Sentencia> explicada;

    std::vector<Valor> valores;         // INSERT
    std::vector<Condicion> condiciones; // WHERE, unidas por AND
    bool busqueda_texto = false;
    std::string columna_texto;
    std::string texto_consulta;
    std::string ranking_texto;  // TF_IDF | BM25

    // SIMILAR_TO: los k vecinos más parecidos al vector (o a la imagen, antes de que la API la cambie)
    bool busqueda_similar = false;
    std::string columna_similar;  // vacía: la única columna VECTOR de la tabla
    std::string archivo_similar;  // 'foto.jpg'
    Valor vector_similar;
    long long k_similar = 10;
    std::string metrica_similar = "COSENO";
    std::string indice_similar;   // IVF | HNSW; vacío = k-NN secuencial del motor
    bool hay_candidatos = false;
    std::vector<long long> candidatos;  // pk que devolvió FAISS, de la más parecida a la menos

    bool todas_las_columnas = false;
    std::vector<ItemSelect> items;
    std::string group_by;
    std::string order_by;
    bool order_by_distancia = false;  // ORDER BY distancia(order_by, order_by_punto)
    Valor order_by_punto;
    std::string order_by_metrica;
    bool descendente = false;
    long long limite = -1;
};

// lanza std::runtime_error con el error de sintaxis
Sentencia parsear(const std::string& sql);

// parte un texto en sentencias por ';' respetando comillas; descarta vacías
std::vector<std::string> separar_sentencias(const std::string& texto);

}  // namespace sql
}  // namespace motor
