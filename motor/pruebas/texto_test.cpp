#include "../texto/indice_invertido.h"
#include "../texto/preprocesamiento.h"
#include "../texto/ranking.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using motor::texto::IndiceInvertido;
using motor::texto::Posting;
using motor::texto::Ranking;
using motor::texto::Resultado;
using motor::texto::tokenizar;

namespace {

using Frecuencias = std::map<std::string, int>;

const std::string DIRECTORIO = ".build/texto_test";

const std::vector<std::string> DOCUMENTOS = {
    "Stainless steel water bottle, keeps drinks cold for 24 hours",
    "Insulated coffee mug with lid, stainless steel",
    "Running shoes for men, lightweight and breathable",
    "Women's running shoes with cushioned sole",
    "Glass water bottle with silicone sleeve",
    "Leather wallet for men with coin pocket",
    "Wireless earbuds with charging case, water resistant",
    "Kids water bottle with straw, leak proof lid",
    "Trail running backpack with water bladder",
    "Ceramic coffee mug set of 4, dishwasher safe",
};

bool cerca(double obtenido, double esperado, double tolerancia) {
    return std::fabs(obtenido - esperado) <= tolerancia;
}

double tf_w(int tf) { return tf > 0 ? 1.0 + std::log10(tf) : 0.0; }

// coseno lnc.ltc: el documento usa solo tf_w y la consulta tf_w * idf, ambos normalizados
double lnc_ltc(const Frecuencias& doc, const Frecuencias& consulta, const Frecuencias& df, double n) {
    double norma_doc = 0.0;
    for (const auto& [t, tf] : doc) norma_doc += tf_w(tf) * tf_w(tf);
    std::map<std::string, double> pesos;
    double norma_consulta = 0.0;
    for (const auto& [t, tf] : consulta) {
        if (!df.count(t)) continue;
        pesos[t] = tf_w(tf) * std::log10(n / df.at(t));
        norma_consulta += pesos[t] * pesos[t];
    }
    if (norma_doc == 0.0 || norma_consulta == 0.0) return 0.0;
    double score = 0.0;
    for (const auto& [t, w] : pesos)
        if (doc.count(t)) score += tf_w(doc.at(t)) * w;
    return score / (std::sqrt(norma_doc) * std::sqrt(norma_consulta));
}

double bm25(const Frecuencias& doc, int largo, double largo_promedio, const Frecuencias& consulta,
            const Frecuencias& df, double n) {
    const double k1 = motor::texto::BM25_K1, b = motor::texto::BM25_B;
    double score = 0.0;
    for (const auto& [t, tf_q] : consulta) {
        if (!doc.count(t)) continue;
        const double idf = std::log((n - df.at(t) + 0.5) / (df.at(t) + 0.5) + 1.0);
        const double tf = doc.at(t);
        score += idf * tf * (k1 + 1) / (tf + k1 * (1 - b + b * largo / largo_promedio));
    }
    return score;
}

Frecuencias contar(const std::vector<std::string>& terminos) {
    Frecuencias f;
    for (const auto& t : terminos) ++f[t];
    return f;
}

// el ejemplo de la clase: documento "seguro de carro, seguro de auto" y consulta "el mejor seguro de carro"
void prueba_ejemplo_clase() {
    const Frecuencias df{{"auto", 5000}, {"mejor", 50000}, {"carro", 10000}, {"seguro", 1000}};
    const Frecuencias doc{{"carro", 1}, {"seguro", 2}, {"auto", 1}};
    const Frecuencias consulta{{"mejor", 1}, {"carro", 1}, {"seguro", 1}};
    const double score = lnc_ltc(doc, consulta, df, 1000000);
    assert(cerca(score, 0.8, 0.01));
    std::cout << "ejemplo de la clase: score " << score << " (lnc.ltc)\n";
}

void prueba_tokenizar() {
    const std::vector<std::string> esperado{"car", "run", "fast"};
    assert(tokenizar("The CARS are running, fast!") == esperado);
    assert(tokenizar("CAFÉ, niño!") == std::vector<std::string>({"cafe", "nino"}));
    assert(tokenizar("").empty());
    assert(tokenizar("the and of").empty());
    std::cout << "tokenizar: minusculas, sin signos ni stopwords y con stemming\n";
}

struct Corpus {
    std::vector<Frecuencias> docs;
    std::vector<int> largos;
    Frecuencias df;
    double largo_promedio = 0.0;
};

Corpus corpus_en_memoria() {
    Corpus c;
    for (const auto& texto : DOCUMENTOS) {
        const auto terminos = tokenizar(texto);
        c.docs.push_back(contar(terminos));
        c.largos.push_back(static_cast<int>(terminos.size()));
        c.largo_promedio += terminos.size();
        for (const auto& [t, tf] : c.docs.back()) ++c.df[t];
    }
    c.largo_promedio /= DOCUMENTOS.size();
    return c;
}

void construir(IndiceInvertido& indice) {
    for (std::size_t i = 0; i < DOCUMENTOS.size(); ++i) indice.agregar(static_cast<long long>(i), DOCUMENTOS[i]);
    indice.construir();
}

// con un bloque de 256 bytes SPIMI escribe varios bloques y los mezcla
void prueba_postings() {
    std::filesystem::remove_all(DIRECTORIO);
    std::filesystem::create_directories(DIRECTORIO);
    const Corpus c = corpus_en_memoria();
    IndiceInvertido indice(DIRECTORIO, 256);
    construir(indice);
    assert(indice.num_docs() == static_cast<long>(DOCUMENTOS.size()));
    assert(cerca(indice.largo_promedio(), c.largo_promedio, 1e-9));
    for (std::size_t i = 0; i < DOCUMENTOS.size(); ++i) assert(indice.largo_doc(static_cast<long long>(i)) == c.largos[i]);
    for (const auto& [t, df] : c.df) {
        std::vector<Posting> esperado;
        for (std::size_t i = 0; i < c.docs.size(); ++i)
            if (c.docs[i].count(t)) esperado.push_back({static_cast<long long>(i), c.docs[i].at(t)});
        const auto obtenido = indice.postings(t);
        assert(indice.df(t) == df);
        assert(obtenido.size() == esperado.size());
        for (std::size_t j = 0; j < esperado.size(); ++j) {
            assert(obtenido[j].doc_id == esperado[j].doc_id);
            assert(obtenido[j].tf == esperado[j].tf);
        }
    }
    assert(indice.postings("inexistente").empty());
    assert(indice.df("inexistente") == 0);
    std::cout << "postings: " << c.df.size() << " terminos iguales a un indice en memoria, "
              << indice.tamano_en_disco() << " bytes en disco\n";
}

// cada documento devuelto tiene el score de la fuerza bruta y los scores van de mayor a menor
void comparar(const std::vector<Resultado>& obtenido, const std::vector<double>& scores, std::size_t k) {
    std::vector<double> ordenados;
    for (double s : scores)
        if (s > 0) ordenados.push_back(s);
    std::sort(ordenados.rbegin(), ordenados.rend());
    if (ordenados.size() > k) ordenados.resize(k);
    assert(obtenido.size() == ordenados.size());
    for (std::size_t i = 0; i < obtenido.size(); ++i) {
        assert(cerca(obtenido[i].score, ordenados[i], 1e-6));
        assert(cerca(obtenido[i].score, scores[static_cast<std::size_t>(obtenido[i].doc_id)], 1e-6));
    }
}

void prueba_ranking() {
    std::filesystem::remove_all(DIRECTORIO);
    std::filesystem::create_directories(DIRECTORIO);
    const Corpus c = corpus_en_memoria();
    IndiceInvertido indice(DIRECTORIO, 256);
    construir(indice);
    const double n = static_cast<double>(DOCUMENTOS.size());
    for (const std::string consulta : {"water", "stainless steel bottle", "running shoes for men with cushioned sole"}) {
        const Frecuencias q = contar(tokenizar(consulta));
        std::vector<double> tfidf, okapi;
        for (std::size_t i = 0; i < c.docs.size(); ++i) {
            tfidf.push_back(lnc_ltc(c.docs[i], q, c.df, n));
            okapi.push_back(bm25(c.docs[i], c.largos[i], c.largo_promedio, q, c.df, n));
        }
        comparar(motor::texto::buscar(indice, consulta, 3, Ranking::TF_IDF), tfidf, 3);
        comparar(motor::texto::buscar(indice, consulta, 3, Ranking::BM25), okapi, 3);
    }
    std::cout << "ranking: TF-IDF y BM25 iguales a la fuerza bruta con consultas de 1, 3 y 5+ palabras\n";
}

void prueba_ranking_desde() {
    assert(motor::texto::ranking_desde("TF_IDF") == Ranking::TF_IDF);
    assert(motor::texto::ranking_desde("BM25") == Ranking::BM25);
    bool lanzo = false;
    try {
        motor::texto::ranking_desde("PAGERANK");
    } catch (const std::runtime_error&) {
        lanzo = true;
    }
    assert(lanzo);
}

}  // namespace

int main() {
    prueba_ejemplo_clase();
    prueba_ranking_desde();
    prueba_tokenizar();
    prueba_postings();
    prueba_ranking();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
