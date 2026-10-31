#pragma once

#include "preprocesamiento.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace motor {
namespace texto {

struct Posting {
    long long doc_id = 0;
    int tf = 0;
};

// SPIMI: cada bloque conserva su diccionario hash local; el merge solo mantiene
// una entrada término -> offset para consultar el archivo final.
class IndiceInvertido {
public:
    explicit IndiceInvertido(const std::string& directorio, std::size_t memoria_bloque = 4 * 1024 * 1024)
        : directorio_(directorio), memoria_bloque_(memoria_bloque) {
        if (memoria_bloque_ == 0) throw std::invalid_argument("memoria_bloque debe ser mayor que cero");
        std::filesystem::create_directories(directorio_);
        ruta_final_ = (std::filesystem::path(directorio_) / "indice_spimi.bin").string();
    }

    // Los doc_id deben llegar en orden creciente para que los postings se agreguen ya ordenados.
    void agregar(long long doc_id, const std::string& texto) {
        if (construido_) throw std::runtime_error("no se puede agregar documentos despues de construir el indice");
        if (hay_doc_ && doc_id <= ultimo_doc_) throw std::invalid_argument("doc_id debe ser estrictamente creciente");

        const std::vector<std::string> tokens = tokenizar(texto);
        std::unordered_map<std::string, int> frecuencias;
        for (const std::string& termino : tokens) ++frecuencias[termino];

        std::size_t estimado = sizeof(doc_id) + sizeof(std::size_t);
        for (const auto& [termino, tf] : frecuencias) {
            (void)tf;
            estimado += termino.size() + sizeof(Posting) + sizeof(void*) * 4;
            if (bloque_.find(termino) != bloque_.end()) estimado -= termino.size() + sizeof(void*) * 4;
        }
        if (!bloque_.empty() && memoria_usada_ + estimado > memoria_bloque_) vaciar_bloque();

        for (const auto& [termino, tf] : frecuencias) {
            auto encontrado = bloque_.find(termino);
            if (encontrado == bloque_.end()) {
                memoria_usada_ += termino.size() + sizeof(Posting) + sizeof(void*) * 4;
                encontrado = bloque_.emplace(termino, std::vector<Posting>{}).first;
            } else {
                memoria_usada_ += sizeof(Posting);
            }
            encontrado->second.push_back({doc_id, tf});
        }

        largos_[doc_id] = static_cast<int>(tokens.size());
        ultimo_doc_ = doc_id;
        hay_doc_ = true;
        ++num_docs_;
        memoria_usada_ += sizeof(doc_id) + sizeof(std::size_t);
        if (memoria_usada_ >= memoria_bloque_) vaciar_bloque();
    }

    // Ordena términos por bloque y hace un merge externo, preservando el orden de doc_id.
    void construir() {
        if (construido_) return;
        if (!bloque_.empty()) vaciar_bloque();
        std::ofstream salida(ruta_final_, std::ios::binary | std::ios::trunc);
        if (!salida) throw std::runtime_error("no se pudo crear el indice final: " + ruta_final_);

        std::vector<LectorBloque> lectores;
        lectores.reserve(bloques_.size());
        for (std::size_t i = 0; i < bloques_.size(); ++i) {
            lectores.emplace_back(bloques_[i], i);
            lectores.back().leer_siguiente();
        }
        struct EntradaCola {
            std::string termino;
            std::size_t lector;
        };
        const auto mayor = [](const EntradaCola& a, const EntradaCola& b) { return a.termino > b.termino; };
        std::priority_queue<EntradaCola, std::vector<EntradaCola>, decltype(mayor)> cola(mayor);
        for (std::size_t i = 0; i < lectores.size(); ++i)
            if (lectores[i].activo) cola.push({lectores[i].termino, i});

        while (!cola.empty()) {
            const std::string termino = cola.top().termino;
            std::vector<std::size_t> grupo;
            while (!cola.empty() && cola.top().termino == termino) {
                grupo.push_back(cola.top().lector);
                cola.pop();
            }
            std::sort(grupo.begin(), grupo.end(), [&](std::size_t a, std::size_t b) {
                return lectores[a].numero_bloque < lectores[b].numero_bloque;
            });

            std::uint64_t total = 0;
            for (std::size_t i : grupo) total += lectores[i].cantidad;
            const std::streamoff posicion = salida.tellp();
            escribir_u32(salida, static_cast<std::uint32_t>(termino.size()));
            escribir_u64(salida, total);
            salida.write(termino.data(), static_cast<std::streamsize>(termino.size()));
            if (!salida) throw std::runtime_error("error escribiendo el termino en el indice final");
            directorio_terminos_[termino] = {posicion, static_cast<std::size_t>(total)};

            for (std::size_t i : grupo) {
                LectorBloque& lector = lectores[i];
                lector.entrada.seekg(lector.offset_postings);
                for (std::uint64_t p = 0; p < lector.cantidad; ++p) {
                    Posting posting = leer_posting(lector.entrada);
                    escribir_posting(salida, posting);
                    const double peso = 1.0 + std::log10(static_cast<double>(posting.tf));
                    suma_normas_[posting.doc_id] += peso * peso;
                }
                if (!lector.entrada) throw std::runtime_error("error leyendo postings de un bloque SPIMI");
                lector.leer_siguiente();
                if (lector.activo) cola.push({lector.termino, i});
            }
        }
        salida.close();
        if (!salida) throw std::runtime_error("error cerrando el indice final: " + ruta_final_);
        for (LectorBloque& lector : lectores) lector.entrada.close();
        for (const std::string& ruta : bloques_) {
            std::error_code error;
            std::filesystem::remove(ruta, error);
            if (error) throw std::runtime_error("no se pudo eliminar el bloque temporal: " + error.message());
        }
        bloques_.clear();
        const std::uintmax_t bytes = std::filesystem::file_size(ruta_final_);
        tamano_disco_ = static_cast<long>(bytes);
        construido_ = true;
    }

