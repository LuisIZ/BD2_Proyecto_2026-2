#include "../consultas/external_algorithms.h"
#include "../consultas/hash_externo.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using motor::AggregateOp;
using motor::sql::AgregadoPedido;
using motor::sql::EstadisticasHash;
using motor::sql::EstadoAgregado;
using motor::sql::ExternalHashAggregate;
using motor::sql::Fila;
using motor::sql::Valor;
namespace fs = std::filesystem;

// COUNT(*), SUM(v), AVG(v), MIN(v), MAX(v) sobre la columna 1
static const std::vector<AgregadoPedido> AGREGADOS = {
    {AggregateOp::COUNT, -1}, {AggregateOp::SUM, 1}, {AggregateOp::AVG, 1},
    {AggregateOp::MIN, 1}, {AggregateOp::MAX, 1}};

// n filas repartidas en g grupos con texto ("grupo 17"); el valor cambia por fila
static std::vector<Fila> filas_de_prueba(int n, int g) {
    std::vector<Fila> filas;
    for (int i = 0; i < n; ++i) {
        filas.push_back({Valor::de_texto("grupo " + std::to_string(i % g)), Valor::de_entero((i * 37) % 1000 - 300)});
    }
    return filas;
}

// lo mismo agrupado en memoria, para comparar
static std::map<std::string, EstadoAgregado> en_memoria(const std::vector<Fila>& filas) {
    std::map<std::string, EstadoAgregado> grupos;
    for (const Fila& f : filas) {
        EstadoAgregado& e = grupos[f[0].a_texto()];
        const double x = f[1].numero();
        ++e.count;
        e.sum += x;
        e.minimum = std::min(e.minimum, x);
        e.maximum = std::max(e.maximum, x);
    }
    return grupos;
}

static EstadisticasHash comparar(const fs::path& base, int n, int g, std::size_t b) {
    const auto filas = filas_de_prueba(n, g);
    const auto esperado = en_memoria(filas);
    std::map<std::string, std::vector<EstadoAgregado>> obtenido;
    motor::PlanTrace traza("logs/external_algorithms_cpp_test.log");
    ExternalHashAggregate agregador(base, b, &traza);
    const auto stats = agregador.agrupar(filas, 0, AGREGADOS, [&](const Valor& grupo, const std::vector<EstadoAgregado>& e) {
        const bool nuevo = obtenido.emplace(grupo.a_texto(), e).second;
        assert(nuevo && "un grupo se emitio dos veces");
    });
    assert(obtenido.size() == esperado.size());
    for (const auto& [clave, e] : esperado) {
        const auto& estados = obtenido.at(clave);
        assert(estados[0].count == e.count);                          // COUNT(*)
        assert(estados[1].sum == e.sum);                              // SUM
        assert(std::fabs(estados[2].average() - e.sum / e.count) < 1e-9);  // AVG
        assert(estados[3].minimum == e.minimum);                      // MIN
        assert(estados[4].maximum == e.maximum);                      // MAX
    }
    assert(stats.grupos == esperado.size() && stats.filas == filas.size());
    // P = B - 1 particiones en el primer nivel, y todas pasan por disco
    assert(stats.particiones >= b - 1);
    assert(stats.paginas_escritas >= b - 1 && stats.paginas_leidas >= stats.paginas_escritas);
    assert(traza.last("external_hash_aggregate").details.at("groups") == std::to_string(esperado.size()));
    std::cout << "  " << g << " grupos, B = " << b << ": particiones " << stats.particiones << ", niveles "
              << stats.niveles << ", grupos en memoria " << stats.grupos_en_memoria << ", paginas escritas "
              << stats.paginas_escritas << ", leidas " << stats.paginas_leidas << "\n";
    return stats;
}

int main() {
    motor::PlanTrace traza("logs/external_algorithms_cpp_test.log");
    motor::ExternalMergeSort<int, int> sort(10, 1000, 9, &traza);
    std::vector<int> desordenados;
    for (int clave = 99'999; clave >= 0; --clave) desordenados.push_back(clave);
    const auto ordenados = sort.ordenar(desordenados, [](const int& clave) { return clave; });
    assert(ordenados.size() == 100'000);
    assert(ordenados.front() == 0 && ordenados.back() == 99'999);
    assert(traza.last("external_merge_sort").details.at("buffer_pages") == "10");

    // GROUP BY con external hashing en disco: igual que agrupar en memoria
    const fs::path base = ".build/external_hashing_test";
    fs::remove_all(base);
    const auto uno = comparar(base, 5'000, 1, 10);
    assert(uno.niveles == 1 && uno.grupos_en_memoria == 1);
    const auto cien = comparar(base, 5'000, 100, 10);
    assert(cien.niveles == 1 && cien.grupos_en_memoria < 100);  // cada particion tiene una parte
    // cada grupo ocupa su clave y 5 estados (unos 174 bytes): en 10 paginas caben unos 235,
    // y 50 000 / 9 particiones no caben, asi que hay que volver a particionar
    const auto muchos = comparar(base, 100'000, 50'000, 10);
    assert(muchos.niveles >= 2 && muchos.reparticiones > 0);
    // con B = 3 hay 2 particiones y 12 KB de tabla (unos 70 grupos): 200 grupos dan 100 por
    // particion, que no caben, y en el segundo nivel quedan unos 50
    const auto chico = comparar(base, 3'000, 200, 3);
    assert(chico.niveles == 2 && chico.reparticiones > 0);
    assert(chico.particiones > 2);

    // sin GROUP BY (col_grupo = -1): un solo grupo con todas las filas
    {
        const auto filas = filas_de_prueba(1'234, 7);
        std::size_t emitidos = 0;
        ExternalHashAggregate agregador(base, 4);
        agregador.agrupar(filas, -1, AGREGADOS, [&](const Valor&, const std::vector<EstadoAgregado>& e) {
            ++emitidos;
            assert(e[0].count == 1'234);
        });
        assert(emitidos == 1);
    }

    // si ni con 3 niveles cabe, error claro
    {
        bool fallo = false;
        try {
            ExternalHashAggregate agregador(base, 3);
            agregador.agrupar(filas_de_prueba(60'000, 60'000), 0, AGREGADOS, [](const Valor&, const std::vector<EstadoAgregado>&) {});
        } catch (const std::runtime_error& e) {
            fallo = std::string(e.what()).find("no cabe en 3 paginas") != std::string::npos;
        }
        assert(fallo);
    }
    // los temporales se borran al terminar, aunque haya error
    assert(!fs::exists(base / "tmp") || fs::is_empty(base / "tmp"));
    fs::remove_all(base);

    std::vector<int> izquierda{1, 2, 3};
    std::vector<int> derecha{2, 3, 4};
    const auto unidos = motor::hash_join<int, int, int>(
        izquierda, derecha,
        [](const int& valor) { return valor; },
        [](const int& valor) { return valor; });
    assert(unidos.size() == 2);

    const auto index_unidos = motor::index_nested_loop_join<int, int, int>(
        izquierda,
        [](const int& clave) {
            if (clave == 1 || clave == 2) return std::vector<int>{clave};
            return std::vector<int>{};
        },
        [](const int& valor) { return valor; });
    assert(index_unidos.size() == 2);

    motor::JoinPlanner planner(&traza);
    assert(planner.choose(100, 10, false) == "hash_join");
    assert(planner.choose(3, 10, true) == "index_nested_loop_join");

    std::cout << "External algorithms C++: OK\n";
}
