#include "../consultas/catalogo.h"
#include "../consultas/ejecutor.h"
#include "../consultas/parser_sql.h"
#include "../vectorial/vector.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using motor::sql::Catalogo;
using motor::sql::Ejecutor;
using motor::sql::Resultado;
using motor::sql::Sentencia;
using motor::sql::TipoSentencia;

namespace {

const std::string DB = ".build/db_sql_test";
const std::string CSV = ".build/sql_test.csv";

bool falla(const std::string& sql) {
    try {
        motor::sql::parsear(sql);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

// une las líneas que devuelve EXPLAIN para poder buscar dentro
std::string texto_del_plan(const Resultado& r) {
    std::string s;
    for (const auto& fila : r.filas) s += fila[0].texto + "\n";
    return s;
}

bool falla_ejecucion(Ejecutor& e, const std::string& sql) {
    try {
        e.ejecutar(sql);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

bool tiene_paso(const Resultado& r, const std::string& operacion) {
    for (const auto& p : r.plan) if (p.operacion == operacion) return true;
    return false;
}

std::string detalle_paso(const Resultado& r, const std::string& operacion, const std::string& clave) {
    for (const auto& p : r.plan) {
        if (p.operacion != operacion) continue;
        for (const auto& [k, v] : p.detalles) if (k == clave) return v;
    }
    return "";
}

void prueba_parser() {
    Sentencia s = motor::sql::parsear("select Index, name from Orgs where Founded >= 2010 and Country = 'Peru' order by Index desc limit 5");
    assert(s.tipo == TipoSentencia::SELECT && s.tabla == "Orgs" && s.items.size() == 2);
    assert(s.condiciones.size() == 2 && s.condiciones[0].op == ">=" && s.condiciones[1].valor.texto == "Peru");
    assert(s.order_by == "Index" && s.descendente && s.limite == 5);

    s = motor::sql::parsear("SELECT * FROM t WHERE k BETWEEN 3 AND 9");
    assert(s.todas_las_columnas && s.condiciones[0].op == "BETWEEN" && s.condiciones[0].hasta.entero == 9);

    s = motor::sql::parsear("SELECT Country, COUNT(*), AVG(emp) FROM t GROUP BY Country");
    assert(s.items.size() == 3 && s.items[1].agregado == "COUNT" && s.items[2].columna == "emp" && s.group_by == "Country");

    s = motor::sql::parsear("CREATE TABLE t (id INT PRIMARY KEY, nombre VARCHAR(30), anio INT) USING SEQUENTIAL");
    assert(s.tipo == TipoSentencia::CREATE_TABLE && s.columnas.size() == 3 && s.columnas[0].pk && s.columnas[1].tam == 30);
    assert(s.organizacion == "SEQUENTIAL");

    s = motor::sql::parsear("create table o from file 'datos/x.csv' using bplus primary key Index");
    assert(s.tipo == TipoSentencia::CREATE_TABLE_FROM_FILE && s.archivo_csv == "datos/x.csv" && s.organizacion == "BPLUS" && s.pk == "Index");

    s = motor::sql::parsear("CREATE INDEX idx ON t (anio) USING HASH");
    assert(s.tipo == TipoSentencia::CREATE_INDEX && s.indice_tipo == "HASH" && s.indice_columna == "anio");

    s = motor::sql::parsear("INSERT INTO t VALUES (1, 'O''Brien', -5)");
    assert(s.tipo == TipoSentencia::INSERT && s.valores.size() == 3 && s.valores[1].texto == "O'Brien" && s.valores[2].entero == -5);

    s = motor::sql::parsear("DELETE FROM t WHERE id <> 4;");
    assert(s.tipo == TipoSentencia::DELETE_FROM && s.condiciones[0].op == "!=");

    assert(motor::sql::parsear("show tables").tipo == TipoSentencia::SHOW_TABLES);
    assert(motor::sql::parsear("describe t").tabla == "t");

    s = motor::sql::parsear("CREATE TABLE p (id INT PRIMARY KEY, precio FLOAT, peso REAL)");
    assert(s.columnas[1].tipo == "FLOAT" && s.columnas[2].tipo == "FLOAT");
    s = motor::sql::parsear("SELECT * FROM p WHERE precio <= -2.5");
    assert(s.condiciones[0].valor.es_decimal && s.condiciones[0].valor.decimal == -2.5f);

    s = motor::sql::parsear("CREATE TABLE img (id INT PRIMARY KEY, v VECTOR(256))");
    assert(s.columnas[1].tipo == "VECTOR" && s.columnas[1].tam == 256);
    s = motor::sql::parsear("INSERT INTO img VALUES (1, VECTOR(0.5, -2, 1e-05, 3.5E+2))");
    assert(s.valores[1].es_vector && s.valores[1].componentes.size() == 4);
    assert(s.valores[1].componentes[1] == -2.0f && s.valores[1].componentes[2] == 1e-05f && s.valores[1].componentes[3] == 350.0f);
    assert(s.valores[1].a_texto() == "VECTOR(0.5, -2, 1e-05, 350)");
    assert(falla("CREATE TABLE t (v VECTOR)"));
    assert(falla("CREATE TABLE t (v VECTOR(0))"));
    assert(falla("INSERT INTO t VALUES (1, VECTOR())"));
    assert(falla("INSERT INTO t VALUES (1, VECTOR(1, 'a'))"));
    assert(falla("SELECT * FROM t WHERE distancia(p, POINT(1e1, 2)) < 5") && "las coordenadas no usan exponentes");
    s = motor::sql::parsear("SELECT id FROM img ORDER BY distancia(v, VECTOR(1, 0.5), 'cosine') LIMIT 3");
    assert(s.order_by_distancia && s.order_by_punto.es_vector && s.order_by_metrica == "COSENO" && s.limite == 3);
    s = motor::sql::parsear("SELECT id FROM img ORDER BY distancia(v, VECTOR(1, 0.5)) LIMIT 3");
    assert(s.order_by_metrica == "EUCLIDIANA" && "la metrica por omision de un vector es la euclidiana");
    s = motor::sql::parsear("SELECT * FROM img WHERE distancia(v, VECTOR(1, 2), 'producto_punto') <= -0.5");
    assert(s.condiciones[0].metrica == "PRODUCTO_PUNTO" && s.condiciones[0].valor.decimal == -0.5f);
    assert(falla("SELECT * FROM img WHERE distancia(v, VECTOR(1, 2), 'haversine') < 1"));
    assert(falla("SELECT * FROM t WHERE distancia(p, POINT(1, 2), 'coseno') < 1"));
    assert(falla("SELECT * FROM t WHERE distancia(p, POINT(1, 2)) < 1.5") && "con POINT se sigue comparando con metros enteros");

    s = motor::sql::parsear(
        "SELECT *, SCORE() AS relevancia FROM articulos "
        "WHERE MATCH(texto, 'machine learning') USING BM25 ORDER BY relevancia DESC LIMIT 10");
    assert(s.busqueda_texto && s.columna_texto == "texto" && s.texto_consulta == "machine learning");
    assert(s.ranking_texto == "BM25" && s.items.size() == 1 && s.items[0].score);
    assert(s.items[0].etiqueta() == "relevancia" && s.descendente && s.limite == 10);
    s = motor::sql::parsear("SELECT * FROM documentos WHERE MATCH(contenido, 'base de datos') USING TF_IDF");
    assert(s.busqueda_texto && s.ranking_texto == "TF_IDF");
    assert(falla("SELECT * FROM t WHERE MATCH(texto, 'foo')"));
    assert(falla("SELECT * FROM t USING BM25"));

    s = motor::sql::parsear(
        "SELECT id, SIMILARITY_SCORE() FROM imagenes WHERE SIMILAR_TO('foto.jpg', k=10) USING HNSW WITH METRIC=cosine "
        "ORDER BY score DESC");
    assert(s.busqueda_similar && s.archivo_similar == "foto.jpg" && s.columna_similar.empty() && s.k_similar == 10);
    assert(s.indice_similar == "HNSW" && s.metrica_similar == "COSENO" && !s.hay_candidatos);
    assert(s.items[1].score && s.items[1].similitud && s.items[1].etiqueta() == "score" && s.order_by == "score");
    s = motor::sql::parsear("SELECT * FROM img WHERE SIMILAR_TO(v, VECTOR(1, 0.5)) AND id > 3 WITH METRIC = 'euclidiana' LIMIT 2");
    assert(s.columna_similar == "v" && s.vector_similar.componentes.size() == 2 && s.archivo_similar.empty());
    assert(s.k_similar == 10 && "k por omision" && s.indice_similar.empty() && s.metrica_similar == "EUCLIDIANA");
    assert(s.condiciones.size() == 1 && s.condiciones[0].columna == "id" && s.limite == 2);
    s = motor::sql::parsear("SELECT * FROM img WHERE SIMILAR_TO(VECTOR(1, 2), k = 3)");
    assert(s.metrica_similar == "COSENO" && "la metrica por omision de SIMILAR_TO es el coseno" && s.k_similar == 3);
    s = motor::sql::parsear("SELECT id FROM img WHERE SIMILAR_TO(v, VECTOR(1, 2), k=3) USING IVF CANDIDATES (7, 2, 9) WITH METRIC=l2");
    assert(s.indice_similar == "IVF" && s.hay_candidatos && s.candidatos == std::vector<long long>({7, 2, 9}));
    s = motor::sql::parsear("SELECT id FROM img WHERE SIMILAR_TO(v, VECTOR(1, 2)) USING HNSW CANDIDATES ()");
    assert(s.hay_candidatos && s.candidatos.empty() && "FAISS puede no encontrar nada");
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO(v)"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('')"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('a.jpg', k=0)"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('a.jpg', 5)"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('a.jpg') AND SIMILAR_TO('b.jpg')"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('a.jpg') USING BM25"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('a.jpg') AND MATCH(t, 'x') USING BM25"));
    assert(falla("SELECT * FROM img USING HNSW"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('a.jpg') USING HNSW CANDIDATES (1,)"));
    assert(falla("SELECT * FROM img WHERE SIMILAR_TO('a.jpg') WITH METRIC=haversine"));
    assert(falla("SELECT * FROM img WITH METRIC=coseno"));
    assert(falla("SELECT SIMILARITY_SCORE() FROM img"));
    assert(falla("SELECT SCORE() FROM img WHERE SIMILAR_TO('a.jpg')") && "el score de texto es otro");
    assert(falla("SELECT SIMILARITY_SCORE() FROM t WHERE MATCH(texto, 'x') USING BM25"));

    s = motor::sql::parsear("CREATE INDEX iv ON img (v) USING HNSW WITH (M = 16, ef_busqueda = 64, metrica = coseno)");
    assert(s.indice_tipo == "HNSW" && s.indice_parametros.size() == 3);
    assert(s.indice_parametros[0].first == "M" && s.indice_parametros[0].second == "16" && s.indice_parametros[2].second == "coseno");
    s = motor::sql::parsear("CREATE INDEX iv ON img (v) USING IVF");
    assert(s.indice_tipo == "IVF" && s.indice_parametros.empty());
    assert(falla("CREATE INDEX iv ON img (v) USING BPLUS WITH (M = 16)"));
    assert(falla("CREATE INDEX iv ON img (v) USING IVF WITH (nlist)"));
    assert(falla("CREATE INDEX iv ON img (v) USING IVF WITH ()"));

    assert(falla("SELECT FROM t"));
    assert(falla("SELECT * t"));
    assert(falla("SELECT * FROM t WHERE"));
    assert(falla("INSERT INTO t VALUES (1, 'abierto)"));
    assert(falla("CREATE TABLE t (a DOUBLE)"));
    assert(falla("SELECT * FROM t WHERE a = 1 extra"));
    assert(falla("UPDATE t SET a = 1"));

    const auto partes = motor::sql::separar_sentencias("SELECT 1; INSERT INTO t VALUES ('a;b') ;\n\n ; DELETE FROM t");
    assert(partes.size() == 3 && partes[1] == "INSERT INTO t VALUES ('a;b')");

    s = motor::sql::parsear("CREATE TABLE tiendas (id INT PRIMARY KEY, ubicacion POINT)");
    assert(s.columnas[1].tipo == "POINT");
    s = motor::sql::parsear("INSERT INTO tiendas VALUES (1, POINT(-12.0464, -77.0428))");
    assert(s.valores[1].es_punto && s.valores[1].lat_e6 == -12046400 && s.valores[1].lon_e6 == -77042800);
    s = motor::sql::parsear("SELECT * FROM tiendas WHERE distancia(ubicacion, POINT(-12.0464, -77.0428)) < 5000");
    assert(s.condiciones[0].funcion == "DISTANCIA" && s.condiciones[0].columna == "ubicacion");
    assert(s.condiciones[0].op == "<" && s.condiciones[0].valor.entero == 5000 && s.condiciones[0].metrica == "HAVERSINE");
    s = motor::sql::parsear("SELECT * FROM tiendas ORDER BY distancia(ubicacion, POINT(1, 2), 'euclidiana') LIMIT 10");
    assert(s.order_by_distancia && s.order_by == "ubicacion" && s.order_by_metrica == "EUCLIDIANA" && s.limite == 10);
    assert(motor::sql::parsear("CREATE INDEX g ON tiendas (ubicacion) USING RTREE").indice_tipo == "RTREE");
    assert(falla("SELECT * FROM t WHERE distancia(u, POINT(1, 2)) = 3"));
    assert(falla("SELECT * FROM t WHERE distancia(u, 5) < 3"));
    assert(falla("SELECT * FROM t WHERE distancia(u, POINT(1, 2), 'manhattan') < 3"));
    assert(falla("INSERT INTO t VALUES (POINT(1.1234567, 2))"));
    assert(falla("INSERT INTO t VALUES (POINT(500, 2))"));
    assert(falla("INSERT INTO t VALUES (POINT(90.000001, 0))"));
    assert(falla("INSERT INTO t VALUES (POINT(0, 180.000001))"));
    s = motor::sql::parsear("SELECT * FROM t WHERE dentro(u, POLYGON((-12.13 -77.05, -12.13 -77.02, -12.09 -77.02))) AND id > 1");
    assert(s.condiciones.size() == 2 && s.condiciones[0].funcion == "DENTRO" && s.condiciones[0].columna == "u");
    assert(s.condiciones[0].poligono.size() == 3 && s.condiciones[0].poligono[1].lon_e6 == -77020000);
    assert(falla("SELECT * FROM t WHERE dentro(u, POLYGON((1 1, 2 2)))"));
    assert(falla("SELECT * FROM t WHERE dentro(u, POLYGON((90.000001 0, 0 1, 1 0)))"));
    assert(falla("SELECT * FROM t WHERE dentro(u, POLYGON((0 180.000001, 1 1, 2 0)))"));
    assert(falla("SELECT * FROM t WHERE dentro(u, POINT(1, 2))"));
    std::cout << "parser: sentencias, errores de sintaxis y separacion\n";
}

void escribir_csv() {
    std::ofstream f(CSV);
    f << "Index,Name,Country,Founded,Employees\n";
    for (int i = 1; i <= 300; ++i) {
        f << i << ",\"Org, " << i << "\"," << (i % 3 == 0 ? "Peru" : i % 3 == 1 ? "Chile" : "Bolivia") << ','
          << 2000 + i % 20 << ',' << i * 7 << '\n';
    }
}

void prueba_organizacion(Ejecutor& e, const std::string& org) {
    const std::string t = "org_" + org;
    Resultado r = e.ejecutar("CREATE TABLE " + t + " FROM FILE '" + CSV + "' USING " + org);
    assert(r.tipo == "create_table" && r.afectadas == 300);
    r = e.ejecutar("DESCRIBE " + t);
    assert(r.filas.size() == 5 && r.filas[0][1].texto == "INT" && r.filas[0][2].texto == "PK" && r.filas[1][1].texto == "VARCHAR(8)");

    // igualdad por clave
    r = e.ejecutar("SELECT Index, Name, Country FROM " + t + " WHERE Index = 150");
    assert(r.filas.size() == 1 && r.filas[0][1].texto == "Org, 150" && r.filas[0][2].texto == "Peru");
    assert(tiene_paso(r, org == "HEAP" ? "scan_completo" : "busqueda_por_clave"));
    assert(e.ejecutar("SELECT * FROM " + t + " WHERE Index = 999").filas.empty());

    // rango por clave y condicion adicional filtrada
    r = e.ejecutar("SELECT Index FROM " + t + " WHERE Index BETWEEN 10 AND 30 AND Country = 'Peru'");
    assert(r.filas.size() == 7 && r.filas[0][0].entero == 12);
    assert(tiene_paso(r, "filtro"));
    if (org != "HEAP") assert(tiene_paso(r, "rango_por_clave"));

    // ordenamiento, limite, agregados
    r = e.ejecutar("SELECT Index, Employees FROM " + t + " WHERE Founded = 2005 ORDER BY Employees DESC LIMIT 2");
    assert(r.filas.size() == 2 && r.filas[0][0].entero == 285 && r.filas[1][0].entero == 265);
    assert(tiene_paso(r, "ordenamiento") && tiene_paso(r, "limite"));
    r = e.ejecutar("SELECT Country, COUNT(*), SUM(Employees), MIN(Index) FROM " + t + " GROUP BY Country ORDER BY Country");
    assert(r.filas.size() == 3 && r.filas[0][0].texto == "Bolivia" && r.filas[0][1].entero == 100 && r.filas[2][3].entero == 3);
    assert(tiene_paso(r, "agrupacion"));
    r = e.ejecutar("SELECT Index, COUNT(*) FROM " + t + " GROUP BY Index");
    assert(r.filas.size() == 300);
    const int grupos_en_memoria = std::stoi(detalle_paso(r, "agrupacion", "grupos_en_memoria"));
    assert(grupos_en_memoria > 0 && grupos_en_memoria < 100);
    r = e.ejecutar("SELECT COUNT(*) FROM " + t);
    assert(r.filas[0][0].entero == 300);

    // insertar, clave repetida, borrar
    assert(e.ejecutar("INSERT INTO " + t + " VALUES (301, 'Nueva', 'Peru', 2024, 1)").afectadas == 1);
    assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (301, 'Repetida', 'Peru', 2024, 1)"));
    assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (302, 'Nombre demasiado largo', 'Peru', 2024, 1)"));
    assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (302, 'x', 'Peru', 'texto', 1)"));
    assert(e.ejecutar("SELECT * FROM " + t + " WHERE Index = 301").filas.size() == 1);
    r = e.ejecutar("DELETE FROM " + t + " WHERE Index BETWEEN 1 AND 100");
    assert(r.afectadas == 100);
    assert(e.ejecutar("SELECT COUNT(*) FROM " + t).filas[0][0].entero == 201);
    assert(e.ejecutar("SELECT * FROM " + t + " WHERE Index = 50").filas.empty());
    assert(e.ejecutar("DELETE FROM " + t + " WHERE Country = 'Nadie'").afectadas == 0);
    std::cout << "organizacion " << org << ": carga, busqueda, rango, agregados, insert, delete\n";
}

// MATCH con el indice SPIMI en las tres organizaciones
void prueba_busqueda_texto() {
    const std::string db_texto = ".build/db_sql_text_test";
    std::filesystem::remove_all(db_texto);
    Catalogo catalogo(db_texto);
    Ejecutor e(catalogo);
    for (const std::string org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        const std::string t = "texto_" + org;
        const std::string match = " WHERE MATCH(contenido, 'running shoes') USING BM25";
        e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, contenido VARCHAR(200), precio INT) USING " + org);
        e.ejecutar("INSERT INTO " + t + " VALUES (1, 'Running shoes with waterproof fabric', 150)");
        e.ejecutar("INSERT INTO " + t + " VALUES (2, 'Trail shoes for running and hiking', 80)");
        e.ejecutar("INSERT INTO " + t + " VALUES (3, 'Stainless steel water bottle', 20)");

        assert(falla_ejecucion(e, "SELECT id FROM " + t + match) && "MATCH sin indice SPIMI");
        e.ejecutar("CREATE INDEX idx ON " + t + " (contenido) USING SPIMI");

        Resultado r = e.ejecutar("SELECT id, SCORE() AS relevancia FROM " + t + match + " ORDER BY relevancia DESC LIMIT 10");
        assert(r.filas.size() == 2 && r.columnas.size() == 2);
        assert(r.columnas[1] == "relevancia" && r.filas[0][1].es_decimal && r.filas[0][1].decimal > 0);
        assert(r.filas[0][1].decimal >= r.filas[1][1].decimal);
        assert(tiene_paso(r, "busqueda_texto"));
        r = e.ejecutar("SELECT * FROM " + t + " WHERE MATCH(contenido, 'water bottle') USING TF_IDF LIMIT 10");
        assert(r.filas.size() == 1 && r.filas[0][0].entero == 3);

        // MATCH con otra condicion: el resto del WHERE filtra
        r = e.ejecutar("SELECT id FROM " + t + " WHERE MATCH(contenido, 'running shoes') AND precio < 100 USING BM25");
        assert(r.filas.size() == 1 && r.filas[0][0].entero == 2);

        // LIMIT sin filtros: el ranking solo pide los mejores k
        r = e.ejecutar("SELECT id FROM " + t + match + " LIMIT 1");
        assert(r.filas.size() == 1);

        r = e.ejecutar("EXPLAIN ANALYZE SELECT id FROM " + t + match);
        const std::string plan = texto_del_plan(r);
        assert(plan.find("Text Scan using idx on " + t + " (contenido)") != std::string::npos);
        assert(plan.find("Postings leidos:") != std::string::npos && plan.find("USING BM25") != std::string::npos);
        assert(texto_del_plan(e.ejecutar("EXPLAIN SELECT id FROM " + t + match)).find("Text Scan using idx") != std::string::npos);

        // INSERT y DELETE: el indice se reconstruye en la siguiente busqueda
        e.ejecutar("INSERT INTO " + t + " VALUES (4, 'Running shoes for kids', 40)");
        r = e.ejecutar("SELECT id FROM " + t + match);
        assert(r.filas.size() == 3);
        assert(texto_del_plan(e.ejecutar("EXPLAIN ANALYZE SELECT id FROM " + t + match)).find("Postings leidos:") != std::string::npos);
        e.ejecutar("DELETE FROM " + t + " WHERE id = 1");
        r = e.ejecutar("SELECT id FROM " + t + match);
        assert(r.filas.size() == 2);
        assert(falla_ejecucion(e, "DELETE FROM " + t + " WHERE MATCH(contenido, 'shoes') USING BM25") && "DELETE no admite MATCH");
        assert(falla_ejecucion(e, "SELECT id FROM " + t + " WHERE MATCH(precio, 'x') USING BM25") && "MATCH solo sobre VARCHAR");
    }

    // el indice sobrevive al cierre: otro catalogo lo reabre sin reconstruirlo
    Catalogo reabierto(db_texto);
    Ejecutor otro(reabierto);
    Resultado r = otro.ejecutar("SELECT id FROM texto_HEAP WHERE MATCH(contenido, 'running shoes') USING TF_IDF");
    assert(r.filas.size() == 2);
    std::cout << "MATCH: indice SPIMI en las tres organizaciones, filtros, EXPLAIN, INSERT/DELETE y reapertura\n";
}

// CREATE INDEX ... USING SPIMI sobre VARCHAR en las tres organizaciones
void prueba_indice_spimi() {
    const std::string db = ".build/db_sql_spimi_test";
    std::filesystem::remove_all(db);
    Catalogo catalogo(db);
    Ejecutor e(catalogo);
    for (const std::string org : {"heap", "sequential", "bplus"}) {
        const std::string t = "spimi_" + org;
        const std::string archivo = db + "/" + t + "__s.spimi/indice_spimi.bin";
        e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, descripcion VARCHAR(100), precio INT) USING " + org);
        e.ejecutar("INSERT INTO " + t + " VALUES (1, 'Running shoes with waterproof fabric', 50)");
        e.ejecutar("INSERT INTO " + t + " VALUES (2, 'Trail shoes for running and hiking', 80)");
        e.ejecutar("INSERT INTO " + t + " VALUES (3, 'Stainless steel water bottle', 20)");

        Resultado r = e.ejecutar("CREATE INDEX s ON " + t + " (descripcion) USING SPIMI");
        assert(r.afectadas == 3 && tiene_paso(r, "construir_indice"));
        assert(std::filesystem::exists(archivo));
        assert(falla_ejecucion(e, "CREATE INDEX s2 ON " + t + " (descripcion) USING SPIMI") && "una columna, un indice SPIMI");
        assert(falla_ejecucion(e, "CREATE INDEX s3 ON " + t + " (precio) USING SPIMI") && "SPIMI solo sobre VARCHAR");

        // el SPIMI no estorba a los demas indices de la columna
        e.ejecutar("CREATE INDEX b ON " + t + " (descripcion) USING BPLUS");
        r = e.ejecutar("SELECT id FROM " + t + " WHERE descripcion = 'Stainless steel water bottle'");
        assert(r.filas.size() == 1 && r.filas[0][0].entero == 3);

        // INSERT y DELETE invalidan el indice: se reconstruye en la proxima busqueda
        e.ejecutar("INSERT INTO " + t + " VALUES (4, 'Glass water bottle', 15)");
        assert(!std::filesystem::exists(archivo));
        e.ejecutar("DROP TABLE " + t);
        assert(!std::filesystem::exists(db + "/" + t + "__s.spimi"));
    }
    std::cout << "indice SPIMI: CREATE INDEX sobre VARCHAR en las tres organizaciones, se invalida al insertar y DROP lo borra\n";
}

void prueba_indice_secundario(Ejecutor& e) {
    assert(falla_ejecucion(e, "CREATE INDEX i ON org_HEAP (Country) USING RTREE") && "RTREE solo sobre POINT");

    for (const std::string org : {"SEQUENTIAL", "BPLUS"}) {
        const std::string t = "org_" + org;
        assert(e.ejecutar("CREATE INDEX idx_" + org + " ON " + t + " (Founded)").afectadas == 201);
        Resultado ri = e.ejecutar("SELECT Index FROM " + t + " WHERE Founded = 2005 ORDER BY Index");
        assert(tiene_paso(ri, "busqueda_por_indice") && ri.filas.size() == 10 && ri.filas[0][0].entero == 105);
        e.ejecutar("INSERT INTO " + t + " VALUES (400, 'Idx', 'Peru', 2005, 1)");
        assert(e.ejecutar("SELECT Index FROM " + t + " WHERE Founded = 2005").filas.size() == 11);
        e.ejecutar("DELETE FROM " + t + " WHERE Index = 400");
        assert(e.ejecutar("SELECT Index FROM " + t + " WHERE Founded = 2005").filas.size() == 10);
    }

    Resultado r = e.ejecutar("CREATE INDEX idx_f ON org_HEAP (Founded)");
    assert(r.afectadas == 201);
    r = e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded = 2005 ORDER BY Index");
    assert(tiene_paso(r, "busqueda_por_indice") && r.filas.size() == 10 && r.filas[0][0].entero == 105);
    r = e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded BETWEEN 2018 AND 2019");
    assert(tiene_paso(r, "rango_por_indice") && r.filas.size() == 20);

    // el indice se mantiene al insertar y borrar
    e.ejecutar("INSERT INTO org_HEAP VALUES (400, 'Idx', 'Peru', 2005, 1)");
    assert(e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded = 2005").filas.size() == 11);
    e.ejecutar("DELETE FROM org_HEAP WHERE Index = 400");
    assert(e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded = 2005").filas.size() == 10);
    assert(falla_ejecucion(e, "CREATE INDEX otro ON org_HEAP (Founded)"));
    std::cout << "indice secundario: B+ no agrupado sobre heap, uso en igualdad y rango, mantenimiento\n";
}

void prueba_indice_hash(Ejecutor& e) {
    assert(falla_ejecucion(e, "CREATE INDEX h ON org_SEQUENTIAL (Name) USING RTREE"));
    e.ejecutar("CREATE TABLE org_hash FROM FILE '" + CSV + "'");
    Resultado r = e.ejecutar("CREATE INDEX idx_emp ON org_hash (Employees) USING HASH");
    assert(r.afectadas == 300 && detalle_paso(r, "construir_indice", "estructura") == "hash_extensible");

    r = e.ejecutar("SELECT Index FROM org_hash WHERE Employees = 735");
    assert(tiene_paso(r, "busqueda_por_indice") && r.filas.size() == 1 && r.filas[0][0].entero == 105);
    assert(detalle_paso(r, "busqueda_por_indice", "estructura") == "hash_extensible");
    r = e.ejecutar("SELECT Index FROM org_hash WHERE Employees BETWEEN 735 AND 770");
    assert(tiene_paso(r, "scan_completo") && !tiene_paso(r, "rango_por_indice") && r.filas.size() == 6);

    e.ejecutar("INSERT INTO org_hash VALUES (500, 'Hash', 'Peru', 2001, 735)");
    assert(e.ejecutar("SELECT Index FROM org_hash WHERE Employees = 735").filas.size() == 2);
    e.ejecutar("DELETE FROM org_hash WHERE Index = 500");
    assert(e.ejecutar("SELECT Index FROM org_hash WHERE Employees = 735").filas.size() == 1);

    {
        Catalogo otro(DB);
        Ejecutor e2(otro);
        r = e2.ejecutar("SELECT Index FROM org_hash WHERE Employees = 1400");
        assert(tiene_paso(r, "busqueda_por_indice") && r.filas.size() == 1 && r.filas[0][0].entero == 200);
    }
    e.ejecutar("DROP TABLE org_hash");
    assert(!std::filesystem::exists(DB + "/org_hash__idx_emp.hash"));
    std::cout << "indice hash: igualdad por hash, rango por recorrido, mantenimiento y persistencia\n";
}

// EXPLAIN describe sin tocar los datos; EXPLAIN ANALYZE ejecuta y mide
void prueba_explain(Ejecutor& e) {
    Resultado r = e.ejecutar("EXPLAIN SELECT * FROM org_HEAP WHERE Founded = 2005");
    assert(r.tipo == "explain" && !r.analizado);
    assert(r.columnas.size() == 1 && r.columnas[0] == "QUERY PLAN");
    assert(!r.plan.empty() && r.plan[0].nodo == "Index Scan" && "el acceso es la hoja del plan");
    assert(r.plan[0].indice == "idx_f" && r.plan[0].columna == "Founded" && "el plan dice qué índice y sobre qué columna");
    assert(r.plan[0].costo >= 0 && r.plan[0].filas_estimadas > 0 && "hay estimación");
    assert(r.plan[0].filas_reales < 0 && "sin ANALYZE no se ejecuta nada");
    assert(texto_del_plan(r).find("Index Cond: Founded = 2005") != std::string::npos);

    // el plan se lee con la raíz arriba: Limit envuelve a Sort, que envuelve al acceso
    r = e.ejecutar("EXPLAIN SELECT Index FROM org_HEAP WHERE Founded = 2005 ORDER BY Index LIMIT 3");
    assert(r.plan.front().nodo == "Limit" && r.plan.front().nivel == 0);
    assert(r.plan.back().operacion == "busqueda_por_indice" && r.plan.back().nivel == static_cast<int>(r.plan.size()) - 1);

    r = e.ejecutar("EXPLAIN ANALYZE SELECT Index FROM org_HEAP WHERE Founded = 2005");
    assert(r.analizado && "ANALYZE marca el resultado");
    const motor::sql::PasoPlan& acceso = r.plan.back();
    assert(acceso.filas_reales == 10 && acceso.paginas_leidas >= 0 && acceso.tiempo_ms >= 0 && "trae medidas reales");
    assert(texto_del_plan(r).find("actual") != std::string::npos);

    r = e.ejecutar("EXPLAIN ANALYZE SELECT Index FROM org_HEAP WHERE Founded = 2005 ORDER BY Index LIMIT 3");
    assert(r.planificacion_ms > 0 && r.ejecucion_ms > 0);
    double suma_nodos = 0;
    for (const motor::sql::PasoPlan& p : r.plan) if (p.tiempo_ms >= 0) suma_nodos += p.tiempo_ms;
    assert(r.ejecucion_ms >= suma_nodos);
    const std::size_t n = r.filas.size();
    assert(r.filas[n - 2][0].texto.rfind("Planning Time: ", 0) == 0);
    assert(r.filas[n - 1][0].texto.rfind("Execution Time: ", 0) == 0);
    r = e.ejecutar("EXPLAIN SELECT Index FROM org_HEAP WHERE Founded = 2005");
    assert(r.planificacion_ms > 0 && r.filas.back()[0].texto.rfind("Planning Time: ", 0) == 0);

    // el B+ agrupado navega por la clave; el heap sin índice recorre todo
    assert(e.ejecutar("EXPLAIN SELECT * FROM org_BPLUS WHERE Index = 50").plan.back().operacion == "busqueda_por_clave");
    assert(e.ejecutar("EXPLAIN SELECT * FROM org_HEAP WHERE Employees = 7").plan.back().operacion == "scan_completo");

    assert(falla_ejecucion(e, "EXPLAIN CREATE TABLE x (a INT)") && "sin ANALYZE solo describe consultas");
    assert(falla(" EXPLAIN EXPLAIN SELECT * FROM org_HEAP") && "no se anida");
    assert(falla("EXPLAIN") && "EXPLAIN necesita una sentencia");
    std::cout << "EXPLAIN: plan estimado sin ejecutar, ANALYZE con medidas reales y tiempos al final\n";
}

// COPY separa la definición del esquema de la carga de datos
void prueba_copy(Ejecutor& e) {
    e.ejecutar("CREATE TABLE copia (Index INT PRIMARY KEY, Name VARCHAR(20), Country VARCHAR(10), Founded INT, Employees INT) USING SEQUENTIAL");
    assert(e.ejecutar("SELECT COUNT(*) FROM copia").filas[0][0].entero == 0 && "nace vacia");

    Resultado r = e.ejecutar("COPY copia FROM FILE '" + CSV + "'");
    assert(r.tipo == "copy" && r.afectadas == 300);
    assert(tiene_paso(r, "carga_masiva") && "con la tabla vacia se escribe de una vez");
    assert(e.ejecutar("SELECT COUNT(*) FROM copia").filas[0][0].entero == 300);
    assert(e.ejecutar("SELECT Name FROM copia WHERE Index = 7").filas[0][0].texto == "Org, 7");

    // una segunda carga sobre datos ya existentes choca con la clave primaria
    assert(falla_ejecucion(e, "COPY copia FROM FILE '" + CSV + "'"));

    // el CSV tiene que cuadrar con el esquema
    e.ejecutar("CREATE TABLE otra (a INT PRIMARY KEY, b VARCHAR(5))");
    assert(falla_ejecucion(e, "COPY otra FROM FILE '" + CSV + "'") && "distinto numero de columnas");
    assert(falla_ejecucion(e, "COPY no_existe FROM FILE '" + CSV + "'"));
    e.ejecutar("DROP TABLE otra");
    e.ejecutar("DROP TABLE copia");
    std::cout << "COPY: carga un CSV en una tabla ya creada, separada del CREATE TABLE\n";
}

// Una carga inicial en secuencial debe quedar entera en el área principal.
// Insertando una a una, los registros que no caben en su página van al área
// auxiliar y acaban disparando reorganizaciones.
void prueba_carga_secuencial(Ejecutor& e) {
    e.ejecutar("CREATE TABLE seq_carga FROM FILE '" + CSV + "' USING SEQUENTIAL");
    const Resultado r = e.ejecutar("SHOW TABLES");
    bool vista = false;
    for (const auto& fila : r.filas) {
        if (fila[0].texto != "seq_carga") continue;
        vista = true;
        const std::string detalle = fila[8].texto;
        assert(detalle.find("aux=0") != std::string::npos && "la carga masiva no usa el area auxiliar");
        assert(detalle.find("reorganizaciones=0") != std::string::npos && "ni dispara reorganizaciones");
        std::cout << "carga secuencial: " << detalle << "\n";
    }
    assert(vista && "falta seq_carga");
    assert(e.ejecutar("SELECT COUNT(*) FROM seq_carga").filas[0][0].entero == 300);
    assert(e.ejecutar("SELECT Name FROM seq_carga WHERE Index = 150").filas[0][0].texto == "Org, 150");
    e.ejecutar("DROP TABLE seq_carga");
}

void prueba_persistencia() {
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    assert(catalogo.tablas().size() == 3);
    assert(e.ejecutar("SELECT COUNT(*) FROM org_BPLUS").filas[0][0].entero == 201);
    assert(e.ejecutar("SELECT COUNT(*) FROM org_SEQUENTIAL").filas[0][0].entero == 201);
    assert(tiene_paso(e.ejecutar("SELECT * FROM org_HEAP WHERE Founded = 2005"), "busqueda_por_indice"));
    assert(e.ejecutar("SHOW TABLES").filas.size() == 3);
    e.ejecutar("DROP TABLE org_HEAP");
    assert(!catalogo.existe("org_heap") && !std::filesystem::exists(DB + "/org_heap.heap"));
    assert(falla_ejecucion(e, "SELECT * FROM org_HEAP"));
    std::cout << "persistencia: el catalogo y los indices sobreviven al cierre; drop borra archivos\n";
}

void prueba_tabla_manual() {
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    e.ejecutar("CREATE TABLE alumnos (codigo INT PRIMARY KEY, nombre VARCHAR(20), nota INT) USING BPLUS");
    e.ejecutar("INSERT INTO alumnos VALUES (20, 'Ana', 17)");
    e.ejecutar("INSERT INTO alumnos VALUES (10, 'Luis', 12)");
    e.ejecutar("INSERT INTO alumnos VALUES (30, 'Eva', 19)");
    Resultado r = e.ejecutar("SELECT nombre FROM alumnos WHERE nota > 15 ORDER BY nombre");
    assert(r.filas.size() == 2 && r.filas[0][0].texto == "Ana" && r.filas[1][0].texto == "Eva");
    r = e.ejecutar("SELECT * FROM alumnos");
    assert(r.filas.size() == 3 && r.filas[0][0].entero == 10 && "el agrupado devuelve en orden de clave");
    assert(falla_ejecucion(e, "CREATE TABLE alumnos (x INT)"));
    assert(falla_ejecucion(e, "CREATE TABLE sinpk (a VARCHAR(3))"));
    std::cout << "tabla manual: CREATE TABLE con esquema explicito\n";
}

void prueba_pk_no_primera() {
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    for (const std::string org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        const std::string t = "notas_" + org;
        e.ejecutar("CREATE TABLE " + t + " (nombre VARCHAR(20), codigo INT PRIMARY KEY, nota INT) USING " + org);
        e.ejecutar("INSERT INTO " + t + " VALUES ('Ana', 30, 17)");
        e.ejecutar("INSERT INTO " + t + " VALUES ('Luis', 10, 12)");
        e.ejecutar("INSERT INTO " + t + " VALUES ('Eva', 20, 19)");
        Resultado r = e.ejecutar("SELECT nombre, nota FROM " + t + " WHERE codigo = 20");
        assert(r.filas.size() == 1 && r.filas[0][0].texto == "Eva" && r.filas[0][1].entero == 19);
        if (org != "HEAP") assert(tiene_paso(r, "busqueda_por_clave"));
        r = e.ejecutar("SELECT codigo FROM " + t + " WHERE codigo BETWEEN 15 AND 30 ORDER BY codigo");
        assert(r.filas.size() == 2 && r.filas[0][0].entero == 20 && r.filas[1][0].entero == 30);
        assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES ('Otra', 10, 5)"));
        e.ejecutar("DELETE FROM " + t + " WHERE codigo = 30");
        assert(e.ejecutar("SELECT COUNT(*) FROM " + t).filas[0][0].entero == 2);
    }

    e.ejecutar("CREATE INDEX idx_nota ON notas_HEAP (nota)");
    Resultado r = e.ejecutar("EXPLAIN SELECT nombre FROM notas_HEAP WHERE nota = 12");
    assert(r.plan.back().indice == "idx_nota" && r.plan.back().columna == "nota");
    assert(texto_del_plan(r).find("Index Scan using idx_nota on notas_HEAP (nota)") != std::string::npos);
    assert(e.ejecutar("SELECT nombre FROM notas_HEAP WHERE nota = 12").filas[0][0].texto == "Luis");

    for (const std::string org : {"HEAP", "SEQUENTIAL", "BPLUS"}) e.ejecutar("DROP TABLE notas_" + org);
    std::cout << "clave primaria en la segunda columna: busqueda, rango, repetidas e indice por columna\n";
}

std::vector<std::string> nombres(const Resultado& r) {
    std::vector<std::string> salida;
    for (const auto& fila : r.filas) salida.push_back(fila[0].texto);
    return salida;
}

void prueba_float() {
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    for (const char* org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        const std::string t = std::string("precios_") + org;
        e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, nombre VARCHAR(10), precio FLOAT) USING " + org);
        e.ejecutar("INSERT INTO " + t + " VALUES (1, 'pan', 0.5)");
        e.ejecutar("INSERT INTO " + t + " VALUES (2, 'leche', 4.25)");
        e.ejecutar("INSERT INTO " + t + " VALUES (3, 'cafe', 12)");
        e.ejecutar("INSERT INTO " + t + " VALUES (4, 'agua', 0.1)");

        Resultado r = e.ejecutar("SELECT id, precio FROM " + t + " WHERE precio > 0.3 ORDER BY precio DESC");
        assert(r.filas.size() == 3 && r.filas[0][0].entero == 3 && r.filas[2][0].entero == 1);
        assert(r.filas[0][1].es_decimal && r.filas[0][1].a_texto() == "12" && "el entero se guarda como FLOAT");
        assert(e.ejecutar("SELECT nombre FROM " + t + " WHERE precio = 0.1").filas[0][0].texto == "agua" &&
               "el literal se redondea a float32 igual que la columna");
        assert(e.ejecutar("SELECT COUNT(*) FROM " + t + " WHERE precio BETWEEN 0.5 AND 4.25").filas[0][0].entero == 2);

        r = e.ejecutar("SELECT SUM(precio), MIN(precio), MAX(precio), AVG(precio) FROM " + t);
        assert(r.filas[0][0].es_decimal && std::fabs(r.filas[0][0].decimal - 16.85f) < 1e-4);
        assert(r.filas[0][1].a_texto() == "0.1" && r.filas[0][2].a_texto() == "12");

        assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (5, 'sal', 'caro')"));
        assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (2.5, 'sal', 1)") && "la clave sigue siendo INT");
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE nombre = 1.5"));
        assert(falla_ejecucion(e, "CREATE INDEX ip ON " + t + " (precio)") && "solo se indexan columnas INT");
        e.ejecutar("DROP TABLE " + t);
    }

    // COPY lee los decimales del CSV como float32
    const std::string csv = ".build/sql_test_float.csv";
    {
        std::ofstream f(csv);
        f << "id,precio\n1,19.99\n2,-3\n3,1e-3\n";
    }
    e.ejecutar("CREATE TABLE copia_float (id INT PRIMARY KEY, precio FLOAT)");
    assert(e.ejecutar("COPY copia_float FROM FILE '" + csv + "'").afectadas == 3);
    Resultado r = e.ejecutar("SELECT precio FROM copia_float ORDER BY precio");
    assert(r.filas[0][0].a_texto() == "-3" && r.filas[1][0].a_texto() == "0.001" && r.filas[2][0].a_texto() == "19.99");
    assert(e.ejecutar("DESCRIBE copia_float").filas[1][1].texto == "FLOAT");
    e.ejecutar("DROP TABLE copia_float");
    {
        std::ofstream f(csv);
        f << "id,precio\n1,abc\n";
    }
    e.ejecutar("CREATE TABLE copia_float (id INT PRIMARY KEY, precio FLOAT)");
    assert(falla_ejecucion(e, "COPY copia_float FROM FILE '" + csv + "'"));
    e.ejecutar("DROP TABLE copia_float");
    std::cout << "FLOAT: float32 en heap, secuencial y B+, WHERE, ORDER BY, agregados y COPY\n";
}

void prueba_vector() {
    {
        Catalogo catalogo(DB);
        Ejecutor e(catalogo);
        for (const char* org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
            const std::string t = std::string("vectores_") + org;
            e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, v VECTOR(4), nombre VARCHAR(8)) USING " + org);
            e.ejecutar("INSERT INTO " + t + " VALUES (2, VECTOR(0.1, -2, 3.25, 1e-05), 'b')");
            e.ejecutar("INSERT INTO " + t + " VALUES (1, VECTOR(1, 2, 3, 4), 'a')");
            const Resultado r = e.ejecutar("SELECT v, nombre FROM " + t + " WHERE id = 2");
            assert(r.filas.size() == 1 && r.filas[0][0].es_vector && r.filas[0][1].texto == "b");
            assert(r.filas[0][0].a_texto() == "VECTOR(0.1, -2, 3.25, 1e-05)" && "se guarda en float32 sin perder nada");
            assert(e.ejecutar("DESCRIBE " + t).filas[1][1].texto == "VECTOR(4)");

            assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (3, VECTOR(1, 2, 3), 'c')") && "dimension distinta");
            assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (3, 'texto', 'c')"));
            assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (3, VECTOR(1, 2, 3, 4), VECTOR(1, 2))"));
            assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE v = 1") && "un vector se compara con distancia");
            assert(falla_ejecucion(e, "SELECT * FROM " + t + " ORDER BY v"));
            assert(falla_ejecucion(e, "CREATE INDEX iv ON " + t + " (v)") && "VECTOR no tiene indice en el motor");
            e.ejecutar("DELETE FROM " + t + " WHERE id = 1");
            assert(e.ejecutar("SELECT * FROM " + t).filas.size() == 1);
        }
        assert(falla_ejecucion(e, "CREATE TABLE t (id INT, v VECTOR(513))"));
        assert(falla_ejecucion(e, "CREATE TABLE t (v VECTOR(4) PRIMARY KEY)"));
        assert(falla_ejecucion(e, "CREATE TABLE t (id INT, v VECTOR(512)) USING BPLUS") && "el registro fijo pasa de 2040 bytes");
        e.ejecutar("CREATE TABLE grande (id INT PRIMARY KEY, v VECTOR(512))");
        e.ejecutar("DROP TABLE grande");

        // COPY lee el formato que escribe extraer.py: "VECTOR(x1,x2,...)" entre comillas
        const std::string csv = ".build/sql_test_vector.csv";
        {
            std::ofstream f(csv);
            f << "id,v\n1,\"VECTOR(0.5,1.25e-06,-3,4)\"\n2,\"[1, 2, 3, 4]\"\n3,0 0 0 1\n";
        }
        e.ejecutar("CREATE TABLE copia_vector (id INT PRIMARY KEY, v VECTOR(4))");
        assert(e.ejecutar("COPY copia_vector FROM FILE '" + csv + "'").afectadas == 3);
        const Resultado r = e.ejecutar("SELECT v FROM copia_vector");
        assert(r.filas[0][0].a_texto() == "VECTOR(0.5, 1.25e-06, -3, 4)" && r.filas[2][0].a_texto() == "VECTOR(0, 0, 0, 1)");
        for (const char* malo : {"id,v\n1,\"VECTOR(1,2,3)\"\n", "id,v\n1,\"VECTOR(1,x,3,4)\"\n"}) {
            {
                std::ofstream f(csv);
                f << malo;
            }
            e.ejecutar("CREATE TABLE copia_mala (id INT PRIMARY KEY, v VECTOR(4))");
            assert(falla_ejecucion(e, "COPY copia_mala FROM FILE '" + csv + "'"));
            e.ejecutar("DROP TABLE copia_mala");
        }
    }
    // los vectores y la dimension sobreviven a reabrir el catalogo
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    assert(e.ejecutar("DESCRIBE copia_vector").filas[1][1].texto == "VECTOR(4)");
    assert(e.ejecutar("SELECT v FROM copia_vector WHERE id = 1").filas[0][0].a_texto() == "VECTOR(0.5, 1.25e-06, -3, 4)");
    e.ejecutar("DROP TABLE copia_vector");
    std::cout << "VECTOR: float32 en heap, secuencial y B+, literal, COPY, DESCRIBE y reapertura\n";
}

std::string literal_vector(const motor::vectorial::Vector& v) {
    return motor::sql::Valor::de_vector(v).a_texto();
}

// ids de los k vecinos calculados a mano; `acepta` deja fuera filas (para probar con WHERE)
template <typename Acepta>
std::vector<long long> knn_fuerza_bruta(const std::vector<motor::vectorial::Vector>& datos, const motor::vectorial::Vector& q, std::size_t k,
                                        motor::vectorial::MetricaVec m, bool lejanos, Acepta acepta) {
    std::vector<std::pair<double, long long>> todos;
    for (std::size_t i = 0; i < datos.size(); ++i) {
        if (acepta(static_cast<long long>(i + 1))) todos.push_back({motor::vectorial::distancia(datos[i], q, m), static_cast<long long>(i + 1)});
    }
    std::sort(todos.begin(), todos.end());
    if (lejanos) std::reverse(todos.begin(), todos.end());
    std::vector<long long> ids;
    for (std::size_t i = 0; i < k && i < todos.size(); ++i) ids.push_back(todos[i].second);
    return ids;
}

std::vector<long long> ids(const Resultado& r) {
    std::vector<long long> salida;
    for (const auto& f : r.filas) salida.push_back(f[0].entero);
    return salida;
}

void prueba_knn_vectorial() {
    using motor::vectorial::MetricaVec;
    const std::size_t n = 300, dim = 16;
    std::vector<motor::vectorial::Vector> datos(n, motor::vectorial::Vector(dim));
    unsigned semilla = 7;
    auto azar = [&]() {
        semilla = semilla * 1103515245u + 12345u;
        return static_cast<float>((semilla >> 8) % 20001) / 10000.0f - 1.0f;
    };
    for (auto& v : datos) for (float& x : v) x = azar();
    const std::string csv = ".build/sql_test_knn.csv";
    {
        std::ofstream f(csv);
        f << "id,v\n";
        for (std::size_t i = 0; i < n; ++i) f << i + 1 << ",\"" << literal_vector(datos[i]) << "\"\n";
    }
    std::vector<motor::vectorial::Vector> consultas(3, motor::vectorial::Vector(dim));
    for (auto& q : consultas) for (float& x : q) x = azar();
    consultas[2] = datos[41];  // una fila de la tabla: su vecino más cercano es ella misma
    const std::pair<const char*, MetricaVec> metricas[] = {
        {"euclidiana", MetricaVec::EUCLIDIANA}, {"producto_punto", MetricaVec::PRODUCTO_PUNTO}, {"coseno", MetricaVec::COSENO}};
    const auto todas = [](long long) { return true; };

    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    for (const char* org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        const std::string t = std::string("knn_") + org;
        e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, v VECTOR(16)) USING " + org);
        e.ejecutar("COPY " + t + " FROM FILE '" + csv + "'");
        for (const auto& q : consultas) {
            for (const auto& [nombre, m] : metricas) {
                const std::string dist = "distancia(v, " + literal_vector(q) + ", '" + nombre + "')";
                Resultado r = e.ejecutar("SELECT id FROM " + t + " ORDER BY " + dist + " LIMIT 10");
                assert(ids(r) == knn_fuerza_bruta(datos, q, 10, m, false, todas));
                assert(tiene_paso(r, "knn_vectorial") && !tiene_paso(r, "ordenamiento") && "el heap ya entrega el orden");
                assert(detalle_paso(r, "knn_vectorial", "distancias_calculadas") == std::to_string(n));

                // con WHERE, DESC o sin LIMIT no sirve el heap: Seq Scan, filtro y Sort, con el mismo resultado
                r = e.ejecutar("SELECT id FROM " + t + " WHERE id > 150 ORDER BY " + dist + " LIMIT 5");
                assert(ids(r) == knn_fuerza_bruta(datos, q, 5, m, false, [](long long id) { return id > 150; }));
                assert(!tiene_paso(r, "knn_vectorial") && tiene_paso(r, "ordenamiento"));
                assert(ids(e.ejecutar("SELECT id FROM " + t + " ORDER BY " + dist + " DESC LIMIT 5")) ==
                       knn_fuerza_bruta(datos, q, 5, m, true, todas));

                // rango: todas las filas a menos de la distancia del 20.o vecino
                const auto veinte = knn_fuerza_bruta(datos, q, 20, m, false, todas);
                const double radio = motor::vectorial::distancia(datos[static_cast<std::size_t>(veinte.back() - 1)], q, m);
                char limite[32];
                std::snprintf(limite, sizeof(limite), "%.9g", radio + 1e-6);
                r = e.ejecutar("SELECT COUNT(*) FROM " + t + " WHERE " + dist + " < " + limite);
                assert(r.filas[0][0].entero == 20);
            }
        }
        Resultado r = e.ejecutar("SELECT id FROM " + t + " ORDER BY distancia(v, " + literal_vector(consultas[2]) + ") LIMIT 1");
        assert(r.filas[0][0].entero == 42);
        r = e.ejecutar("EXPLAIN SELECT * FROM " + t + " ORDER BY distancia(v, " + literal_vector(consultas[0]) + ", 'coseno') LIMIT 3");
        assert(texto_del_plan(r).find("Seq Vector Scan") != std::string::npos);
        assert(texto_del_plan(r).find("VECTOR(16 dimensiones)") != std::string::npos);

        assert(falla_ejecucion(e, "SELECT * FROM " + t + " ORDER BY distancia(v, VECTOR(1, 2)) LIMIT 3") && "dimension distinta");
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " ORDER BY distancia(v, POINT(1, 2)) LIMIT 3"));
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE distancia(id, " + literal_vector(consultas[0]) + ") < 1"));
        e.ejecutar("DELETE FROM " + t + " WHERE distancia(v, " + literal_vector(consultas[2]) + ") < 0.0001");
        assert(e.ejecutar("SELECT COUNT(*) FROM " + t).filas[0][0].entero == static_cast<long long>(n - 1));
        e.ejecutar("DROP TABLE " + t);
    }
    std::cout << "k-NN de vectores: euclidiana, producto punto y coseno igual a la fuerza bruta en las tres organizaciones;\n"
                 "          con heap (Seq Vector Scan) sin WHERE, con Sort si hay WHERE o DESC; rango y DELETE por distancia\n";
}

void prueba_indices_vectoriales() {
    const std::string csv = ".build/sql_test_ivf.csv";
    {
        std::ofstream f(csv);
        f << "id,v,nombre\n";
        for (int i = 1; i <= 120; ++i) f << i << ",\"VECTOR(" << i << ",1,0)\",p" << i << "\n";
    }
    {
        Catalogo catalogo(DB);
        Ejecutor e(catalogo);
        e.ejecutar("CREATE TABLE fotos (id INT PRIMARY KEY, v VECTOR(3), nombre VARCHAR(8))");
        Resultado r = e.ejecutar("CREATE INDEX fotos_hnsw ON fotos (v) USING HNSW WITH (M = 16, metrica = 'cosine')");
        assert(r.tipo == "create_index" && r.afectadas == 0 && tiene_paso(r, "registrar_indice"));
        assert(detalle_paso(r, "registrar_indice", "M") == "16" && detalle_paso(r, "registrar_indice", "metrica") == "coseno");
        assert(detalle_paso(r, "registrar_indice", "ef_busqueda") == "64" && "lo que no se da toma el valor por omision");
        assert(!std::filesystem::exists(detalle_paso(r, "registrar_indice", "archivo")) && "el motor no construye el indice");

        assert(falla_ejecucion(e, "CREATE INDEX fotos_ivf ON fotos (v) USING IVF") && "el IVF necesita nlist filas para entrenar");
        e.ejecutar("COPY fotos FROM FILE '" + csv + "'");
        assert(e.ejecutar("SELECT COUNT(*) FROM fotos").filas[0][0].entero == 120 && "el HNSW no frena el COPY");
        r = e.ejecutar("CREATE INDEX fotos_ivf ON fotos (v) USING IVF WITH (NLIST = 10, nprobe = 2, metrica = l2)");
        assert(r.afectadas == 120 && detalle_paso(r, "registrar_indice", "nlist") == "10");
        assert(detalle_paso(r, "registrar_indice", "metrica") == "euclidiana");

        assert(falla_ejecucion(e, "CREATE INDEX otro ON fotos (v) USING HNSW") && "ya hay un HNSW sobre v");
        assert(falla_ejecucion(e, "CREATE INDEX otro ON fotos (nombre) USING HNSW") && "solo sobre VECTOR");
        assert(falla_ejecucion(e, "CREATE INDEX otro ON fotos (v) USING RTREE"));
        e.ejecutar("CREATE TABLE fotos2 (id INT PRIMARY KEY, v VECTOR(3))");
        assert(falla_ejecucion(e, "CREATE INDEX x ON fotos2 (v) USING HNSW WITH (nlist = 4)") && "nlist es del IVF");
        assert(falla_ejecucion(e, "CREATE INDEX x ON fotos2 (v) USING HNSW WITH (M = 0)"));
        assert(falla_ejecucion(e, "CREATE INDEX x ON fotos2 (v) USING HNSW WITH (M = 2.5)"));
        assert(falla_ejecucion(e, "CREATE INDEX x ON fotos2 (v) USING HNSW WITH (M = 8, m = 9)"));
        assert(falla_ejecucion(e, "CREATE INDEX x ON fotos2 (v) USING HNSW WITH (umbral_reconstruccion = 1.5)"));
        assert(falla_ejecucion(e, "CREATE INDEX x ON fotos2 (v) USING HNSW WITH (metrica = haversine)"));
        e.ejecutar("DROP TABLE fotos2");

        // el motor no abre los IVF y HNSW: INSERT, DELETE y las consultas siguen como antes
        e.ejecutar("INSERT INTO fotos VALUES (500, VECTOR(1, 2, 3), 'nueva')");
        e.ejecutar("DELETE FROM fotos WHERE id <= 10");
        assert(e.ejecutar("SELECT COUNT(*) FROM fotos").filas[0][0].entero == 111);
        assert(e.ejecutar("SELECT nombre FROM fotos ORDER BY distancia(v, VECTOR(1, 2, 3)) LIMIT 1").filas[0][0].texto == "nueva");
        e.ejecutar("CREATE INDEX fotos_nombre ON fotos (nombre) USING HASH");
        assert(e.ejecutar("SELECT id FROM fotos WHERE nombre = 'p50'").filas[0][0].entero == 50);
    }
    // los parametros sobreviven a reabrir el catalogo, y DESCRIBE muestra los dos indices de v
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    const Resultado r = e.ejecutar("DESCRIBE fotos");
    assert(r.filas[1][3].texto == "fotos_hnsw (HNSW), fotos_ivf (IVF)" && r.filas[2][3].texto == "fotos_nombre (hash extensible)");
    const motor::sql::Indice* hnsw = catalogo.tabla("fotos").indice_vectorial("v", "HNSW");
    assert(hnsw && hnsw->parametro("M") == "16" && hnsw->parametro("metrica") == "COSENO" && hnsw->parametro("umbral_reconstruccion") == "0.3");
    assert(!catalogo.tabla("fotos").indice_sobre("v") && "las consultas comunes no ven los indices de FAISS");
    const std::string archivo = hnsw->archivo;
    {
        std::ofstream f(archivo);  // lo que dejaría la API
        f << "faiss";
    }
    e.ejecutar("DROP TABLE fotos");
    assert(!std::filesystem::exists(archivo) && "DROP TABLE borra el archivo del indice");
    std::cout << "IVF y HNSW: se registran con sus parametros sin construirse, DESCRIBE, reapertura y DROP TABLE\n";
}

std::string lista_pk(std::vector<long long> pks) {
    std::string s;
    for (std::size_t i = 0; i < pks.size(); ++i) s += (i ? ", " : "") + std::to_string(pks[i]);
    return s;
}

void prueba_similar_to() {
    using motor::vectorial::MetricaVec;
    const std::size_t n = 200, dim = 8;
    std::vector<motor::vectorial::Vector> datos(n, motor::vectorial::Vector(dim));
    unsigned semilla = 11;
    auto azar = [&]() {
        semilla = semilla * 1103515245u + 12345u;
        return static_cast<float>((semilla >> 8) % 20001) / 10000.0f - 1.0f;
    };
    for (auto& v : datos) for (float& x : v) x = azar();
    motor::vectorial::Vector q(dim);
    for (float& x : q) x = azar();
    const std::string csv = ".build/sql_test_similar.csv";
    {
        std::ofstream f(csv);
        f << "id,v,nombre\n";
        for (std::size_t i = 0; i < n; ++i) f << i + 1 << ",\"" << literal_vector(datos[i]) << "\",n" << (i + 1) % 7 << "\n";
    }
    const auto todas = [](long long) { return true; };
    const std::string vq = literal_vector(q);

    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    for (const char* org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        const std::string t = std::string("similar_") + org;
        e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, v VECTOR(8), nombre VARCHAR(4)) USING " + org);
        e.ejecutar("COPY " + t + " FROM FILE '" + csv + "'");

        // sin indice: el k-NN secuencial del motor, con el coseno por omision
        Resultado r = e.ejecutar("SELECT id, SIMILARITY_SCORE() FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=10)");
        const auto coseno = knn_fuerza_bruta(datos, q, 10, MetricaVec::COSENO, false, todas);
        assert(ids(r) == coseno && tiene_paso(r, "knn_vectorial") && r.columnas[1] == "score");
        for (std::size_t i = 0; i < r.filas.size(); ++i) {
            const double d = motor::vectorial::distancia(datos[static_cast<std::size_t>(coseno[i] - 1)], q, MetricaVec::COSENO);
            assert(std::abs(r.filas[i][1].decimal - (1 - d)) < 1e-5 && "el score del coseno es el coseno");
            if (i) assert(r.filas[i - 1][1].decimal >= r.filas[i][1].decimal);
        }
        for (const auto& [nombre, m] : {std::pair<const char*, MetricaVec>{"euclidiana", MetricaVec::EUCLIDIANA},
                                        {"producto_punto", MetricaVec::PRODUCTO_PUNTO}}) {
            r = e.ejecutar("SELECT id, SIMILARITY_SCORE() AS s FROM " + t + " WHERE SIMILAR_TO(v, " + vq + ", k=5) WITH METRIC=" + nombre);
            const auto esperado = knn_fuerza_bruta(datos, q, 5, m, false, todas);
            assert(ids(r) == esperado && r.columnas[1] == "s");
            const double d = motor::vectorial::distancia(datos[static_cast<std::size_t>(esperado[0] - 1)], q, m);
            assert(std::abs(r.filas[0][1].decimal - (m == MetricaVec::EUCLIDIANA ? 1 / (1 + d) : -d)) < 1e-4);
        }

        // ORDER BY y LIMIT se aplican sobre los k vecinos
        std::vector<long long> invertido(coseno.rbegin(), coseno.rend());
        assert(ids(e.ejecutar("SELECT id, SIMILARITY_SCORE() FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=10) ORDER BY score ASC")) == invertido);
        assert(ids(e.ejecutar("SELECT id, SIMILARITY_SCORE() AS s FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=10) ORDER BY s DESC LIMIT 3")) ==
               std::vector<long long>(coseno.begin(), coseno.begin() + 3));
        std::vector<long long> por_id = coseno;
        std::sort(por_id.begin(), por_id.end());
        assert(ids(e.ejecutar("SELECT id FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=10) ORDER BY id")) == por_id);
        assert(e.ejecutar("SELECT COUNT(*) FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=7)").filas[0][0].entero == 7);

        // con WHERE: primero el filtro y despues los k mas cercanos entre los que pasan
        r = e.ejecutar("SELECT id FROM " + t + " WHERE id > 100 AND SIMILAR_TO(" + vq + ", k=4)");
        assert(ids(r) == knn_fuerza_bruta(datos, q, 4, MetricaVec::COSENO, false, [](long long id) { return id > 100; }));
        assert(tiene_paso(r, "top_k_vectorial") && detalle_paso(r, "top_k_vectorial", "entrada") == "100");

        // USING HNSW: la API pone los candidatos de FAISS; el motor los lee y recalcula la distancia
        assert(falla_ejecucion(e, "SELECT id FROM " + t + " WHERE SIMILAR_TO(" + vq + ") USING HNSW") && "no hay indice");
        e.ejecutar("CREATE INDEX " + t + "_hnsw ON " + t + " (v) USING HNSW");
        assert(falla_ejecucion(e, "SELECT id FROM " + t + " WHERE SIMILAR_TO(" + vq + ") USING HNSW") && "sin la API no hay candidatos");
        assert(falla_ejecucion(e, "SELECT id FROM " + t + " WHERE SIMILAR_TO(" + vq + ") USING HNSW CANDIDATES (1) WITH METRIC=euclidiana") &&
               "el indice es de coseno");
        std::vector<long long> candidatos = {coseno[3], coseno[0], 99999, coseno[2], coseno[1]};  // desordenados y uno que no existe
        r = e.ejecutar("SELECT id, SIMILARITY_SCORE() FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=4) USING HNSW CANDIDATES (" + lista_pk(candidatos) + ")");
        assert(ids(r) == std::vector<long long>(coseno.begin(), coseno.begin() + 4) && tiene_paso(r, "knn_vectorial_indice"));
        assert(detalle_paso(r, "knn_vectorial_indice", "candidatos") == "5" && detalle_paso(r, "knn_vectorial_indice", "estructura") == "hnsw_faiss");
        candidatos.push_back(coseno[4]);
        r = e.ejecutar("SELECT id FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=3) USING HNSW CANDIDATES (" + lista_pk(candidatos) + ")");
        assert(ids(r) == std::vector<long long>(coseno.begin(), coseno.begin() + 3) && "mas candidatos que k: se queda con k");
        // post-filtro: el WHERE se aplica a los candidatos, asi que pueden quedar menos de k
        const std::vector<long long> cuatro(coseno.begin(), coseno.begin() + 4);
        std::vector<long long> ordenados = cuatro;
        std::sort(ordenados.begin(), ordenados.end());
        const long long corte = ordenados[1];  // pasan los dos de pk mas alta
        std::vector<long long> pasan;
        for (long long id : cuatro) if (id > corte) pasan.push_back(id);
        r = e.ejecutar("SELECT id FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=4) AND id > " + std::to_string(corte) +
                       " USING HNSW CANDIDATES (" + lista_pk(cuatro) + ")");
        assert(ids(r) == pasan && pasan.size() == 2 && tiene_paso(r, "filtro"));

        // EXPLAIN: Seq Vector Scan sin indice, Vector Scan using ... con indice (sin ejecutar, no necesita candidatos)
        std::string plan = texto_del_plan(e.ejecutar("EXPLAIN SELECT * FROM " + t + " WHERE SIMILAR_TO(" + vq + ", k=10)"));
        assert(plan.find("Seq Vector Scan on " + t + " (coseno)") != std::string::npos);
        assert(plan.find("Order By: SIMILAR_TO(v, VECTOR(8 dimensiones), k=10) [coseno]") != std::string::npos);
        plan = texto_del_plan(e.ejecutar("EXPLAIN SELECT * FROM " + t + " WHERE SIMILAR_TO(" + vq + ") USING HNSW"));
        assert(plan.find("Vector Scan using " + t + "_hnsw on " + t + " (HNSW, coseno, ef=64)") != std::string::npos);
        plan = texto_del_plan(e.ejecutar("EXPLAIN SELECT * FROM " + t + " WHERE id > 3 AND SIMILAR_TO(" + vq + ")"));
        assert(plan.find("Top-k") != std::string::npos);

        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE SIMILAR_TO('foto.jpg')") && "la imagen la convierte la API");
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE SIMILAR_TO(VECTOR(1, 2))") && "dimension distinta");
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE SIMILAR_TO(nombre, " + vq + ")"));
        assert(falla_ejecucion(e, "DELETE FROM " + t + " WHERE SIMILAR_TO(" + vq + ")"));
        e.ejecutar("DROP TABLE " + t);
    }

    // con dos columnas VECTOR hay que decir cual
    e.ejecutar("CREATE TABLE dos_vectores (id INT PRIMARY KEY, a VECTOR(2), b VECTOR(2))");
    e.ejecutar("INSERT INTO dos_vectores VALUES (1, VECTOR(1, 0), VECTOR(0, 1))");
    e.ejecutar("INSERT INTO dos_vectores VALUES (2, VECTOR(0, 1), VECTOR(1, 0))");
    assert(falla_ejecucion(e, "SELECT id FROM dos_vectores WHERE SIMILAR_TO(VECTOR(1, 0), k=1)"));
    assert(e.ejecutar("SELECT id FROM dos_vectores WHERE SIMILAR_TO(b, VECTOR(1, 0), k=1)").filas[0][0].entero == 2);
    // un heap con indice hash sobre la clave lee los candidatos por el indice
    e.ejecutar("CREATE INDEX dos_id ON dos_vectores (id) USING HASH");
    e.ejecutar("CREATE INDEX dos_hnsw ON dos_vectores (a) USING HNSW");
    const Resultado r = e.ejecutar("SELECT id FROM dos_vectores WHERE SIMILAR_TO(a, VECTOR(0, 1), k=2) USING HNSW CANDIDATES (1, 2)");
    assert(ids(r) == std::vector<long long>({2, 1}));
    e.ejecutar("DROP TABLE dos_vectores");
    std::cout << "SIMILAR_TO: k-NN secuencial igual a la fuerza bruta en las tres organizaciones, SIMILARITY_SCORE,\n"
                 "          ORDER BY y LIMIT sobre los k, WHERE + Top-k, CANDIDATES de FAISS con post-filtro y EXPLAIN\n";
}

struct Cambio {
    char op;
    int pk;
    std::vector<float> v;
};

// lee el archivo que consume la API: registros [uint8 op][int32 pk][dim float32]
std::vector<Cambio> leer_cambios(const std::string& ruta, std::size_t dim) {
    std::vector<Cambio> salida;
    std::ifstream f(ruta, std::ios::binary);
    Cambio c;
    while (f.get(c.op)) {
        c.v.assign(dim, 0.0f);
        f.read(reinterpret_cast<char*>(&c.pk), sizeof(c.pk));
        f.read(reinterpret_cast<char*>(c.v.data()), static_cast<std::streamsize>(dim * sizeof(float)));
        assert(f && "registro incompleto");
        salida.push_back(c);
    }
    return salida;
}

std::string ops(const std::vector<Cambio>& cambios) {
    std::string s;
    for (const Cambio& c : cambios) s += std::string(1, c.op) + std::to_string(c.pk) + " ";
    return s;
}

void prueba_cambios_vectoriales() {
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    e.ejecutar("CREATE TABLE galeria (id INT PRIMARY KEY, v VECTOR(2), nombre VARCHAR(8))");
    e.ejecutar("INSERT INTO galeria VALUES (1, VECTOR(1, 0), 'a')");
    e.ejecutar("INSERT INTO galeria VALUES (2, VECTOR(0, 1), 'b')");
    e.ejecutar("INSERT INTO galeria VALUES (3, VECTOR(1, 1), 'c')");

    // CREATE INDEX vuelca la tabla entera: la API construye el indice con eso
    Resultado r = e.ejecutar("CREATE INDEX galeria_hnsw ON galeria (v) USING HNSW");
    const std::string archivo = catalogo.tabla("galeria").indice_vectorial("v", "HNSW")->archivo_cambios();
    std::vector<Cambio> cambios = leer_cambios(archivo, 2);
    assert(ops(cambios) == "I1 I2 I3 " && cambios[2].v == std::vector<float>({1, 1}));
    assert(detalle_paso(r, "registrar_indice", "archivo_cambios") == archivo);

    r = e.ejecutar("INSERT INTO galeria VALUES (4, VECTOR(0.5, 2), 'd')");
    assert(detalle_paso(r, "insertar", "vectores_para_faiss") == "1");
    r = e.ejecutar("DELETE FROM galeria WHERE id <= 2");
    assert(detalle_paso(r, "eliminar", "vectores_para_faiss") == "2");
    cambios = leer_cambios(archivo, 2);
    assert(ops(cambios) == "I1 I2 I3 I4 D1 D2 " && cambios[3].v == std::vector<float>({0.5f, 2}));
    assert(cambios[4].v == std::vector<float>({0, 0}) && "un borrado no necesita el vector");

    // en una transaccion no se escribe nada hasta el COMMIT, y con ROLLBACK nunca
    e.ejecutar("BEGIN");
    e.ejecutar("INSERT INTO galeria VALUES (10, VECTOR(3, 3), 'x')");
    e.ejecutar("DELETE FROM galeria WHERE id = 3");
    assert(leer_cambios(archivo, 2).size() == 6);
    e.ejecutar("ROLLBACK");
    assert(leer_cambios(archivo, 2).size() == 6 && "el ROLLBACK no deja rastro para FAISS");
    assert(e.ejecutar("SELECT COUNT(*) FROM galeria").filas[0][0].entero == 2);
    e.ejecutar("BEGIN");
    r = e.ejecutar("INSERT INTO galeria VALUES (11, VECTOR(4, 4), 'y')");
    assert(detalle_paso(r, "insertar", "vectores_para_faiss") == "1 (al confirmar)");
    e.ejecutar("DELETE FROM galeria WHERE id = 3");
    assert(leer_cambios(archivo, 2).size() == 6);
    e.ejecutar("COMMIT");
    assert(ops(leer_cambios(archivo, 2)) == "I1 I2 I3 I4 D1 D2 I11 D3 " && "se escriben en el orden de la transaccion");

    // COPY anota cada fila; si falla a la mitad, anota las que alcanzaron a entrar
    const std::string csv = ".build/sql_test_galeria.csv";
    {
        std::ofstream f(csv);
        f << "id,v,nombre\n20,\"VECTOR(2,0)\",e\n21,\"VECTOR(0,2)\",f\n";
    }
    e.ejecutar("COPY galeria FROM FILE '" + csv + "'");
    {
        std::ofstream f(csv);
        f << "id,v,nombre\n30,\"VECTOR(2,2)\",g\n4,\"VECTOR(1,2)\",repetida\n31,\"VECTOR(3,0)\",h\n";
    }
    assert(falla_ejecucion(e, "COPY galeria FROM FILE '" + csv + "'") && "la pk 4 ya existe");
    assert(ops(leer_cambios(archivo, 2)) == "I1 I2 I3 I4 D1 D2 I11 D3 I20 I21 I30 ");
    assert(e.ejecutar("SELECT COUNT(*) FROM galeria").filas[0][0].entero == 5);

    // con dos indices sobre la columna, cada uno tiene su archivo; una tabla sin IVF ni HNSW no escribe nada
    e.ejecutar("CREATE INDEX galeria_ivf ON galeria (v) USING IVF WITH (nlist = 2)");
    const std::string archivo_ivf = catalogo.tabla("galeria").indice_vectorial("v", "IVF")->archivo_cambios();
    std::vector<int> volcadas;  // en el orden fisico del heap, que reusa espacio de las borradas
    for (const Cambio& c : leer_cambios(archivo_ivf, 2)) volcadas.push_back(c.op == 'I' ? c.pk : -1);
    std::sort(volcadas.begin(), volcadas.end());
    assert(volcadas == std::vector<int>({4, 11, 20, 21, 30}));
    e.ejecutar("DELETE FROM galeria WHERE id = 20");
    cambios = leer_cambios(archivo_ivf, 2);
    assert(cambios.size() == 6 && cambios.back().op == 'D' && cambios.back().pk == 20);
    assert(ops(leer_cambios(archivo, 2)) == "I1 I2 I3 I4 D1 D2 I11 D3 I20 I21 I30 D20 ");
    e.ejecutar("CREATE TABLE sin_indice (id INT PRIMARY KEY, v VECTOR(2))");
    e.ejecutar("INSERT INTO sin_indice VALUES (1, VECTOR(1, 1))");
    for (const auto& entrada : std::filesystem::directory_iterator(DB)) {
        assert(entrada.path().filename().string().rfind("sin_indice", 0) != 0 || entrada.path().extension() != ".cambios");
    }
    e.ejecutar("DROP TABLE sin_indice");

    e.ejecutar("DROP TABLE galeria");
    assert(!std::filesystem::exists(archivo) && !std::filesystem::exists(archivo_ivf) && "DROP TABLE borra los archivos de cambios");
    std::cout << "cambios para FAISS: CREATE INDEX vuelca la tabla, INSERT, DELETE y COPY anotan;\n"
                 "          en transaccion se escriben con COMMIT y se descartan con ROLLBACK\n";
}

void prueba_espacial() {
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    const std::string centro = "POINT(-12.0464, -77.0428)";
    const std::string rectangulo = "POLYGON((-12.13 -77.05, -12.13 -77.02, -12.09 -77.02, -12.09 -77.05))";
    const std::string triangulo = "POLYGON((-12.13 -77.05, -12.13 -77.02, -12.09 -77.05))";
    const std::string csv = ".build/sql_test_tiendas.csv";
    {
        std::ofstream f(csv);
        f << "id,nombre,ubicacion\n4,Barranco,-12.1494 -77.0219\n5,Callao,\"POINT(-12.0566 -77.1181)\"\n";
    }
    for (const std::string org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        const std::string t = "tiendas_" + org;
        e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, nombre VARCHAR(20), ubicacion POINT) USING " + org);
        e.ejecutar("INSERT INTO " + t + " VALUES (1, 'Centro', " + centro + ")");
        e.ejecutar("INSERT INTO " + t + " VALUES (2, 'Miraflores', POINT(-12.1211, -77.0297))");
        e.ejecutar("INSERT INTO " + t + " VALUES (3, 'San Isidro', POINT(-12.0977, -77.0365))");
        assert(e.ejecutar("COPY " + t + " FROM FILE '" + csv + "'").afectadas == 2);

        Resultado r = e.ejecutar("SELECT ubicacion FROM " + t + " WHERE id = 5");
        assert(r.filas[0][0].es_punto && r.filas[0][0].a_texto() == "POINT(-12.056600, -77.118100)");

        r = e.ejecutar("SELECT nombre FROM " + t + " WHERE distancia(ubicacion, " + centro + ") < 6000 ORDER BY nombre");
        assert(nombres(r) == std::vector<std::string>({"Centro", "San Isidro"}));
        r = e.ejecutar("SELECT nombre FROM " + t + " WHERE distancia(ubicacion, " + centro + ") >= 9000 ORDER BY nombre");
        assert(nombres(r) == std::vector<std::string>({"Barranco"}));

        r = e.ejecutar("SELECT nombre FROM " + t + " ORDER BY distancia(ubicacion, " + centro + ") LIMIT 3");
        assert(nombres(r) == std::vector<std::string>({"Centro", "San Isidro", "Callao"}));
        r = e.ejecutar("SELECT nombre FROM " + t + " ORDER BY distancia(ubicacion, " + centro + ") DESC LIMIT 1");
        assert(nombres(r) == std::vector<std::string>({"Barranco"}));
        r = e.ejecutar("SELECT nombre FROM " + t + " ORDER BY distancia(ubicacion, " + centro + ", 'euclidiana') LIMIT 3");
        assert(nombres(r).size() == 3 && nombres(r)[0] == "Centro");

        r = e.ejecutar("SELECT nombre FROM " + t + " WHERE dentro(ubicacion, " + rectangulo + ") ORDER BY nombre");
        assert(nombres(r) == std::vector<std::string>({"Miraflores", "San Isidro"}));
        r = e.ejecutar("SELECT nombre FROM " + t + " WHERE dentro(ubicacion, " + triangulo + ")");
        assert(nombres(r) == std::vector<std::string>({"Miraflores"}));
        r = e.ejecutar("SELECT nombre FROM " + t +
                       " WHERE dentro(ubicacion, POLYGON((-12.0464 -77.0428, -12.04 -77.0428, -12.0464 -77.03)))");
        assert(nombres(r) == std::vector<std::string>({"Centro"}));
        assert(e.ejecutar("SELECT nombre FROM " + t + " WHERE dentro(ubicacion, POLYGON((10 10, 10 11, 11 11)))").filas.empty());
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE dentro(nombre, " + rectangulo + ")"));

        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE ubicacion = 3"));
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " WHERE distancia(nombre, " + centro + ") < 10"));
        assert(falla_ejecucion(e, "SELECT * FROM " + t + " ORDER BY ubicacion"));
        assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (9, 'Mal', POINT(95, 0))"));
        assert(falla_ejecucion(e, "INSERT INTO " + t + " VALUES (9, 'Mal', 7)"));
    }

    Resultado r = e.ejecutar("EXPLAIN SELECT nombre FROM tiendas_HEAP WHERE distancia(ubicacion, " + centro + ") < 6000");
    assert(texto_del_plan(r).find("Filter: distancia(ubicacion, POINT(-12.046400, -77.042800)) [haversine] < 6000") != std::string::npos);
    assert(r.plan.back().operacion == "scan_completo");
    r = e.ejecutar("EXPLAIN SELECT nombre FROM tiendas_HEAP ORDER BY distancia(ubicacion, " + centro + ") LIMIT 2");
    assert(texto_del_plan(r).find("Sort Key: distancia(ubicacion,") != std::string::npos);
    assert(e.ejecutar("DESCRIBE tiendas_HEAP").filas[2][1].texto == "POINT");
    assert(falla_ejecucion(e, "CREATE TABLE malo (u POINT PRIMARY KEY, id INT)"));
    assert(falla_ejecucion(e, "CREATE INDEX g ON tiendas_HEAP (nombre) USING RTREE"));
    assert(falla_ejecucion(e, "CREATE INDEX g ON tiendas_HEAP (ubicacion) USING HASH"));

    const std::string radio = "SELECT nombre FROM tiendas_HEAP WHERE distancia(ubicacion, " + centro + ") < 6000 ORDER BY nombre";
    const std::string knn = "SELECT nombre FROM tiendas_HEAP ORDER BY distancia(ubicacion, " + centro + ") LIMIT 3";
    const std::vector<std::string> sin_indice_radio = nombres(e.ejecutar(radio));
    const std::vector<std::string> sin_indice_knn = nombres(e.ejecutar(knn));
    assert(e.ejecutar("CREATE INDEX idx_geo ON tiendas_HEAP (ubicacion) USING RTREE").afectadas == 5);
    assert(e.ejecutar("CREATE INDEX idx_geo ON tiendas_SEQUENTIAL (ubicacion) USING RTREE").afectadas == 5);
    r = e.ejecutar("SELECT nombre FROM tiendas_SEQUENTIAL WHERE distancia(ubicacion, " + centro + ") < 6000 ORDER BY nombre");
    assert(nombres(r) == sin_indice_radio && tiene_paso(r, "rango_espacial"));

    r = e.ejecutar(radio);
    assert(nombres(r) == sin_indice_radio && tiene_paso(r, "rango_espacial"));
    assert(!detalle_paso(r, "rango_espacial", "nodos_visitados").empty());
    r = e.ejecutar(knn);
    assert(nombres(r) == sin_indice_knn && tiene_paso(r, "knn_espacial"));
    r = e.ejecutar("SELECT nombre FROM tiendas_HEAP WHERE dentro(ubicacion, " + rectangulo + ") ORDER BY nombre");
    assert(nombres(r) == std::vector<std::string>({"Miraflores", "San Isidro"}) && tiene_paso(r, "poligono_espacial"));
    r = e.ejecutar("SELECT nombre FROM tiendas_HEAP WHERE dentro(ubicacion, " + triangulo + ")");
    assert(nombres(r) == std::vector<std::string>({"Miraflores"}) && tiene_paso(r, "poligono_espacial"));
    r = e.ejecutar("SELECT nombre FROM tiendas_HEAP WHERE dentro(ubicacion, POLYGON((-12.0464 -77.0428, -12.04 -77.0428, -12.0464 -77.03)))");
    assert(nombres(r) == std::vector<std::string>({"Centro"}) && tiene_paso(r, "poligono_espacial"));
    assert(texto_del_plan(e.ejecutar("EXPLAIN " + knn)).find("Index Scan using idx_geo on tiendas_HEAP (ubicacion)") != std::string::npos);
    r = e.ejecutar("SELECT nombre FROM tiendas_HEAP WHERE id > 0 ORDER BY distancia(ubicacion, " + centro + ") LIMIT 3");
    assert(!tiene_paso(r, "knn_espacial") && nombres(r) == sin_indice_knn);

    e.ejecutar("INSERT INTO tiendas_HEAP VALUES (6, 'Cercado', POINT(-12.0470, -77.0430))");
    assert(nombres(e.ejecutar(knn))[1] == "Cercado");
    e.ejecutar("DELETE FROM tiendas_HEAP WHERE id = 6");
    assert(nombres(e.ejecutar(knn)) == sin_indice_knn);
    {
        Catalogo otro(DB);
        Ejecutor e2(otro);
        r = e2.ejecutar(knn);
        assert(tiene_paso(r, "knn_espacial") && nombres(r) == sin_indice_knn);
    }

    for (const std::string org : {"HEAP", "SEQUENTIAL", "BPLUS"}) e.ejecutar("DROP TABLE tiendas_" + org);
    std::filesystem::remove(csv);
    std::cout << "espacial: POINT, radio por distancia, k vecinos y validaciones en las tres organizaciones;\n"
              << "          poligonos con ray casting; con R-Tree el radio, el k-NN y el poligono dan lo mismo que sin indice\n";
}

