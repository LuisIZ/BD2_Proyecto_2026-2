#include "../indices/extendible_hash.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct HashColisionante {
    std::size_t operator()(int) const noexcept { return 1; }
};

int main() {
    motor::ExtendibleHashing<int, std::string> indice(2);
    assert(indice.insertar(10, "diez"));
    assert(indice.insertar(20, "veinte"));
    assert(!indice.insertar(10, "duplicado"));
    assert(indice.buscar(10).value() == "diez");
    assert(!indice.buscar(99).has_value());
    assert(indice.supportsRange() == false);

    for (int clave = 0; clave < 8; ++clave) {
        indice.insertar(clave, std::to_string(clave));
    }
    assert(indice.cantidad() == 10);
    assert(indice.cantidad_buckets() > 1);
    assert(indice.profundidad_global() > 0);

    assert(indice.eliminar(10));
    assert(!indice.buscar(10).has_value());

    motor::ExtendibleHashing<int, std::string, HashColisionante> colisiones(2);
    colisiones.insertar(1, "uno");
    colisiones.insertar(2, "dos");
    try {
        colisiones.insertar(3, "tres");
        assert(false && "Se esperaba overflow por colision inseparable");
    } catch (const std::overflow_error&) {
        assert(colisiones.cantidad() == 2);
    }

    motor::ExtendibleHashing<int, std::string> merge(2);
    for (int clave = 0; clave < 16; ++clave) merge.insertar(clave, std::to_string(clave));
    const auto profundidad_con_datos = merge.profundidad_global();
    for (int clave = 0; clave < 16; ++clave) assert(merge.eliminar(clave));
    assert(profundidad_con_datos > 0);
    assert(merge.profundidad_global() == 0);
    assert(merge.cantidad_buckets() == 1);

    std::vector<std::pair<int, std::string>> registros;
    for (int clave = 0; clave < 100'000; ++clave) {
        registros.push_back({clave, std::to_string(clave)});
    }
    auto grande = motor::ExtendibleHashing<int, std::string>::construir(registros, 64);
    assert(grande.cantidad() == 100'000);
    assert(grande.buscar(50'000).value() == "50000");
    assert(grande.buscar(100'001) == std::nullopt);
    const auto& metricas = grande.metricas();
    assert(metricas.construccion_ns > 0);
    assert(metricas.espacio_adicional_bytes > 0);

    std::cout << "Extendible hashing C++: OK\n";
    std::cout << "Construccion ms: " << metricas.construccion_ms() << "\n";
    std::cout << "Espacio adicional: " << metricas.espacio_adicional_bytes << " bytes\n";
}