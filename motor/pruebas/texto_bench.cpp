// Comparación experimental de la Parte 3 (sección 2.3.2): índice invertido SPIMI con
// ranking TF-IDF (coseno) y BM25 sobre la descripción de los productos de ABO.
//
//   texto_bench [--salida datos/resultados/texto_bench.csv] [--repeticiones 5]
//               [--relevancia docs/comparacion_texto/consultas_relevancia.csv]
//               [--ranking datos/resultados/texto_ranking.csv]
//
// Para cada n (1k, 10k y 100k, si está el CSV) construye el índice y lo reabre desde
// disco, y mide las mismas consultas de 1, 3 y 5 o más palabras con los dos rankings.
// Las consultas son las de datos/resultados/gin_bench.sql, para comparar con GIN.
// Con el índice de 10k guarda además el top 10 de las 30 consultas con juicios de
// relevancia, que evalúa docs/comparacion_texto/relevancia.py (Precision@10 y Recall@10).

#include "../texto/indice_invertido.h"
#include "../texto/ranking.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using motor::texto::buscar;
using motor::texto::IndiceInvertido;
using motor::texto::Ranking;

namespace {

using Reloj = std::chrono::steady_clock;

// 10 consultas por largo, con palabras de los productos de ABO (en inglés)
const std::vector<std::pair<int, std::vector<std::string>>> CONSULTAS = {
    {1, {"shoes", "sofa", "coffee", "lamp", "backpack", "rug", "chair", "bottle", "shirt", "pillow"}},
    {3,
     {"leather office chair", "stainless steel bottle", "wooden coffee table", "running shoes men",
      "cotton bath towel", "led desk lamp", "printed phone case", "memory foam pillow",
      "glass storage jar", "women ankle boots"}},
    {5,
     {"soft back case mobile cover", "modern upholstered sofa living room", "stainless steel insulated water bottle",
      "solid wood dining table with chairs", "women casual canvas sneakers lace up", "organic ground coffee medium roast blend",
      "queen size memory foam mattress topper", "outdoor patio lounge chair with cushion",
      "kids printed cotton t shirt", "non slip bath mat for bathroom floor"}},
};

std::vector<std::string> campos_csv(const std::string& linea) {
    std::vector<std::string> campos(1);
    bool comillas = false;
    for (std::size_t i = 0; i < linea.size(); ++i) {
        const char c = linea[i];
        if (c == '"') {
            if (comillas && i + 1 < linea.size() && linea[i + 1] == '"') {
                campos.back().push_back('"');
                ++i;
            } else {
                comillas = !comillas;
            }
        } else if (c == ',' && !comillas) {
            campos.emplace_back();
        } else {
            campos.back().push_back(c);
        }
    }
    return campos;
}

// (id, descripcion) de datos/productos_<n>.csv (lo genera tools/dataset/preparar.py)
std::vector<std::pair<long long, std::string>> leer_productos(const std::string& ruta) {
    std::ifstream entrada(ruta, std::ios::binary);
    if (!entrada) throw std::runtime_error("no se pudo abrir " + ruta);
    std::string linea;
    std::getline(entrada, linea);
    const std::vector<std::string> encabezado = campos_csv(linea);
    std::size_t col_id = encabezado.size(), col_desc = encabezado.size();
    for (std::size_t i = 0; i < encabezado.size(); ++i) {
        if (encabezado[i] == "id") col_id = i;
        if (encabezado[i] == "descripcion") col_desc = i;
    }
    if (col_id == encabezado.size() || col_desc == encabezado.size()) {
        throw std::runtime_error(ruta + " no tiene las columnas id y descripcion");
    }
    std::vector<std::pair<long long, std::string>> docs;
    while (std::getline(entrada, linea)) {
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.empty()) continue;
        const std::vector<std::string> campos = campos_csv(linea);
        docs.emplace_back(std::stoll(campos.at(col_id)), campos.at(col_desc));
    }
    return docs;
}

// (consulta, texto) de los juicios de relevancia
std::vector<std::pair<std::string, std::string>> leer_consultas(const std::string& ruta) {
    std::ifstream entrada(ruta, std::ios::binary);
    if (!entrada) throw std::runtime_error("no se pudo abrir " + ruta);
    std::string linea;
    std::getline(entrada, linea);
    std::vector<std::pair<std::string, std::string>> consultas;
    while (std::getline(entrada, linea)) {
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.empty()) continue;
        const std::vector<std::string> campos = campos_csv(linea);
        consultas.emplace_back(campos.at(0), campos.at(1));
    }
    return consultas;
}

double ms_desde(Reloj::time_point inicio) {
    return std::chrono::duration<double, std::milli>(Reloj::now() - inicio).count();
}

}  // namespace