std::vector<std::string> filas_texto(const Resultado& r) {
    std::vector<std::string> salida;
    for (const auto& fila : r.filas) {
        std::string s;
        for (const auto& v : fila) s += v.a_texto() + "|";
        salida.push_back(s);
    }
    return salida;
}

std::string con_tabla(std::string sql, const std::string& t) {
    return sql.replace(sql.find("{t}"), 3, t);
}

void prueba_matriz_indices() {
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    const std::vector<std::string> columnas = {"num", "texto", "punto"};
    const std::vector<std::vector<std::string>> consultas = {
        {"SELECT id FROM {t} WHERE num = 3 ORDER BY id",
         "SELECT id FROM {t} WHERE num BETWEEN 2 AND 4 ORDER BY id",
         "SELECT num FROM {t} ORDER BY num"},
        {"SELECT id FROM {t} WHERE texto = 'abcd1' ORDER BY id",
         "SELECT id FROM {t} WHERE texto BETWEEN 'abcd2' AND 'abcd4' ORDER BY id",
         "SELECT id FROM {t} WHERE texto >= 'zeta3' ORDER BY id",
         "SELECT texto FROM {t} ORDER BY texto"},
        {"SELECT id FROM {t} WHERE distancia(punto, POINT(-12.1, -77.1)) < 8000 ORDER BY id",
         "SELECT id FROM {t} ORDER BY distancia(punto, POINT(-12.1, -77.1004)) LIMIT 3"}};
    // plan esperado: {bplus o rtree, hash}
    const std::vector<std::vector<std::array<std::string, 2>>> planes = {
        {{"busqueda_por_indice", "busqueda_por_indice"}, {"rango_por_indice", "scan_completo"}, {"orden_por_indice", "ordenamiento"}},
        {{"busqueda_por_indice", "busqueda_por_indice"}, {"", ""}, {"rango_por_indice", "scan_completo"}, {"orden_por_indice", "ordenamiento"}},
        {{"rango_espacial", ""}, {"knn_espacial", ""}}};

    for (const std::string org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        for (const std::string idx : {"BPLUS", "HASH", "RTREE"}) {
            const std::string t = "m_" + org + "_" + idx;
            e.ejecutar("CREATE TABLE " + t + " (id INT PRIMARY KEY, num INT, texto VARCHAR(12), punto POINT) USING " + org);
            for (int i = 1; i <= 40; ++i) {
                char punto[48];
                std::snprintf(punto, sizeof(punto), "POINT(%.2f, %.2f)", -12.0 - 0.01 * i, -77.0 - 0.01 * i);
                e.ejecutar("INSERT INTO " + t + " VALUES (" + std::to_string(i) + ", " + std::to_string(i % 8) + ", '" +
                           (i % 2 ? "abcd" : "zeta") + std::to_string(i % 7) + "', " + punto + ")");
            }
            for (std::size_t k = 0; k < columnas.size(); ++k) {
                const std::string crear = "CREATE INDEX i_" + columnas[k] + " ON " + t + " (" + columnas[k] + ") USING " + idx;
                if (idx == "RTREE" ? columnas[k] != "punto" : columnas[k] == "punto") {
                    assert(falla_ejecucion(e, crear));
                    continue;
                }
                std::vector<std::vector<std::string>> esperado;
                for (const std::string& q : consultas[k]) esperado.push_back(filas_texto(e.ejecutar(con_tabla(q, t))));
                assert(!esperado[0].empty());
                assert(e.ejecutar(crear).afectadas == 40);

                const std::size_t j = idx == "HASH" ? 1 : 0;
                for (std::size_t q = 0; q < consultas[k].size(); ++q) {
                    const Resultado r = e.ejecutar(con_tabla(consultas[k][q], t));
                    assert(filas_texto(r) == esperado[q]);
                    assert(planes[k][q][j].empty() || tiene_paso(r, planes[k][q][j]));
                }

                // insert y delete con el indice
                const int borrar = std::stoi(esperado[0][0]);
                const std::string nuevo = std::to_string(41 + k);
                e.ejecutar("INSERT INTO " + t + " VALUES (" + nuevo + ", 3, 'abcd1', POINT(-12.1, -77.1))");
                e.ejecutar("DELETE FROM " + t + " WHERE id = " + std::to_string(borrar));
                esperado[0].erase(esperado[0].begin());
                esperado[0].push_back(nuevo + "|");
                assert(filas_texto(e.ejecutar(con_tabla(consultas[k][0], t))) == esperado[0]);
            }
        }
    }
    std::cout << "matriz de indices: HEAP, SEQUENTIAL y BPLUS x INT, VARCHAR y POINT x BPLUS, HASH y RTREE;\n"
              << "          lo valido da lo mismo que sin indice y se mantiene con INSERT y DELETE; lo demas falla\n";
}

}  // namespace

int main() {
    std::filesystem::create_directories(".build");
    std::filesystem::remove_all(DB);
    prueba_parser();
    escribir_csv();
    {
        Catalogo catalogo(DB);
        Ejecutor e(catalogo);
        for (const char* org : {"HEAP", "SEQUENTIAL", "BPLUS"}) prueba_organizacion(e, org);
        prueba_indice_secundario(e);
        prueba_indice_hash(e);
        prueba_explain(e);
        prueba_copy(e);
        prueba_carga_secuencial(e);
    }
    prueba_persistencia();
    prueba_tabla_manual();
    prueba_pk_no_primera();
    prueba_float();
    prueba_vector();
    prueba_knn_vectorial();
    prueba_indices_vectoriales();
    prueba_cambios_vectoriales();
    prueba_similar_to();
    prueba_espacial();
    prueba_matriz_indices();
    prueba_busqueda_texto();
    prueba_indice_spimi();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
