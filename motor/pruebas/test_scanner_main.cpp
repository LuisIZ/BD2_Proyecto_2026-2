#include <iostream>
#include <string>
#include <vector>
#include "scanner.h"

using namespace std;

static int fallos = 0;

static string tokenize(const string &sql, bool show_pos = false)
{
    Scanner sc(sql.c_str());
    string out;
    Token *t;
    while ((t = sc.nextToken())->type != Token::END_INPUT)
    {
        if (!out.empty()) out += " ";
        out += Token::typeName(t->type);
        if (t->type == Token::ID || t->type == Token::NUM || t->type == Token::STR || t->type == Token::ERR)
            out += "(" + t->text + ")";
        if (show_pos)
            out += "@" + to_string(t->line) + ":" + to_string(t->column);
        if (t->type == Token::ERR) { delete t; break; }
        delete t;
    }
    return out;
}

static void check(const string &sql, const string &esperado)
{
    string got = tokenize(sql);
    bool ok = (got == esperado);
    if (!ok) fallos++;
    cout << (ok ? "[OK]    " : "[FALLO] ") << sql << "\n";
    if (!ok) cout << "         esperado: " << esperado << "\n         obtenido: " << got << "\n";
}

int main()
{
    cout << "===== palabras reservadas e insensibilidad a mayusculas =====\n";
    check("SELECT * FROM t", "SELECT * FROM ID(t)");
    check("select * from t", "SELECT * FROM ID(t)");
    check("SeLeCt * FrOm T", "SELECT * FROM ID(T)");

    cout << "\n===== identificadores vs reservadas =====\n";
    check("selected", "ID(selected)");          // no es SELECT
    check("_tabla1 x2", "ID(_tabla1) ID(x2)");
    check("a.b", "ID(a) . ID(b)");

    cout << "\n===== operadores de uno y dos caracteres =====\n";
    check("= <> != < <= > >=", "= <> <> < <= > >=");
    check("a<=b", "ID(a) <= ID(b)");
    check("a<>b", "ID(a) <> ID(b)");
    check("a<b", "ID(a) < ID(b)");
    check("+ - * %", "+ - * %");

    cout << "\n===== numeros (solo enteros en la Parte 1) =====\n";
    check("42", "NUM(42)");
    check("ciclo = -5", "ID(ciclo) = - NUM(5)");
    check("3.5", "ERR(3.5)");                    // decimal rechazado con el lexema completo

    cout << "\n===== cadenas =====\n";
    check("'Ana'", "STR(Ana)");
    check("'D''Angelo'", "STR(D'Angelo)");       // comilla escapada al estilo SQL
    check("'sin cerrar", "ERR('sin cerrar)");

    cout << "\n===== comentarios =====\n";
    check("-- nada\nSELECT", "SELECT");
    check("SELECT -- comentario al final", "SELECT");
    check("a -- x\n- b", "ID(a) - ID(b)");       // el '-' suelto sigue siendo MINUS

    cout << "\n===== reservadas sin uso: cierran el hueco de LEFT JOIN =====\n";
    check("FROM t LEFT JOIN u", "FROM ID(t) LEFT JOIN ID(u)");   // LEFT no es ID
    check("FROM t x JOIN u", "FROM ID(t) ID(x) JOIN ID(u)");      // alias normal si funciona

    cout << "\n===== caracteres fuera de la gramatica =====\n";
    check("a / b", "ID(a) ERR(/)");
    check("a ? b", "ID(a) ERR(?)");

    cout << "\n===== consultas completas =====\n";
    check("CREATE TABLE t (id INT PRIMARY KEY, nom VARCHAR(20) INDEX HASH);",
          "CREATE TABLE ID(t) ( ID(id) INT PRIMARY KEY , ID(nom) VARCHAR ( NUM(20) ) INDEX HASH ) ;");
    check("SELECT ciclo, COUNT(*) FROM alumnos GROUP BY ciclo HAVING COUNT(*) > 5;",
          "SELECT ID(ciclo) , ID(COUNT) ( * ) FROM ID(alumnos) GROUP BY ID(ciclo) HAVING ID(COUNT) ( * ) > NUM(5) ;");
    check("INSERT INTO t VALUES ('A1', 3), ('A2', 4);",
          "INSERT INTO ID(t) VALUES ( STR(A1) , NUM(3) ) , ( STR(A2) , NUM(4) ) ;");
    check("BEGIN TRANSACTION; UPDATE t SET x = x + 1 WHERE id = 1; COMMIT;",
          "BEGIN TRANSACTION ; UPDATE ID(t) SET ID(x) = ID(x) + NUM(1) WHERE ID(id) = NUM(1) ; COMMIT ;");
    check("SELECT * FROM t WHERE a BETWEEN 1 AND 9 OR NOT b IN (1,2) AND c LIKE 'A%';",
          "SELECT * FROM ID(t) WHERE ID(a) BETWEEN NUM(1) AND NUM(9) OR NOT ID(b) IN ( NUM(1) , NUM(2) ) AND ID(c) LIKE STR(A%) ;");

    cout << "\n===== linea y columna =====\n";
    {
        string got = tokenize("SELECT x\nFROM t", true);
        string esp = "SELECT@1:1 ID(x)@1:8 FROM@2:1 ID(t)@2:6";
        bool ok = (got == esp);
        if (!ok) fallos++;
        cout << (ok ? "[OK]    " : "[FALLO] ") << "posiciones en entrada multilinea\n";
        if (!ok) cout << "         esperado: " << esp << "\n         obtenido: " << got << "\n";
    }

    cout << "\n";
    if (fallos == 0) { cout << "Todas las pruebas del scanner pasaron.\n"; return 0; }
    cout << fallos << " prueba(s) fallaron.\n";
    return 1;
}
