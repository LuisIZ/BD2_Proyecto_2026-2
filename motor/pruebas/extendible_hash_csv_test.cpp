#include "../indices/extendible_hash.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int indice_de_linea(const std::string& linea) {
    const auto separador = linea.find(',');
    return std::stoi(linea.substr(0, separador));
}

int main() {
    std::ifstream archivo("datos/organizations-100000.csv");
    assert(archivo && "No se encontro datos/organizations-100000.csv");
    std::string linea;
    std::getline(archivo, linea);

    std::vector<std::pair<int, std::string>> registros;
    while (std::getline(archivo, linea)) {
        registros.push_back({indice_de_linea(linea), linea});
    }
    assert(registros.size() == 100'000);

    auto indice = motor::ExtendibleHashing<int, std::string>::construir(registros, 128);
    assert(indice.cantidad() == 100'000);
    assert(indice.buscar(1).has_value());
    assert(indice.buscar(50'000).has_value());
    assert(indice.buscar(100'000).has_value());
    assert(!indice.buscar(100'001).has_value());

    for (int clave = 1; clave <= 1000; ++clave) {
        assert(indice.eliminar(clave));
    }
    for (const auto& registro : registros) {
        if (registro.first <= 1000) assert(indice.insertar(registro.first, registro.second));
    }
    const auto& metricas = indice.metricas();
    assert(metricas.construccion_ns > 0);
    assert(metricas.consultas > 0);
    assert(metricas.inserciones >= 101'000);
    assert(metricas.eliminaciones == 1000);
    assert(metricas.espacio_adicional_bytes > 0);
    assert(indice.cantidad() == 100'000);

    std::cout << "CSV extendible hashing C++: OK\n";
    std::cout << "Construccion ms: " << metricas.construccion_ms() << "\n";
    std::cout << "Consultas promedio us: " << metricas.consulta_promedio_us() << "\n";
    std::cout << "Inserciones promedio us: " << metricas.insercion_promedio_us() << "\n";
    std::cout << "Eliminaciones promedio us: " << metricas.eliminacion_promedio_us() << "\n";
    std::cout << "Espacio adicional: " << metricas.espacio_adicional_bytes << " bytes\n";
}