    std::vector<Posting> postings(const std::string& termino) {
        exigir_construido();
        const auto encontrado = directorio_terminos_.find(termino);
        if (encontrado == directorio_terminos_.end()) return {};

        std::ifstream archivo(ruta_final_, std::ios::binary);
        if (!archivo) throw std::runtime_error("no se pudo abrir el indice: " + ruta_final_);
        archivo.seekg(encontrado->second.offset);
        const std::uint32_t largo = leer_u32(archivo);
        const std::uint64_t cantidad = leer_u64(archivo);
        archivo.seekg(static_cast<std::streamoff>(largo), std::ios::cur);
        std::vector<Posting> salida;
        salida.reserve(static_cast<std::size_t>(cantidad));
        for (std::uint64_t i = 0; i < cantidad; ++i) salida.push_back(leer_posting(archivo));
        if (!archivo) throw std::runtime_error("indice truncado al leer postings");
        const std::size_t bytes = sizeof(largo) + sizeof(cantidad) + largo + cantidad * (sizeof(std::int64_t) + sizeof(std::int32_t));
        paginas_leidas_ += static_cast<long>((bytes + 4095) / 4096);
        return salida;
    }

    int df(const std::string& termino) {
        exigir_construido();
        const auto encontrado = directorio_terminos_.find(termino);
        return encontrado == directorio_terminos_.end() ? 0 : static_cast<int>(encontrado->second.df);
    }

    long num_docs() const { return num_docs_; }

    int largo_doc(long long doc_id) {
        exigir_construido();
        const auto encontrado = largos_.find(doc_id);
        return encontrado == largos_.end() ? 0 : encontrado->second;
    }

    double largo_promedio() const {
        if (num_docs_ == 0) return 0.0;
        std::size_t total = 0;
        for (const auto& [id, largo] : largos_) {
            (void)id;
            total += static_cast<std::size_t>(largo);
        }
        return static_cast<double>(total) / static_cast<double>(num_docs_);
    }

    double norma_doc(long long doc_id) {
        exigir_construido();
        const auto encontrado = suma_normas_.find(doc_id);
        return encontrado == suma_normas_.end() ? 0.0 : std::sqrt(encontrado->second);
    }

    long paginas_leidas() const { return paginas_leidas_; }
    long tamano_en_disco() const { return tamano_disco_; }

private:
    struct EntradaIndice {
        std::streamoff offset = 0;
        std::size_t df = 0;
    };

    struct LectorBloque {
        explicit LectorBloque(const std::string& ruta, std::size_t numero)
            : entrada(ruta, std::ios::binary), numero_bloque(numero) {
            if (!entrada) throw std::runtime_error("no se pudo abrir bloque SPIMI: " + ruta);
        }

        void leer_siguiente() {
            if (!entrada) throw std::runtime_error("error leyendo un bloque SPIMI");
            const int siguiente = entrada.peek();
            if (siguiente == std::char_traits<char>::eof()) { activo = false; return; }
            const std::uint32_t largo = leer_u32(entrada);
            cantidad = leer_u64(entrada);
            if (largo == 0 || largo > 1024 * 1024) throw std::runtime_error("termino corrupto en bloque SPIMI");
            termino.resize(largo);
            entrada.read(&termino[0], static_cast<std::streamsize>(largo));
            offset_postings = entrada.tellg();
            entrada.seekg(static_cast<std::streamoff>(cantidad * (sizeof(std::int64_t) + sizeof(std::int32_t))), std::ios::cur);
            if (!entrada) throw std::runtime_error("bloque SPIMI truncado");
            activo = true;
        }

