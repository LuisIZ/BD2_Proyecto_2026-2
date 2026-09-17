#include <iostream>
#include <sstream>
#include <string>
#include "ast.h"
#include "parser.h"
#include "scanner.h"

using namespace std;

static int fallos = 0;

static string imprimirPrograma(const string &sql)
{
    Scanner sc(sql.c_str());
    Parser p(sc);
    StatementList *lista = p.parseProgram();
    ostringstream out;
    PrintVisitor pv(out);
    pv.imprimirPrograma(*lista);
    delete lista;
    return out.str();
}

static void debeParsear(const string &sql)
{
    try
    {
        imprimirPrograma(sql);
        cout << "[OK]    parsea: " << sql << "\n";
    }
    catch (const ErrorSintaxis &e)
    {
        fallos++;
        cout << "[FALLO] deberia parsear pero fallo: " << sql
             << "\n         " << e.mensaje << " (linea " << e.linea << ", columna " << e.columna << ")\n";
    }
}

static void debeFallar(const string &sql, int linea_esperada = -1, int columna_esperada = -1)
{
    try
    {
        imprimirPrograma(sql);
        fallos++;
        cout << "[FALLO] deberia fallar pero parseo: " << sql << "\n";
    }
    catch (const ErrorSintaxis &e)
    {
        bool posicion_ok = (linea_esperada < 0) ||
                            (e.linea == linea_esperada && e.columna == columna_esperada);
        if (!posicion_ok)
        {
            fallos++;
            cout << "[FALLO] fallo en posicion incorrecta: " << sql
                 << "\n         esperado (" << linea_esperada << "," << columna_esperada
                 << ") obtenido (" << e.linea << "," << e.columna << ")\n";
        }
        else
        {
            cout << "[OK]    falla como se esperaba: " << sql
                 << "  -- " << e.mensaje << " (linea " << e.linea << ", columna " << e.columna << ")\n";
        }
    }
}

static void formaArbol(const string &sql_completo, const string &esperado)
{
    string got = imprimirPrograma(sql_completo);
    bool ok = (got == esperado);
    if (!ok)
        fallos++;
    cout << (ok ? "[OK]    " : "[FALLO] ") << "forma del arbol: " << sql_completo << "\n";
    if (!ok)
        cout << "         esperado: " << esperado << "\n         obtenido: " << got << "\n";
}

static void roundTrip(const string &sql)
{
    try
    {
        string impreso1 = imprimirPrograma(sql);
        string impreso2 = imprimirPrograma(impreso1);
        bool ok = (impreso1 == impreso2);
        if (!ok)
            fallos++;
        cout << (ok ? "[OK]    " : "[FALLO] ") << "round-trip: " << sql << "\n";
        if (!ok)
            cout << "         1a impresion: " << impreso1 << "\n         2a impresion: " << impreso2 << "\n";
    }
    catch (const ErrorSintaxis &e)
    {
        fallos++;
        cout << "[FALLO] round-trip lanzo excepcion: " << sql
             << "\n         " << e.mensaje << "\n";
    }
}

