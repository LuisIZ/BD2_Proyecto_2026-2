#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace motor {
namespace texto {

struct Posting {
    long long doc_id = 0;
    int tf = 0;
};

// índice invertido construido con SPIMI: bloques en memoria de hasta
// memoria_bloque bytes, escritos a disco y mezclados al final (merge k-way)
class IndiceInvertido {
public:
    explicit IndiceInvertido(const std::string& directorio, std::size_t memoria_bloque = 4 * 1024 * 1024)
        : directorio_(directorio), memoria_bloque_(memoria_bloque) {}

    // doc_id es la posición del registro en la tabla
    void agregar(long long /*doc_id*/, const std::string& /*texto*/) { no_implementado(); }

    // vacía el último bloque y mezcla todos los bloques en el índice final
    void construir() { no_implementado(); }

    // ordenados por doc_id
    std::vector<Posting> postings(const std::string& /*termino*/) { no_implementado(); }

    int df(const std::string& /*termino*/) { no_implementado(); }
    long num_docs() const { no_implementado(); }

    // cantidad de términos del documento después de tokenizar
    int largo_doc(long long /*doc_id*/) { no_implementado(); }
    double largo_promedio() const { no_implementado(); }

    // norma del vector lnc del documento, para el coseno de TF-IDF
    double norma_doc(long long /*doc_id*/) { no_implementado(); }

    long paginas_leidas() const { no_implementado(); }
    long tamano_en_disco() const { no_implementado(); }

private:
    std::string directorio_;
    std::size_t memoria_bloque_;

    [[noreturn]] static void no_implementado() { throw std::runtime_error("no implementado"); }
};

}  // namespace texto
}  // namespace motor
