#pragma once

#include "../comun/archivo.h"

#include <cstddef>
#include <string>
#include <vector>

namespace motor {
namespace pruebas {

struct OpcionesCarga {
    std::size_t limite = 0;
    bool barajar = false;
    unsigned semilla = 42;
};

std::vector<std::string> dividir_campos_csv(const std::string& linea);
std::size_t contar_campos_csv(const std::string& linea);

std::vector<Registro> cargar_csv(const std::string& ruta, const OpcionesCarga& opciones);

}  // namespace pruebas
}  // namespace motor