int main()
{
    cout << "===== DDL: deben parsear =====\n";
    debeParsear("CREATE TABLE alumnos (codigo VARCHAR(8) PRIMARY KEY, nombre VARCHAR(40), ciclo INT);");
    debeParsear("CREATE TABLE t (id INT PRIMARY KEY, nom VARCHAR(20) INDEX HASH);");
    debeParsear("CREATE TABLE m FROM FILE 'matriculas.csv' USING INDEX HASH (codigo);");
    debeParsear("CREATE INDEX idx ON alumnos USING BTREE (ciclo);");
    debeParsear("DROP TABLE alumnos;");

    cout << "\n===== SELECT: deben parsear =====\n";
    debeParsear("SELECT * FROM alumnos;");
    debeParsear("SELECT codigo, nombre FROM alumnos WHERE ciclo = 5;");
    debeParsear("SELECT * FROM alumnos WHERE ciclo BETWEEN 3 AND 7 ORDER BY nombre DESC LIMIT 10 OFFSET 5;");
    debeParsear("SELECT * FROM alumnos WHERE ciclo IN (1,2,3) AND NOT nombre LIKE 'A%';");
    debeParsear("SELECT ciclo, COUNT(*) AS total FROM alumnos GROUP BY ciclo HAVING COUNT(*) > 5;");
    debeParsear("SELECT a.nombre, m.curso FROM alumnos AS a JOIN matriculas m ON a.codigo = m.codigo;");
    debeParsear("SELECT * FROM a JOIN b ON a.x = b.x JOIN c ON b.y = c.y;");
    debeParsear("SELECT * FROM t WHERE ciclo * 2 + 1 > 10 OR ciclo % 2 = 0;");
    debeParsear("SELECT * FROM t WHERE ciclo = -5;");
    debeParsear("SELECT * FROM t WHERE distancia(ubicacion, 5) < 100;");

    cout << "\n===== DML y transacciones: deben parsear =====\n";
    debeParsear("INSERT INTO alumnos (codigo, nombre) VALUES ('A100','Ana'), ('A101','Luis');");
    debeParsear("DELETE FROM alumnos WHERE ciclo = 10;");
    debeParsear("UPDATE alumnos SET ciclo = ciclo + 1 WHERE codigo = 'A100';");
    debeParsear("BEGIN TRANSACTION; UPDATE t SET x = 1 WHERE id = 1; COMMIT;");
    debeParsear("BEGIN; DELETE FROM t WHERE id = 1; ROLLBACK;");
    debeParsear("END TRANSACTION;");

    cout << "\n===== precedencia: forma exacta del arbol (via PrintVisitor) =====\n";
    formaArbol("SELECT * FROM t WHERE a = 1 OR b = 2 AND c = 3;",
               "SELECT * FROM t WHERE ((a = 1) OR ((b = 2) AND (c = 3)))");
    formaArbol("SELECT * FROM t WHERE x = 1 + 2 * 3;",
               "SELECT * FROM t WHERE (x = (1 + (2 * 3)))");
    formaArbol("SELECT * FROM t WHERE x = 1 - 2 - 3;",
               "SELECT * FROM t WHERE (x = ((1 - 2) - 3))");
    formaArbol("SELECT * FROM t WHERE x = -5 + 3;",
               "SELECT * FROM t WHERE (x = ((-5) + 3))");

    cout << "\n===== round-trip: parse -> print -> parse da el mismo arbol =====\n";
    roundTrip("SELECT a, b FROM t WHERE a = 5 AND b < 10 ORDER BY a DESC LIMIT 10 OFFSET 2;");
    roundTrip("SELECT * FROM alumnos WHERE ciclo BETWEEN 3 AND 7;");
    roundTrip("INSERT INTO t (a, b) VALUES (1, 'x'), (2, 'y');");
    roundTrip("UPDATE t SET a = a + 1 WHERE b = 'z';");

    cout << "\n===== deben fallar, con linea y columna correctas =====\n";
    debeFallar("SELECT FROM t;", 1, 8);
    debeFallar("SELECT * t;", 1, 10);
    debeFallar("INSERT INTO t VALUES;", 1, 21);
    debeFallar("CREATE TABLE (id INT);", 1, 14);
    debeFallar("SELECT * FROM t WHERE a = ;", 1, 27);
    debeFallar("SELECT * FROM t WHERE a < b < c;", -1, -1); // solo importa que falle
    debeFallar("SELECT * FROM t LEFT JOIN u ON t.a = u.a;", 1, 17);
    debeFallar("SELECT * FROM t GROUP ciclo;", 1, 23);
    debeFallar("SELECT * FROM t WHERE a = 1 basura;", 1, 29); // "t basura" solo, seria alias valido
    debeFallar("SELECT * FROM t WHERE count(*) > 1 basura basura;", -1, -1);

    cout << "\n===== robustez: limite de profundidad de expresiones =====\n";
    {
        string sql = "SELECT * FROM t WHERE a = ";
        for (int i = 0; i < 300; ++i) sql += "(";
        sql += "1";
        for (int i = 0; i < 300; ++i) sql += ")";
        sql += ";";
        debeFallar(sql); // no debe hacer stack overflow; debe lanzar ErrorSintaxis
    }

    cout << "\n===== robustez: muchas filas en un solo INSERT =====\n";
    {
        string sql = "INSERT INTO t (a) VALUES ";
        for (int i = 0; i < 10000; ++i)
        {
            if (i > 0) sql += ", ";
            sql += "(" + to_string(i) + ")";
        }
        sql += ";";
        try
        {
            imprimirPrograma(sql);
            cout << "[OK]    parsea INSERT con 10000 filas sin desbordar la pila\n";
        }
        catch (const ErrorSintaxis &e)
        {
            fallos++;
            cout << "[FALLO] INSERT con 10000 filas: " << e.mensaje << "\n";
        }
    }

    cout << "\n";
    if (fallos == 0)
    {
        cout << "Todas las pruebas del parser pasaron.\n";
        return 0;
    }
    cout << fallos << " prueba(s) fallaron.\n";
    return 1;
}
