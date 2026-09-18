#pragma once

#include "valor.h"

#include <string>
#include <vector>

namespace motor {
namespace sql {

// Gramática soportada (palabras clave sin distinguir mayúsculas):
//
//   CREATE TABLE t (col INT [PRIMARY KEY], col VARCHAR(n), ...) [USING HEAP|SEQUENTIAL|BPLUS]
//   CREATE TABLE t FROM FILE 'ruta.csv' [USING ...] [PRIMARY KEY col]
//   CREATE INDEX nombre ON t (col) [USING BPLUS|HASH]
//   DROP TABLE t
//   INSERT INTO t VALUES (v1, v2, ...)
//   DELETE FROM t [WHERE cond]
//   SELECT * | col, COUNT(*), SUM(col), AVG(col), MIN(col), MAX(col)
//     FROM t [[INNER] JOIN t2 ON col = col]
//     [WHERE cond [AND cond]...] [GROUP BY col] [ORDER BY col [ASC|DESC]] [LIMIT n]
//   SHOW TABLES
//   DESCRIBE t
//
//   cond := col (= | != | <> | < | <= | > | >=) valor | col BETWEEN a AND b
//   col  := nombre | tabla.nombre   (calificar solo hace falta si el nombre esta en las dos tablas)
//
// Del JOIN solo se admite INNER, uno por consulta y con una sola igualdad en el ON.
// No hay alias: los calificadores son nombres de tabla.

enum class TipoSentencia {
    CREATE_TABLE,
    CREATE_TABLE_FROM_FILE,
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
    std::string calificador;  // tabla en "tabla.col"; vacío si vino sin calificar
    std::string nombre_completo() const {
        return calificador.empty() ? columna : calificador + "." + columna;
    }
};

struct ItemSelect {
    std::string columna;   // vacía para COUNT(*)
    std::string agregado;  // vacía, COUNT, SUM, AVG, MIN, MAX
    std::string calificador;  // tabla en "tabla.col"; vacío si vino sin calificar
    std::string etiqueta() const {
        if (agregado.empty()) return columna;
        return agregado + "(" + (columna.empty() ? "*" : columna) + ")";
    }
    // como etiqueta(), pero conservando el calificador en las columnas simples
    std::string etiqueta_calificada() const {
        if (!agregado.empty()) return etiqueta();
        return calificador.empty() ? columna : calificador + "." + columna;
    }
};

// [INNER] JOIN tabla_derecha ON izq_columna = der_columna
struct JoinSpec {
    std::string tipo = "INNER";
    std::string tabla_derecha;
    std::string izq_calificador, izq_columna;
    std::string der_calificador, der_columna;
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

    std::vector<Valor> valores;         // INSERT
    std::vector<Condicion> condiciones; // WHERE, unidas por AND

    bool todas_las_columnas = false;
    std::vector<ItemSelect> items;
    std::vector<JoinSpec> joins;  // vacío = consulta de una sola tabla
    std::string group_by;
    std::string group_by_calificador;
    std::string order_by;
    std::string order_by_calificador;
    bool descendente = false;
    long long limite = -1;
};

// lanza std::runtime_error con el error de sintaxis
Sentencia parsear(const std::string& sql);

// parte un texto en sentencias por ';' respetando comillas; descarta vacías
std::vector<std::string> separar_sentencias(const std::string& texto);

}  // namespace sql
}  // namespace motor
