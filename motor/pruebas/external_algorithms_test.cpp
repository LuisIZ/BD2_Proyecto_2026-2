#include "../consultas/external_algorithms.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

struct RegistroOrden {
    int clave;
    std::string valor;
};

struct RegistroGrupo {
    int grupo;
    double valor;
};

int main() {
    motor::PlanTrace traza("logs/external_algorithms_cpp_test.log");
    motor::ExternalMergeSort<int, int> sort(10, 1000, 9, &traza);
    std::vector<int> desordenados;
    for (int clave = 99'999; clave >= 0; --clave) desordenados.push_back(clave);
    const auto ordenados = sort.ordenar(desordenados, [](const int& clave) { return clave; });
    assert(ordenados.size() == 100'000);
    assert(ordenados.front() == 0 && ordenados.back() == 99'999);
    assert(traza.last("external_merge_sort").details.at("buffer_pages") == "10");

    std::vector<RegistroGrupo> grupos;
    for (int grupo = 0; grupo < 100; ++grupo) grupos.push_back({grupo, static_cast<double>(grupo)});
    motor::ExternalHashAggregate<RegistroGrupo, int> aggregate(4, 2, &traza);
    const auto resultado = aggregate.agrupar(
        grupos,
        [](const RegistroGrupo& registro) { return registro.grupo; },
        [](const RegistroGrupo& registro) { return registro.valor; },
        {motor::AggregateOp::COUNT, motor::AggregateOp::SUM,
         motor::AggregateOp::AVG, motor::AggregateOp::MIN, motor::AggregateOp::MAX});
    assert(resultado.size() == 100);
    assert(resultado.at(73).count == 1);
    assert(resultado.at(73).sum == 73.0);

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