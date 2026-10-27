#pragma once

#include "indice_invertido.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace motor {
namespace texto {

enum class Ranking { TF_IDF, BM25 };

const double BM25_K1 = 1.2;
const double BM25_B = 0.75;

inline Ranking ranking_desde(const std::string& nombre) {
    if (nombre == "TF_IDF") return Ranking::TF_IDF;
    if (nombre == "BM25") return Ranking::BM25;
    throw std::runtime_error("ranking desconocido: " + nombre);
}

struct Resultado {
    long long doc_id = 0;
    double score = 0.0;
};

// los k documentos con mayor score, de mayor a menor
// TF_IDF: esquema lnc.ltc con coseno; BM25: con BM25_K1 y BM25_B
inline std::vector<Resultado> buscar(IndiceInvertido& /*indice*/, const std::string& /*consulta*/, int /*k*/,
                                     Ranking /*ranking*/) {
    throw std::runtime_error("no implementado");
}

}  // namespace texto
}  // namespace motor
