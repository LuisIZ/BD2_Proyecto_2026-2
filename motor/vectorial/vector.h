#pragma once

#include <cctype>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace motor {
namespace vectorial {

using Vector = std::vector<float>;

enum class MetricaVec { EUCLIDIANA, PRODUCTO_PUNTO, COSENO };

// acepta el nombre en español o el de WITH METRIC=... (euclidean, inner_product, cosine)
inline MetricaVec metrica_vec_desde(const std::string& nombre) {
    std::string n;
    for (char c : nombre) n += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (n == "EUCLIDIANA" || n == "EUCLIDEAN" || n == "L2") return MetricaVec::EUCLIDIANA;
    if (n == "PRODUCTO_PUNTO" || n == "INNER_PRODUCT" || n == "DOT" || n == "IP") return MetricaVec::PRODUCTO_PUNTO;
    if (n == "COSENO" || n == "COSINE") return MetricaVec::COSENO;
    throw std::runtime_error("metrica desconocida: " + nombre);
}

inline double producto_punto(const Vector& a, const Vector& b) {
    double suma = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) suma += static_cast<double>(a[i]) * b[i];
    return suma;
}

inline double norma(const Vector& a) { return std::sqrt(producto_punto(a, a)); }

// deja el vector con norma 1 (igual que faiss.normalize_L2); el vector cero no cambia
inline void normalizar(Vector& a) {
    const double n = norma(a);
    if (n == 0.0) return;
    for (float& x : a) x = static_cast<float>(x / n);
}

// menor = más parecido en las tres: euclidiana, -a·b y 1 - coseno
inline double distancia(const Vector& a, const Vector& b, MetricaVec metrica) {
    if (a.size() != b.size()) throw std::runtime_error("vectores de dimensiones distintas");
    if (metrica == MetricaVec::PRODUCTO_PUNTO) return -producto_punto(a, b);
    if (metrica == MetricaVec::COSENO) {
        const double na = norma(a), nb = norma(b);
        if (na == 0.0 || nb == 0.0) return 1.0;
        return 1.0 - producto_punto(a, b) / (na * nb);
    }
    double suma = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        suma += d * d;
    }
    return std::sqrt(suma);
}

}  // namespace vectorial
}  // namespace motor
