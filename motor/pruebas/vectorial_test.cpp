#include "../vectorial/vector.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

using motor::vectorial::distancia;
using motor::vectorial::MetricaVec;
using motor::vectorial::metrica_vec_desde;
using motor::vectorial::normalizar;
using motor::vectorial::Vector;

namespace {

bool cerca(double obtenido, double esperado, double tolerancia) {
    return std::fabs(obtenido - esperado) <= tolerancia;
}

const MetricaVec METRICAS[] = {MetricaVec::EUCLIDIANA, MetricaVec::PRODUCTO_PUNTO, MetricaVec::COSENO};

std::vector<Vector> vectores_al_azar(int n, int dim, unsigned semilla) {
    std::mt19937 gen(semilla);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    std::vector<Vector> vs(n, Vector(dim));
    for (auto& v : vs)
        for (float& x : v) x = u(gen);
    return vs;
}

// posiciones de los k más cercanos recorriendo todos los vectores
std::vector<int> knn_fuerza_bruta(const std::vector<Vector>& vs, const Vector& q, int k, MetricaVec metrica) {
    std::vector<int> pos(vs.size());
    std::iota(pos.begin(), pos.end(), 0);
    std::stable_sort(pos.begin(), pos.end(),
                     [&](int a, int b) { return distancia(vs[a], q, metrica) < distancia(vs[b], q, metrica); });
    pos.resize(k);
    return pos;
}

void prueba_valores() {
    const Vector a{1, 2, 3}, b{4, 6, 3};
    assert(cerca(distancia(a, b, MetricaVec::EUCLIDIANA), 5.0, 1e-9));
    assert(cerca(distancia(a, b, MetricaVec::PRODUCTO_PUNTO), -25.0, 1e-9));
    assert(cerca(distancia(a, b, MetricaVec::COSENO), 1.0 - 25.0 / (std::sqrt(14.0) * std::sqrt(61.0)), 1e-9));
    assert(cerca(distancia({1, 0}, {0, 1}, MetricaVec::COSENO), 1.0, 1e-9));
    assert(cerca(distancia({1, 0}, {-1, 0}, MetricaVec::COSENO), 2.0, 1e-9));
    assert(cerca(distancia({0, 0}, {1, 1}, MetricaVec::COSENO), 1.0, 1e-9));
    std::cout << "valores: euclidiana 5, producto punto -25 y coseno a mano\n";
}

void prueba_propiedades() {
    const auto vs = vectores_al_azar(50, 256, 7);
    for (const MetricaVec m : METRICAS) {
        for (std::size_t i = 1; i < vs.size(); ++i)
            assert(cerca(distancia(vs[i], vs[i - 1], m), distancia(vs[i - 1], vs[i], m), 1e-9));
    }
    for (const auto& v : vs) {
        assert(distancia(v, v, MetricaVec::EUCLIDIANA) == 0.0);
        assert(cerca(distancia(v, v, MetricaVec::COSENO), 0.0, 1e-9));
        Vector doble = v;
        for (float& x : doble) x *= 2;
        assert(cerca(distancia(v, doble, MetricaVec::COSENO), 0.0, 1e-9));
    }
    bool lanzo = false;
    try {
        distancia({1, 2}, {1, 2, 3}, MetricaVec::EUCLIDIANA);
    } catch (const std::runtime_error&) {
        lanzo = true;
    }
    assert(lanzo);
    std::cout << "propiedades: simetria, distancia cero a si mismo y dimensiones distintas\n";
}

// con vectores normalizados ||a - b||^2 = 2 (1 - cos) y -a.b = (1 - cos) - 1,
// así que las tres métricas dan el mismo k-NN (es lo que se usa con FAISS)
void prueba_normalizados() {
    auto vs = vectores_al_azar(500, 256, 11);
    for (auto& v : vs) normalizar(v);
    Vector q = vectores_al_azar(1, 256, 13)[0];
    normalizar(q);
    assert(cerca(motor::vectorial::norma(q), 1.0, 1e-6));
    for (const auto& v : vs) {
        const double coseno = distancia(v, q, MetricaVec::COSENO);
        const double e = distancia(v, q, MetricaVec::EUCLIDIANA);
        assert(cerca(e * e, 2 * coseno, 1e-5));
        assert(cerca(distancia(v, q, MetricaVec::PRODUCTO_PUNTO), coseno - 1, 1e-5));
    }
    const auto base = knn_fuerza_bruta(vs, q, 10, MetricaVec::COSENO);
    assert(knn_fuerza_bruta(vs, q, 10, MetricaVec::EUCLIDIANA) == base);
    assert(knn_fuerza_bruta(vs, q, 10, MetricaVec::PRODUCTO_PUNTO) == base);
    std::cout << "normalizados: las tres metricas dan los mismos 10 vecinos de 500\n";
}

void prueba_metrica_desde() {
    assert(metrica_vec_desde("EUCLIDIANA") == MetricaVec::EUCLIDIANA);
    assert(metrica_vec_desde("euclidean") == MetricaVec::EUCLIDIANA);
    assert(metrica_vec_desde("inner_product") == MetricaVec::PRODUCTO_PUNTO);
    assert(metrica_vec_desde("PRODUCTO_PUNTO") == MetricaVec::PRODUCTO_PUNTO);
    assert(metrica_vec_desde("cosine") == MetricaVec::COSENO);
    assert(metrica_vec_desde("COSENO") == MetricaVec::COSENO);
    bool lanzo = false;
    try {
        metrica_vec_desde("manhattan");
    } catch (const std::runtime_error&) {
        lanzo = true;
    }
    assert(lanzo);
}

}  // namespace

int main() {
    prueba_valores();
    prueba_propiedades();
    prueba_normalizados();
    prueba_metrica_desde();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
