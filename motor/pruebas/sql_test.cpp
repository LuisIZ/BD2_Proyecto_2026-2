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

    assert(falla("SELECT FROM t"));
    assert(falla("SELECT * t"));
    assert(falla("SELECT * FROM t WHERE"));
    assert(falla("SELECT * FROM t WHERE a = 1.5"));
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

void prueba_indice_secundario(Ejecutor& e) {
    assert(falla_ejecucion(e, "CREATE INDEX i ON org_SEQUENTIAL (Founded)") && "solo sobre heap");
    assert(falla_ejecucion(e, "CREATE INDEX i ON org_HEAP (Founded) USING HASH") && "hash pendiente");
    assert(falla_ejecucion(e, "CREATE INDEX i ON org_HEAP (Country)") && "solo INT");

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
    }
    prueba_persistencia();
    prueba_tabla_manual();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
