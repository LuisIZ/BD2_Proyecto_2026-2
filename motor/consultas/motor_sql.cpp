// CLI del motor: recibe SQL y responde JSON en stdout.
//
//   motor_sql --db datos/db --sql "SELECT * FROM t WHERE Index = 5"   una o varias sentencias separadas por ';'
//   motor_sql --db datos/db --catalogo                                  tablas, columnas e indices
//   motor_sql --db datos/db                                             lee sentencias de stdin, una por linea
//
// Cada respuesta es un objeto {"ok":true,...} o {"ok":false,"error":"..."}.
// Con --sql se devuelve un arreglo con una respuesta por sentencia.

#include "catalogo.h"
#include "ejecutor.h"
#include "parser_sql.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {

using motor::sql::Catalogo;
using motor::sql::Ejecutor;
using motor::sql::Fila;
using motor::sql::PasoPlan;
using motor::sql::Resultado;
using motor::sql::Valor;

std::string json_texto(const std::string& s) {
    std::string salida = "\"";
    for (const unsigned char c : s) {
        switch (c) {
            case '"': salida += "\\\""; break;
            case '\\': salida += "\\\\"; break;
            case '\n': salida += "\\n"; break;
            case '\r': salida += "\\r"; break;
            case '\t': salida += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    salida += buf;
                } else {
                    salida.push_back(static_cast<char>(c));
                }
        }
    }
    return salida + "\"";
}

std::string json_valor(const Valor& v) {
    return v.es_entero ? std::to_string(v.entero) : json_texto(v.texto);
}

std::string json_resultado(const Resultado& r) {
    std::ostringstream s;
    s << "{\"ok\":true,\"tipo\":" << json_texto(r.tipo) << ",\"mensaje\":" << json_texto(r.mensaje)
      << ",\"afectadas\":" << r.afectadas << ",\"tiempo_ms\":" << r.tiempo_ms << ",\"columnas\":[";
    for (std::size_t i = 0; i < r.columnas.size(); ++i) s << (i ? "," : "") << json_texto(r.columnas[i]);
    s << "],\"filas\":[";
    for (std::size_t i = 0; i < r.filas.size(); ++i) {
        s << (i ? ",[" : "[");
        for (std::size_t j = 0; j < r.filas[i].size(); ++j) s << (j ? "," : "") << json_valor(r.filas[i][j]);
        s << "]";
    }
    s << "],\"plan\":[";
    for (std::size_t i = 0; i < r.plan.size(); ++i) {
        const PasoPlan& p = r.plan[i];
        s << (i ? ",{" : "{") << "\"operacion\":" << json_texto(p.operacion);
        for (const auto& [k, v] : p.detalles) s << "," << json_texto(k) << ":" << json_texto(v);
        s << "}";
    }
    s << "]}";
    return s.str();
}

std::string json_error(const std::string& mensaje) {
    return "{\"ok\":false,\"error\":" + json_texto(mensaje) + "}";
}

std::string json_catalogo(const Catalogo& catalogo) {
    std::ostringstream s;
    s << "{\"ok\":true,\"directorio\":" << json_texto(catalogo.dir()) << ",\"tablas\":[";
    bool primera = true;
    for (const auto& [clave, t] : catalogo.tablas()) {
        s << (primera ? "{" : ",{") << "\"nombre\":" << json_texto(t.nombre)
          << ",\"organizacion\":" << json_texto(motor::sql::nombre_organizacion(t.organizacion))
          << ",\"archivo\":" << json_texto(t.archivo) << ",\"clave\":" << json_texto(t.columnas[t.pk].nombre)
          << ",\"columnas\":[";
        for (std::size_t i = 0; i < t.columnas.size(); ++i) {
            const auto& c = t.columnas[i];
            s << (i ? ",{" : "{") << "\"nombre\":" << json_texto(c.nombre) << ",\"tipo\":" << json_texto(motor::sql::nombre_tipo(c.tipo))
              << ",\"tam\":" << c.tam << ",\"pk\":" << (i == t.pk ? "true" : "false") << "}";
        }
        s << "],\"indices\":[";
        for (std::size_t i = 0; i < t.indices.size(); ++i) {
            const auto& x = t.indices[i];
            s << (i ? ",{" : "{") << "\"nombre\":" << json_texto(x.nombre) << ",\"columna\":" << json_texto(x.columna)
              << ",\"tipo\":" << json_texto(x.tipo) << ",\"archivo\":" << json_texto(x.archivo) << "}";
        }
        s << "]}";
        primera = false;
    }
    s << "]}";
    return s.str();
}

std::string ejecutar_lote(Ejecutor& ejecutor, const std::string& sql) {
    std::string salida = "[";
    bool primera = true;
    for (const std::string& sentencia : motor::sql::separar_sentencias(sql)) {
        salida += primera ? "" : ",";
        primera = false;
        try {
            salida += json_resultado(ejecutor.ejecutar(sentencia));
        } catch (const std::exception& e) {
            salida += json_error(e.what());
        }
    }
    return salida + "]";
}

}  // namespace

int main(int argc, char** argv) {
    std::string db = "datos/db";
    std::string sql;
    bool catalogo_pedido = false;
    bool sql_dado = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) db = argv[++i];
        else if (arg == "--sql" && i + 1 < argc) { sql = argv[++i]; sql_dado = true; }
        else if (arg == "--catalogo") catalogo_pedido = true;
        else {
            std::cout << json_error("opcion no reconocida: " + arg) << "\n";
            return 1;
        }
    }

    try {
        Catalogo catalogo(db);
        Ejecutor ejecutor(catalogo);
        if (catalogo_pedido) {
            std::cout << json_catalogo(catalogo) << "\n";
            return 0;
        }
        if (sql_dado) {
            std::cout << ejecutar_lote(ejecutor, sql) << "\n";
            return 0;
        }
        std::string linea;
        while (std::getline(std::cin, linea)) {
            if (linea.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            std::cout << ejecutar_lote(ejecutor, linea) << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << json_error(e.what()) << "\n";
        return 1;
    }
    return 0;
}
