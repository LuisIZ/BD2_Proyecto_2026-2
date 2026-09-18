#include "../consultas/catalogo.h"
#include "../consultas/ejecutor.h"
#include "../consultas/parser_sql.h"

#include <cassert>
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
const std::string CSV_DEC = ".build/sql_test_decadas.csv";

bool falla(const std::string& sql) {
    try {
        motor::sql::parsear(sql);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
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
        for (const auto& d : p.detalles) if (d.first == clave) return d.second;
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

    s = motor::sql::parsear("SELECT orgs.Name, dec.Decada FROM orgs JOIN dec ON orgs.Founded = dec.Founded");
    assert(s.tabla == "orgs" && s.joins.size() == 1 && s.joins[0].tabla_derecha == "dec");
    assert(s.joins[0].izq_calificador == "orgs" && s.joins[0].izq_columna == "Founded");
    assert(s.joins[0].der_calificador == "dec" && s.joins[0].der_columna == "Founded");
    assert(s.items[0].calificador == "orgs" && s.items[0].columna == "Name");
    assert(s.items[0].etiqueta_calificada() == "orgs.Name");

    s = motor::sql::parsear("SELECT * FROM a INNER JOIN b ON k = k WHERE a.x = 1 ORDER BY b.y DESC");
    assert(s.joins.size() == 1 && s.joins[0].tipo == "INNER" && s.joins[0].izq_calificador.empty());
    assert(s.condiciones[0].calificador == "a" && s.condiciones[0].nombre_completo() == "a.x");
    assert(s.order_by_calificador == "b" && s.order_by == "y" && s.descendente);

    s = motor::sql::parsear("SELECT COUNT(o.Index) FROM o JOIN d ON o.k = d.k GROUP BY d.Zona");
    assert(s.items[0].agregado == "COUNT" && s.items[0].calificador == "o");
    assert(s.group_by_calificador == "d" && s.group_by == "Zona");

    assert(motor::sql::parsear("SELECT t.a FROM t").items[0].calificador == "t");
    assert(motor::sql::parsear("SELECT a FROM t").joins.empty());

    assert(falla("SELECT * FROM a LEFT JOIN b ON a.k = b.k"));
    assert(falla("SELECT * FROM a JOIN b"));
    assert(falla("SELECT * FROM a JOIN b ON a.k > b.k"));
    assert(falla("SELECT * FROM a JOIN b ON a.k = b.k AND a.j = b.j"));
    assert(falla("SELECT * FROM a INNER b ON a.k = b.k"));
    assert(falla("SELECT a. FROM t"));

    assert(falla("SELECT FROM t"));
    assert(falla("SELECT * t"));
    assert(falla("SELECT * FROM t WHERE"));
    assert(falla("SELECT * FROM t WHERE a = 1.5"));
    assert(falla("SELECT * FROM t WHERE a = 1.5.6"));
    assert(falla("INSERT INTO t VALUES (1, 'abierto)"));
    assert(falla("CREATE TABLE t (a FLOAT)"));
    assert(falla("SELECT * FROM t WHERE a = 1 extra"));
    assert(falla("UPDATE t SET a = 1"));

    const auto partes = motor::sql::separar_sentencias("SELECT 1; INSERT INTO t VALUES ('a;b') ;\n\n ; DELETE FROM t");
    assert(partes.size() == 3 && partes[1] == "INSERT INTO t VALUES ('a;b')");
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

    r = e.ejecutar("SELECT Index, Name, Country FROM " + t + " WHERE Index = 150");
    assert(r.filas.size() == 1 && r.filas[0][1].texto == "Org, 150" && r.filas[0][2].texto == "Peru");
    assert(tiene_paso(r, org == "HEAP" ? "scan_completo" : "busqueda_por_clave"));
    assert(e.ejecutar("SELECT * FROM " + t + " WHERE Index = 999").filas.empty());

    r = e.ejecutar("SELECT " + t + ".Index, " + t + ".Name FROM " + t + " WHERE " + t + ".Index = 150 ORDER BY " + t + ".Index");
    assert(r.filas.size() == 1 && r.filas[0][1].texto == "Org, 150");
    assert(r.columnas[0] == "Index" && r.columnas[1] == "Name");
    assert(falla_ejecucion(e, "SELECT otra.Index FROM " + t) && "calificador que no es de la consulta");

    r = e.ejecutar("SELECT Index FROM " + t + " WHERE Index BETWEEN 10 AND 30 AND Country = 'Peru'");
    assert(r.filas.size() == 7 && r.filas[0][0].entero == 12);
    assert(tiene_paso(r, "filtro"));
    if (org != "HEAP") assert(tiene_paso(r, "rango_por_clave"));

    r = e.ejecutar("SELECT Index, Employees FROM " + t + " WHERE Founded = 2005 ORDER BY Employees DESC LIMIT 2");
    assert(r.filas.size() == 2 && r.filas[0][0].entero == 285 && r.filas[1][0].entero == 265);
    assert(tiene_paso(r, "ordenamiento") && tiene_paso(r, "limite"));
    r = e.ejecutar("SELECT Country, COUNT(*), SUM(Employees), MIN(Index) FROM " + t + " GROUP BY Country ORDER BY Country");
    assert(r.filas.size() == 3 && r.filas[0][0].texto == "Bolivia" && r.filas[0][1].entero == 100 && r.filas[2][3].entero == 3);
    assert(tiene_paso(r, "agrupacion"));

    // external hashing: la tabla de hash viva es la de una particion, no la de todos
    // los grupos, asi que con 300 claves distintas el pico debe quedar muy por debajo
    r = e.ejecutar("SELECT Index, COUNT(*) FROM " + t + " GROUP BY Index");
    assert(r.filas.size() == 300);
    const int pico = std::stoi(detalle_paso(r, "agrupacion", "grupos_en_memoria"));
    assert(pico > 0 && pico < 100 && "los grupos deben agregarse particion por particion");

    r = e.ejecutar("SELECT COUNT(*) FROM " + t);
    assert(r.filas[0][0].entero == 300);

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

void prueba_indice_secundario(Ejecutor& e) {
    assert(falla_ejecucion(e, "CREATE INDEX i ON org_SEQUENTIAL (Founded)") && "solo sobre heap");
    assert(falla_ejecucion(e, "CREATE INDEX i ON org_HEAP (Country)") && "solo INT");

    Resultado r = e.ejecutar("CREATE INDEX idx_f ON org_HEAP (Founded)");
    assert(r.afectadas == 201);
    r = e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded = 2005 ORDER BY Index");
    assert(tiene_paso(r, "busqueda_por_indice") && r.filas.size() == 10 && r.filas[0][0].entero == 105);
    r = e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded BETWEEN 2018 AND 2019");
    assert(tiene_paso(r, "rango_por_indice") && r.filas.size() == 20);

    e.ejecutar("INSERT INTO org_HEAP VALUES (400, 'Idx', 'Peru', 2005, 1)");
    assert(e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded = 2005").filas.size() == 11);
    e.ejecutar("DELETE FROM org_HEAP WHERE Index = 400");
    assert(e.ejecutar("SELECT Index FROM org_HEAP WHERE Founded = 2005").filas.size() == 10);
    assert(falla_ejecucion(e, "CREATE INDEX otro ON org_HEAP (Founded)"));
    std::cout << "indice secundario: B+ no agrupado sobre heap, uso en igualdad y rango, mantenimiento\n";
}

void prueba_indice_hash(Ejecutor& e) {
    const std::size_t por_igualdad = e.ejecutar("SELECT Index FROM org_HEAP WHERE Employees = 707").filas.size();
    const std::size_t por_rango = e.ejecutar("SELECT Index FROM org_HEAP WHERE Employees BETWEEN 700 AND 1400").filas.size();
    assert(por_igualdad == 1 && por_rango == 100);

    Resultado r = e.ejecutar("CREATE INDEX idx_emp ON org_HEAP (Employees) USING HASH");
    assert(r.afectadas == 201);
    assert(detalle_paso(r, "construir_indice", "estructura") == "hash_extensible");

    r = e.ejecutar("SELECT Index FROM org_HEAP WHERE Employees = 707");
    assert(tiene_paso(r, "busqueda_por_indice") && r.filas.size() == por_igualdad);
    assert(detalle_paso(r, "busqueda_por_indice", "estructura") == "hash_extensible");

    r = e.ejecutar("SELECT Index FROM org_HEAP WHERE Employees BETWEEN 700 AND 1400");
    assert(tiene_paso(r, "scan_completo") && !tiene_paso(r, "rango_por_indice"));
    assert(detalle_paso(r, "scan_completo", "nota").find("no resuelve rangos") != std::string::npos);
    assert(r.filas.size() == por_rango);

    e.ejecutar("INSERT INTO org_HEAP VALUES (500, 'Hash', 'Peru', 2005, 707)");
    assert(e.ejecutar("SELECT Index FROM org_HEAP WHERE Employees = 707").filas.size() == por_igualdad + 1);
    e.ejecutar("DELETE FROM org_HEAP WHERE Index = 500");
    assert(e.ejecutar("SELECT Index FROM org_HEAP WHERE Employees = 707").filas.size() == por_igualdad);
    assert(falla_ejecucion(e, "CREATE INDEX otro ON org_HEAP (Employees) USING HASH") && "ya tiene indice");
    std::cout << "indice hash: igualdad por hash extensible, rango cae a scan, mantenimiento\n";
}

void escribir_csv_decadas() {
    std::ofstream f(CSV_DEC);
    f << "Founded,Decada\n";
    for (int anio = 2000; anio <= 2024; ++anio) f << anio << ',' << anio / 10 * 10 << "s\n";
}

void prueba_join(Ejecutor& e) {
    e.ejecutar("CREATE TABLE j_org FROM FILE '" + CSV + "' USING HEAP");
    e.ejecutar("CREATE TABLE j_dec FROM FILE '" + CSV_DEC + "' USING BPLUS");

    Resultado r = e.ejecutar("SELECT j_org.Index, j_dec.Decada FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded");
    assert(r.filas.size() == 300);
    assert(r.columnas.size() == 2 && r.columnas[0] == "j_org.Index" && r.columnas[1] == "j_dec.Decada");
    assert(tiene_paso(r, "join") && detalle_paso(r, "join", "algoritmo") == "hash_join");
    assert(detalle_paso(r, "join", "filas_resultado") == "300");

    assert(e.ejecutar("SELECT j_org.Index FROM j_org JOIN j_dec ON Founded = Founded").filas.size() == 300);
    assert(e.ejecutar("SELECT j_org.Index FROM j_org JOIN j_dec ON j_dec.Founded = j_org.Founded").filas.size() == 300);

    e.ejecutar("CREATE INDEX ij ON j_org (Founded)");
    r = e.ejecutar("SELECT j_dec.Decada, j_org.Index FROM j_dec JOIN j_org ON j_dec.Founded = j_org.Founded");
    assert(detalle_paso(r, "join", "algoritmo") == "index_nested_loop_join");
    assert(detalle_paso(r, "join", "sondas") == "25");
    assert(r.filas.size() == 300 && "los dos algoritmos dan el mismo resultado");

    r = e.ejecutar("SELECT j_org.Index FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded WHERE j_org.Country = 'Peru'");
    assert(r.filas.size() == 100 && detalle_paso(r, "join", "filas_izquierda") == "100");

    r = e.ejecutar("SELECT j_org.Index FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded WHERE j_org.Founded = 1999");
    assert(r.filas.empty() && detalle_paso(r, "join", "filas_resultado") == "0");

    r = e.ejecutar("SELECT j_dec.Decada, COUNT(*) FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded GROUP BY j_dec.Decada ORDER BY Decada");
    assert(r.filas.size() == 2 && r.filas[0][0].texto == "2000s" && r.filas[0][1].entero == 150);
    assert(tiene_paso(r, "agrupacion") && tiene_paso(r, "ordenamiento"));
    r = e.ejecutar("SELECT j_org.Name FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded ORDER BY j_org.Index LIMIT 3");
    assert(r.filas.size() == 3 && r.filas[0][0].texto == "Org, 1" && tiene_paso(r, "limite"));

    r = e.ejecutar("SELECT * FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded LIMIT 1");
    assert(r.columnas.size() == 7 && r.columnas[0] == "j_org.Index" && r.columnas[6] == "j_dec.Decada");

    assert(falla_ejecucion(e, "SELECT Founded FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded") && "columna ambigua");
    assert(falla_ejecucion(e, "SELECT j_org.Index FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded WHERE Founded = 2005") && "ambigua en el WHERE");
    assert(falla_ejecucion(e, "SELECT x.Index FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded") && "calificador ajeno");
    assert(falla_ejecucion(e, "SELECT j_org.Index FROM j_org JOIN no_existe ON j_org.Founded = no_existe.Founded"));
    assert(falla_ejecucion(e, "SELECT j_org.Index FROM j_org JOIN j_dec ON j_org.Country = j_dec.Founded") && "tipos distintos");
    assert(falla_ejecucion(e, "SELECT j_org.Index FROM j_org JOIN j_org ON j_org.Founded = j_org.Founded") && "auto-join");
    assert(falla_ejecucion(e, "SELECT j_org.Index FROM j_org JOIN j_dec ON j_org.Founded = j_dec.Founded JOIN j_dec ON j_org.Founded = j_dec.Founded") && "solo un join");

    e.ejecutar("DROP TABLE j_org");
    e.ejecutar("DROP TABLE j_dec");
    std::cout << "join: hash join, index nested loop, seleccion antes del join, ambiguedad y errores\n";
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

}

int main() {
    std::filesystem::create_directories(".build");
    std::filesystem::remove_all(DB);
    prueba_parser();
    escribir_csv();
    escribir_csv_decadas();
    {
        Catalogo catalogo(DB);
        Ejecutor e(catalogo);
        for (const char* org : {"HEAP", "SEQUENTIAL", "BPLUS"}) prueba_organizacion(e, org);
        prueba_indice_secundario(e);
        prueba_indice_hash(e);
        prueba_join(e);
    }
    prueba_persistencia();
    prueba_tabla_manual();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
