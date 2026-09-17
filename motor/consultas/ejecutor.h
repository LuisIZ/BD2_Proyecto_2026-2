#pragma once

#include "catalogo.h"
#include "parser_sql.h"

#include <string>
#include <utility>
#include <vector>

namespace motor {
namespace sql {

struct PasoPlan {
    std::string operacion;
    std::vector<std::pair<std::string, std::string>> detalles;
};

struct Resultado {
    std::string tipo;  // select | insert | delete | create_table | create_index | drop_table | show_tables | describe
    std::vector<std::string> columnas;
    std::vector<Fila> filas;
    std::vector<PasoPlan> plan;
    std::string mensaje;
    std::size_t afectadas = 0;
    double tiempo_ms = 0.0;
};

// Ejecuta sentencias contra las tablas del catálogo. Abre los archivos de la
// tabla al empezar cada sentencia y los cierra al terminar, así todo queda en
// disco entre llamadas. Los errores se lanzan como std::runtime_error.
class Ejecutor {
public:
    explicit Ejecutor(Catalogo& catalogo) : catalogo_(catalogo) {}

    Resultado ejecutar(const std::string& sql);
    Resultado ejecutar(const Sentencia& sentencia);

private:
    Resultado crear_tabla(const Sentencia& s);
    Resultado crear_tabla_desde_csv(const Sentencia& s);
    Resultado crear_indice(const Sentencia& s);
    Resultado borrar_tabla(const Sentencia& s);
    Resultado insertar(const Sentencia& s);
    Resultado eliminar(const Sentencia& s);
    Resultado seleccionar(const Sentencia& s);
    Resultado mostrar_tablas();
    Resultado describir(const Sentencia& s);

    Catalogo& catalogo_;
};

}  // namespace sql
}  // namespace motor
