#pragma once

#include "valor.h"

#include <memory>
#include <string>
#include <vector>

namespace motor {
namespace sql {

// Gramática soportada (palabras clave sin distinguir mayúsculas):
//
//   CREATE TABLE t (col INT [PRIMARY KEY], col VARCHAR(n), ...) [USING HEAP|SEQUENTIAL|BPLUS]
//   CREATE TABLE t FROM FILE 'ruta.csv' [USING ...] [PRIMARY KEY col] [INDEX (col, ...)]
//   COPY t FROM FILE 'ruta.csv'
//   CREATE INDEX nombre ON t (col) [USING BPLUS|HASH]
//   DROP TABLE t
//   INSERT INTO t VALUES (v1, v2, ...)
//   DELETE FROM t [WHERE cond]
//   SELECT * | col, COUNT(*), SUM(col), AVG(col), MIN(col), MAX(col)
//     FROM t [WHERE cond [AND cond]...] [GROUP BY col] [ORDER BY col [ASC|DESC]] [LIMIT n]
//   SHOW TABLES
//   DESCRIBE t
//   EXPLAIN [ANALYZE] <sentencia>
//
//   cond := col (= | != | <> | < | <= | > | >=) valor | col BETWEEN a AND b
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
    DESCRIBE
};

struct ColumnaDef {
    std::string nombre;
    std::string tipo;  // INT | VARCHAR
    int tam = 0;       // largo máximo de VARCHAR
    bool pk = false;
};

struct Condicion {
    std::string columna;
    std::string op;  // = != < <= > >= BETWEEN
    Valor valor;
    Valor hasta;     // solo BETWEEN
};

struct ItemSelect {
    std::string columna;   // vacía para COUNT(*)
    std::string agregado;  // vacía, COUNT, SUM, AVG, MIN, MAX
    std::string etiqueta() const {
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
    std::string indice_tipo;   // BPLUS | HASH
    std::vector<std::string> indices;  // INDEX (a, b) en CREATE TABLE FROM FILE

    // EXPLAIN [ANALYZE]: la sentencia explicada viaja en `explicada`
    bool explain = false;
    bool analyze = false;
    std::shared_ptr<Sentencia> explicada;

    std::vector<Valor> valores;         // INSERT
    std::vector<Condicion> condiciones; // WHERE, unidas por AND

    bool todas_las_columnas = false;
    std::vector<ItemSelect> items;
    std::string group_by;
    std::string order_by;
    bool descendente = false;
    long long limite = -1;
};

// lanza std::runtime_error con el error de sintaxis
Sentencia parsear(const std::string& sql);

// parte un texto en sentencias por ';' respetando comillas; descarta vacías
std::vector<std::string> separar_sentencias(const std::string& texto);

}  // namespace sql
}  // namespace motor