int main(int argc, char** argv) {
    std::string salida = "datos/resultados/texto_bench.csv";
    std::string relevancia = "docs/comparacion_texto/consultas_relevancia.csv";
    std::string salida_ranking = "datos/resultados/texto_ranking.csv";
    int repeticiones = 5;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--salida" && i + 1 < argc) salida = argv[++i];
        else if (arg == "--repeticiones" && i + 1 < argc) repeticiones = std::stoi(argv[++i]);
        else if (arg == "--relevancia" && i + 1 < argc) relevancia = argv[++i];
        else if (arg == "--ranking" && i + 1 < argc) salida_ranking = argv[++i];
        else {
            std::cerr << "uso: texto_bench [--salida ruta.csv] [--repeticiones 5] [--relevancia consultas.csv]"
                         " [--ranking ranking.csv]\n";
            return 1;
        }
    }

    std::filesystem::create_directories(std::filesystem::path(salida).parent_path());
    std::ofstream csv(salida);
    csv << "estructura,n,operacion,palabras,tiempo_ms,resultados,paginas,postings,bytes\n";

    for (const auto& [n, nombre] : std::vector<std::pair<int, std::string>>{
             {1000, "1k"}, {10000, "10k"}, {100000, "100k"}}) {
        const std::string ruta = "datos/productos_" + nombre + ".csv";
        if (!std::filesystem::exists(ruta)) {
            std::cout << "se omite n = " << n << ": falta " << ruta << " (python tools/dataset/preparar.py)\n";
            continue;
        }
        const auto docs = leer_productos(ruta);
        const std::string directorio = ".build/texto_bench/" + nombre;
        std::filesystem::remove_all(directorio);

        // construcción: bloques de 4 MB, el valor por defecto del índice
        auto inicio = Reloj::now();
        {
            IndiceInvertido indice(directorio);
            for (const auto& [id, texto] : docs) indice.agregar(id, texto);
            indice.construir();
        }
        const double t_construccion = ms_desde(inicio);

        // las consultas se hacen sobre el índice reabierto desde disco
        IndiceInvertido indice(directorio);
        if (!indice.abrir()) throw std::runtime_error("no se pudo reabrir " + directorio);
        csv << "spimi," << n << ",construccion,," << t_construccion << ",," << ",," << indice.tamano_en_disco() << "\n";
        std::cout << "n=" << n << " construccion " << t_construccion << " ms, " << indice.tamano_en_disco()
                  << " bytes\n";

        // una pasada sin medir, para que el ranking que va primero no pague solo la caché fría
        for (const auto& grupo : CONSULTAS) {
            for (const std::string& consulta : grupo.second) buscar(indice, consulta, 10, Ranking::BM25);
        }

        for (const Ranking ranking : {Ranking::TF_IDF, Ranking::BM25}) {
            const std::string tecnica = ranking == Ranking::TF_IDF ? "tf_idf" : "bm25";
            for (const auto& [palabras, consultas] : CONSULTAS) {
                const long paginas_antes = indice.paginas_leidas();
                const long postings_antes = indice.postings_leidos();
                double resultados = 0;
                inicio = Reloj::now();
                for (int r = 0; r < repeticiones; ++r) {
                    for (const std::string& consulta : consultas) {
                        resultados += static_cast<double>(buscar(indice, consulta, 10, ranking).size());
                    }
                }
                const double veces = static_cast<double>(repeticiones * consultas.size());
                const double t = ms_desde(inicio) / veces;
                csv << tecnica << ',' << n << ",consulta," << palabras << ',' << t << ',' << resultados / veces << ','
                    << (indice.paginas_leidas() - paginas_antes) / veces << ','
                    << (indice.postings_leidos() - postings_antes) / veces << ",\n";
                std::cout << "  " << tecnica << " " << palabras << " palabra(s): " << t << " ms, "
                          << (indice.postings_leidos() - postings_antes) / veces << " postings\n";
            }
        }

        // top 10 de las consultas con juicios de relevancia, sobre el corpus de 10k
        if (n == 10000 && std::filesystem::exists(relevancia)) {
            std::ofstream ranking(salida_ranking);
            ranking << "tecnica,consulta,posicion,id,score\n";
            for (const auto& [consulta, texto] : leer_consultas(relevancia)) {
                for (const Ranking r : {Ranking::TF_IDF, Ranking::BM25}) {
                    const auto top = buscar(indice, texto, 10, r);
                    for (std::size_t p = 0; p < top.size(); ++p) {
                        ranking << (r == Ranking::TF_IDF ? "tf_idf" : "bm25") << ',' << consulta << ',' << p + 1
                                << ',' << top[p].doc_id << ',' << top[p].score << '\n';
                    }
                }
            }
            std::cout << "  top 10 de " << relevancia << " en " << salida_ranking << "\n";
        }
    }
    std::cout << "resultados en " << salida << "\n";
    return 0;
}
