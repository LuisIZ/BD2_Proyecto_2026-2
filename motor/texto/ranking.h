#pragma once

#include "indice_invertido.h"

#include <algorithm>
#include <cmath>
#include <map>
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
// TF_IDF: esquema lnc.ltc con coseno; BM25: k1 y b solo se usan con BM25
inline std::vector<Resultado> buscar(IndiceInvertido& indice, const std::string& consulta, int k,
                                     Ranking ranking, double k1 = BM25_K1, double b = BM25_B) {
    if (k < 0) throw std::invalid_argument("k no puede ser negativo");
    if (k1 <= 0.0) throw std::invalid_argument("k1 debe ser mayor que cero");
    if (b < 0.0 || b > 1.0) throw std::invalid_argument("b debe estar entre cero y uno");
    if (k == 0 || indice.num_docs() == 0) return {};

    std::map<std::string, int> tf_consulta;
    for (const std::string& termino : tokenizar(consulta)) ++tf_consulta[termino];
    if (tf_consulta.empty()) return {};

    const double n = static_cast<double>(indice.num_docs());
    const double largo_promedio = indice.largo_promedio();
    std::map<long long, double> scores;
    double norma_consulta = 0.0;
    for (const auto& [termino, tfq] : tf_consulta) {
        const int frecuencia = indice.df(termino);
        if (frecuencia == 0) continue;
        const std::vector<Posting> lista = indice.postings(termino);
        if (ranking == Ranking::TF_IDF) {
            const double idf = std::log10(n / static_cast<double>(frecuencia));
            const double peso_q = (1.0 + std::log10(static_cast<double>(tfq))) * idf;
            norma_consulta += peso_q * peso_q;
            for (const Posting& posting : lista) {
                const double peso_doc = 1.0 + std::log10(static_cast<double>(posting.tf));
                scores[posting.doc_id] += peso_doc * peso_q;
            }
        } else {
            const double idf = std::log((n - frecuencia + 0.5) / (frecuencia + 0.5) + 1.0);
            for (const Posting& posting : lista) {
                const double normalizacion = largo_promedio == 0.0
                    ? 1.0
                    : 1.0 - b + b * static_cast<double>(indice.largo_doc(posting.doc_id)) / largo_promedio;
                const double tf = static_cast<double>(posting.tf);
                scores[posting.doc_id] += idf * tf * (k1 + 1.0) / (tf + k1 * normalizacion);
            }
        }
    }

    std::vector<Resultado> resultados;
    resultados.reserve(scores.size());
    for (const auto& [doc_id, puntaje] : scores) {
        const double normalizado = ranking == Ranking::TF_IDF
            ? (norma_consulta == 0.0 || indice.norma_doc(doc_id) == 0.0
                   ? 0.0
                   : puntaje / (std::sqrt(norma_consulta) * indice.norma_doc(doc_id)))
            : puntaje;
        if (normalizado > 0.0) resultados.push_back({doc_id, normalizado});
    }
    std::sort(resultados.begin(), resultados.end(), [](const Resultado& a, const Resultado& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.doc_id < b.doc_id;
    });
    if (resultados.size() > static_cast<std::size_t>(k)) resultados.resize(static_cast<std::size_t>(k));
    return resultados;
}

}  // namespace texto
}  // namespace motor
