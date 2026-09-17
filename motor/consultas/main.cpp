#include <cstdio>
#include <iostream>
#include <string>
#include "ast.h"
#include "parser.h"
#include "scanner.h"

using namespace std;

static bool leerArchivo(const char *ruta, string &out)
{
    FILE *f = fopen(ruta, "rb");
    if (f == nullptr)
        return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);
    fclose(f);
    return true;
}

int main(int argc, const char *argv[])
{
    if (argc != 2)
    {
        cout << "Error: Incorrect number of arguments. Use: " << argv[0] << " <input_file>" << endl;
        return 1;
    }

    string input;
    if (!leerArchivo(argv[1], input))
    {
        cout << "Error: Cannot open the file: " << argv[1] << endl;
        return 1;
    }

    Scanner scanner(input.c_str());
    try
    {
        Parser parser(scanner);
        StatementList *programa = parser.parseProgram();
        PrintVisitor pv(cout);
        pv.imprimirPrograma(*programa);
        cout << endl;
        delete programa;
    }
    catch (const ErrorSintaxis &e)
    {
        cout << "Error de sintaxis en linea " << e.linea << ", columna " << e.columna << ":\n"
             << "  " << e.mensaje;
        if (!e.encontrado.empty())
            cout << " (se encontro " << e.encontrado << ")";
        cout << endl;
        return 1;
    }
    return 0;
}
