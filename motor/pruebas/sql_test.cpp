#include "../consultas/catalogo.h"
#include "../consultas/ejecutor.h"
#include "../consultas/parser_sql.h"

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

void prueba_busqueda_texto() {
    const std::string db_texto = ".build/db_sql_text_test";
    std::filesystem::remove_all(db_texto);
    Catalogo catalogo(db_texto);
    Ejecutor e(catalogo);
    e.ejecutar("CREATE TABLE texto_docs (id INT PRIMARY KEY, contenido VARCHAR(200))");
    e.ejecutar("INSERT INTO texto_docs VALUES (1, 'Running shoes with waterproof fabric')");
    e.ejecutar("INSERT INTO texto_docs VALUES (2, 'Trail shoes for running and hiking')");
    e.ejecutar("INSERT INTO texto_docs VALUES (3, 'Stainless steel water bottle')");
    Resultado r = e.ejecutar(
        "SELECT id, SCORE() AS relevancia FROM texto_docs "
        "WHERE MATCH(contenido, 'running shoes') USING BM25 ORDER BY relevancia DESC LIMIT 10");
    assert(r.filas.size() == 2 && r.columnas.size() == 2);
    assert(r.columnas[1] == "relevancia" && r.filas[0][1].es_decimal && r.filas[0][1].decimal > 0);
    assert(tiene_paso(r, "busqueda_texto"));
    r = e.ejecutar("SELECT * FROM texto_docs WHERE MATCH(contenido, 'water bottle') USING TF_IDF LIMIT 10");
    assert(r.filas.size() == 1 && r.filas[0][0].entero == 3);
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
    prueba_espacial();
    prueba_matriz_indices();
    prueba_busqueda_texto();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
