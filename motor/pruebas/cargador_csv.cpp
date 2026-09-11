#include "cargador_csv.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>

namespace motor {
namespace pruebas {

namespace {

constexpr std::size_t CAMPOS_ESPERADOS = 9;

void recortar_retorno(std::string& linea) {
    if (!linea.empty() && linea.back() == '\r') {
        linea.pop_back();
    }
}

}  // namespace

std::size_t contar_campos_csv(const std::string& linea) {
    std::size_t campos = 1;
    bool dentro_comillas = false;
    for (const char caracter : linea) {
        if (caracter == '"') {
            dentro_comillas = !dentro_comillas;
        } else if (caracter == ',' && !dentro_comillas) {
            ++campos;
        }
    }
    return campos;
}

std::vector<std::string> dividir_campos_csv(const std::string& linea) {
    std::vector<std::string> campos;
    std::string actual;
    bool dentro_comillas = false;

    for (const char caracter : linea) {
        if (caracter == '"') {
            dentro_comillas = !dentro_comillas;
        } else if (caracter == ',' && !dentro_comillas) {
            campos.push_back(actual);
            actual.clear();
        } else {
            actual.push_back(caracter);
        }
    }
    campos.push_back(actual);
    return campos;
}

std::vector<Registro> cargar_csv(const std::string& ruta, const OpcionesCarga& opciones) {
    std::ifstream entrada(ruta, std::ios::binary);
    if (!entrada) {
        throw std::runtime_error("no se pudo abrir el CSV: " + ruta);
    }

    std::string linea;
    if (!std::getline(entrada, linea)) {
        throw std::runtime_error("el CSV esta vacio: " + ruta);
    }
    recortar_retorno(linea);
    if (contar_campos_csv(linea) != CAMPOS_ESPERADOS) {
        throw std::runtime_error("el encabezado del CSV no tiene 9 columnas");
    }

    std::vector<Registro> registros;
    if (opciones.limite > 0) {
        registros.reserve(opciones.limite);
    }

    std::size_t numero_linea = 1;
    while ((opciones.limite == 0 || registros.size() < opciones.limite) &&
           std::getline(entrada, linea)) {
        ++numero_linea;
        recortar_retorno(linea);
        if (linea.empty()) {
            continue;
        }
        if (contar_campos_csv(linea) != CAMPOS_ESPERADOS) {
            throw std::runtime_error("linea " + std::to_string(numero_linea) +
                                     " del CSV no tiene 9 columnas");
        }

        const std::size_t coma = linea.find(',');
        if (coma == std::string::npos) {
            throw std::runtime_error("linea " + std::to_string(numero_linea) + " sin separador");
        }

        Registro registro;
        registro.clave = std::stoi(linea.substr(0, coma));
        registro.valor = linea.substr(coma + 1);
        registros.push_back(std::move(registro));
    }

    if (opciones.barajar) {
        std::mt19937 generador(opciones.semilla);
        std::shuffle(registros.begin(), registros.end(), generador);
    }
    return registros;
}

}  // namespace pruebas
}  // namespace motor