        std::ifstream entrada;
        std::size_t numero_bloque;
        std::string termino;
        std::uint64_t cantidad = 0;
        std::streamoff offset_postings = 0;
        bool activo = false;
    };

    static void escribir_u32(std::ostream& archivo, std::uint32_t valor) {
        archivo.write(reinterpret_cast<const char*>(&valor), sizeof(valor));
        if (!archivo) throw std::runtime_error("error escribiendo entero de 32 bits");
    }
    static void escribir_u64(std::ostream& archivo, std::uint64_t valor) {
        archivo.write(reinterpret_cast<const char*>(&valor), sizeof(valor));
        if (!archivo) throw std::runtime_error("error escribiendo entero de 64 bits");
    }
    static std::uint32_t leer_u32(std::istream& archivo) {
        std::uint32_t valor = 0;
        archivo.read(reinterpret_cast<char*>(&valor), sizeof(valor));
        if (!archivo) throw std::runtime_error("bloque SPIMI truncado");
        return valor;
    }
    static std::uint64_t leer_u64(std::istream& archivo) {
        std::uint64_t valor = 0;
        archivo.read(reinterpret_cast<char*>(&valor), sizeof(valor));
        if (!archivo) throw std::runtime_error("bloque SPIMI truncado");
        return valor;
    }
    static Posting leer_posting(std::istream& archivo) {
        std::int64_t doc_id = 0;
        std::int32_t tf = 0;
        archivo.read(reinterpret_cast<char*>(&doc_id), sizeof(doc_id));
        archivo.read(reinterpret_cast<char*>(&tf), sizeof(tf));
        if (!archivo) throw std::runtime_error("posting truncado");
        return {static_cast<long long>(doc_id), static_cast<int>(tf)};
    }
    static void escribir_posting(std::ostream& archivo, const Posting& posting) {
        const std::int64_t doc_id = static_cast<std::int64_t>(posting.doc_id);
        const std::int32_t tf = static_cast<std::int32_t>(posting.tf);
        archivo.write(reinterpret_cast<const char*>(&doc_id), sizeof(doc_id));
        archivo.write(reinterpret_cast<const char*>(&tf), sizeof(tf));
        if (!archivo) throw std::runtime_error("error escribiendo posting");
    }

    void vaciar_bloque() {
        if (bloque_.empty()) return;
        std::vector<std::string> terminos;
        terminos.reserve(bloque_.size());
        for (const auto& [termino, lista] : bloque_) {
            (void)lista;
            terminos.push_back(termino);
        }
        std::sort(terminos.begin(), terminos.end());
        const std::string ruta = (std::filesystem::path(directorio_) / ("bloque_" + std::to_string(bloques_.size()) + ".bin")).string();
        std::ofstream archivo(ruta, std::ios::binary | std::ios::trunc);
        if (!archivo) throw std::runtime_error("no se pudo crear bloque SPIMI: " + ruta);
        for (const std::string& termino : terminos) {
            const auto& lista = bloque_.at(termino);
            escribir_u32(archivo, static_cast<std::uint32_t>(termino.size()));
            escribir_u64(archivo, static_cast<std::uint64_t>(lista.size()));
            archivo.write(termino.data(), static_cast<std::streamsize>(termino.size()));
            for (const Posting& posting : lista) escribir_posting(archivo, posting);
        }
        archivo.close();
        if (!archivo) throw std::runtime_error("error cerrando bloque SPIMI: " + ruta);
        bloques_.push_back(ruta);
        bloque_.clear();
        memoria_usada_ = 0;
    }

    void exigir_construido() const {
        if (!construido_) throw std::runtime_error("el indice invertido aun no se ha construido");
    }

    std::string directorio_;
    std::string ruta_final_;
    std::size_t memoria_bloque_;
    std::size_t memoria_usada_ = 0;
    std::unordered_map<std::string, std::vector<Posting>> bloque_;
    std::vector<std::string> bloques_;
    std::unordered_map<std::string, EntradaIndice> directorio_terminos_;
    std::unordered_map<long long, int> largos_;
    std::unordered_map<long long, double> suma_normas_;
    long num_docs_ = 0;
    long paginas_leidas_ = 0;
    long tamano_disco_ = 0;
    long long ultimo_doc_ = 0;
    bool hay_doc_ = false;
    bool construido_ = false;
};

}  // namespace texto
}  // namespace motor
