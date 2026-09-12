#include "../archivos/sequential_file.h"

#include <cassert>
#include <iostream>
#include <vector>

void imprimir_registros(const std::vector<motor::Registro>& registros) {
    std::cout << "Registros ordenados: ";
    for (const motor::Registro& registro : registros) {
        std::cout << "(" << registro.clave << ", " << registro.valor << ") ";
    }
    std::cout << "\n";
}

void imprimir_stats(const motor::EstadisticasArchivo& estadisticas) {
    std::cout << "Stats: registros=" << estadisticas.registros
              << ", tumbas=" << estadisticas.tumbas
              << ", paginas=" << estadisticas.paginas
              << ", overflow=" << estadisticas.registros_auxiliares
              << ", desperdicio=" << estadisticas.porcentaje_desperdicio * 100.0
              << "%"
              << ", ultima_reorganizacion=" << estadisticas.ultima_reorganizacion_us
              << " us\n";
}

int main() {
    motor::SequentialFile archivo(2, 1.0);
    assert(archivo.insertar({2, "dos"}));
    assert(archivo.insertar({1, "uno"}));
    assert(archivo.insertar({4, "cuatro"}));
    assert(archivo.insertar({3, "tres"}));
    assert(archivo.insertar({5, "cinco"}));
    assert(!archivo.insertar({3, "duplicado"}));

    const std::vector<motor::Registro> ordenado = archivo.scan();
    assert(ordenado.size() == 5);
    for (std::size_t indice = 0; indice < ordenado.size(); ++indice) {
        assert(ordenado[indice].clave == static_cast<int>(indice + 1));
    }
    const motor::EstadisticasArchivo stats_iniciales = archivo.stats();
    assert(stats_iniciales.paginas >= 2);
    assert(stats_iniciales.registros_auxiliares == 0);
    imprimir_registros(ordenado);
    imprimir_stats(stats_iniciales);

    assert(archivo.eliminar(2));
    const motor::EstadisticasArchivo stats_con_tumba = archivo.stats();
    assert(stats_con_tumba.tumbas == 1);
    assert(stats_con_tumba.porcentaje_desperdicio > 0.0);
    std::cout << "Despues de eliminar la clave 2:\n";
    imprimir_stats(stats_con_tumba);

    archivo.set_umbral_reorganizacion(0.30);
    assert(archivo.eliminar(4));
    const motor::EstadisticasArchivo estadisticas = archivo.stats();
    assert(estadisticas.tumbas == 0);
    assert(estadisticas.ultima_reorganizacion_us >= 0);
    std::cout << "Despues de eliminar la clave 4 y reorganizar:\n";
    imprimir_stats(estadisticas);

    const std::vector<motor::Registro> despues = archivo.scan();
    assert(despues.size() == 3);
    assert(despues[0].clave == 1);
    assert(despues[1].clave == 3);
    assert(despues[2].clave == 5);
    imprimir_registros(despues);
    std::cout << "Prueba completada correctamente.\n";
}