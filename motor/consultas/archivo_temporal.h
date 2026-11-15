#pragma once

#include "external_algorithms.h"
#include "valor.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace motor {
namespace sql {

// Archivo temporal de filas en páginas de 4 KB, para los algoritmos externos (sort, hashing
// y join). Cada página empieza con la cantidad de filas (uint16) y sigue con las filas
// codificadas una tras otra; lo que sobra se rellena con ceros. Se escribe todo y después
// se lee página por página. El archivo vive en <base>/tmp/ y se borra en el destructor.
constexpr std::size_t PAGINA_TEMPORAL = 4096;
constexpr std::size_t CABECERA_TEMPORAL = sizeof(std::uint16_t);

namespace detalle_temporal {

enum Etiqueta : std::uint8_t { ENTERO = 1, DECIMAL = 2, TEXTO = 3, PUNTO = 4, VECTOR = 5 };

template <typename T>
void escribir(std::string& salida, T valor) {
    char bytes[sizeof(T)];
    std::memcpy(bytes, &valor, sizeof(T));
    salida.append(bytes, sizeof(T));
}

template <typename T>
T leer(const char* datos, std::size_t tam, std::size_t& pos) {
    if (pos + sizeof(T) > tam) throw std::runtime_error("fila temporal corrupta: faltan bytes");
    T valor;
    std::memcpy(&valor, datos + pos, sizeof(T));
    pos += sizeof(T);
    return valor;
}

inline std::uint16_t longitud(std::size_t n, const char* que) {
    if (n > PAGINA_TEMPORAL) throw std::runtime_error(std::string("la fila no cabe en una pagina: ") + que + " demasiado largo");
    return static_cast<std::uint16_t>(n);
}

}  // namespace detalle_temporal

// Cada Valor se guarda con su tipo, sin depender del esquema de una tabla:
// [n valores: uint16] y por valor [etiqueta: uint8][datos]
//   INT: int64 | FLOAT: float32 | VARCHAR: uint16 + bytes | POINT: 2 int32 | VECTOR: uint16 + float32 x n
inline std::string codificar_fila(const Fila& fila) {
    using namespace detalle_temporal;
    std::string salida;
    escribir<std::uint16_t>(salida, longitud(fila.size(), "la fila tiene demasiadas columnas"));
    for (const Valor& v : fila) {
        if (v.es_decimal) {
            escribir<std::uint8_t>(salida, DECIMAL);
            escribir<float>(salida, v.decimal);
        } else if (v.es_vector) {
            escribir<std::uint8_t>(salida, VECTOR);
            escribir<std::uint16_t>(salida, longitud(v.componentes.size(), "el vector es"));
            for (float c : v.componentes) escribir<float>(salida, c);
        } else if (v.es_punto) {
            escribir<std::uint8_t>(salida, PUNTO);
            escribir<std::int32_t>(salida, v.lat_e6);
            escribir<std::int32_t>(salida, v.lon_e6);
        } else if (v.es_entero) {
            escribir<std::uint8_t>(salida, ENTERO);
            escribir<std::int64_t>(salida, v.entero);
        } else {
            escribir<std::uint8_t>(salida, TEXTO);
            escribir<std::uint16_t>(salida, longitud(v.texto.size(), "el texto es"));
            salida += v.texto;
        }
    }
    if (salida.size() > PAGINA_TEMPORAL - CABECERA_TEMPORAL) {
        throw std::runtime_error("la fila no cabe en una pagina: ocupa " + std::to_string(salida.size()) +
                                 " bytes y el maximo es " + std::to_string(PAGINA_TEMPORAL - CABECERA_TEMPORAL));
    }
    return salida;
}

// lee una fila desde datos[pos] y deja pos al final de ella
inline Fila decodificar_fila(const char* datos, std::size_t tam, std::size_t& pos) {
    using namespace detalle_temporal;
    const auto n = leer<std::uint16_t>(datos, tam, pos);
    Fila fila;
    fila.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) {
        switch (leer<std::uint8_t>(datos, tam, pos)) {
            case ENTERO: fila.push_back(Valor::de_entero(leer<std::int64_t>(datos, tam, pos))); break;
            case DECIMAL: fila.push_back(Valor::de_decimal(leer<float>(datos, tam, pos))); break;
            case PUNTO: {
                const auto lat = leer<std::int32_t>(datos, tam, pos);
                fila.push_back(Valor::de_punto(lat, leer<std::int32_t>(datos, tam, pos)));
                break;
            }
            case VECTOR: {
                std::vector<float> componentes(leer<std::uint16_t>(datos, tam, pos));
                for (float& c : componentes) c = leer<float>(datos, tam, pos);
                fila.push_back(Valor::de_vector(std::move(componentes)));
                break;
            }
            case TEXTO: {
                const auto largo = leer<std::uint16_t>(datos, tam, pos);
                if (pos + largo > tam) throw std::runtime_error("fila temporal corrupta: faltan bytes");
                fila.push_back(Valor::de_texto(std::string(datos + pos, largo)));
                pos += largo;
                break;
            }
            default: throw std::runtime_error("fila temporal corrupta: tipo desconocido");
        }
    }
    return fila;
}

inline Fila decodificar_fila(const std::string& datos) {
    std::size_t pos = 0;
    return decodificar_fila(datos.data(), datos.size(), pos);
}

class ArchivoTemporal {
public:
    // traza (opcional) acumula las páginas escritas y leídas de todos los temporales de una consulta
    explicit ArchivoTemporal(const std::filesystem::path& base, PlanTrace* traza = nullptr) : traza_(traza) {
        static std::atomic<unsigned long long> contador{0};
        const auto dir = base / "tmp";
        std::filesystem::create_directories(dir);
        const auto marca = std::chrono::steady_clock::now().time_since_epoch().count();
        ruta_ = dir / ("temporal_" + std::to_string(marca) + "_" + std::to_string(contador++) + ".pag");
        escritura_.open(ruta_, std::ios::binary | std::ios::trunc);
        if (!escritura_) throw std::runtime_error("no se pudo crear el archivo temporal " + ruta_.string());
        escribiendo_ = true;
    }

    ArchivoTemporal(const ArchivoTemporal&) = delete;
    ArchivoTemporal& operator=(const ArchivoTemporal&) = delete;

    ArchivoTemporal(ArchivoTemporal&& otro) noexcept { mover(std::move(otro)); }
    ArchivoTemporal& operator=(ArchivoTemporal&& otro) noexcept {
        if (this != &otro) {
            borrar();
            mover(std::move(otro));
        }
        return *this;
    }

    ~ArchivoTemporal() { borrar(); }

    void agregar(const Fila& fila) {
        if (!escribiendo_) throw std::runtime_error("el archivo temporal ya se cerro para escritura");
        const std::string bytes = codificar_fila(fila);
        if (pagina_.size() + bytes.size() > PAGINA_TEMPORAL - CABECERA_TEMPORAL) volcar_pagina();
        pagina_ += bytes;
        ++filas_en_pagina_;
        ++filas_;
    }

    // escribe la última página (aunque esté a medias) y deja el archivo listo para leer
    void cerrar_escritura() {
        if (!escribiendo_) return;
        if (filas_en_pagina_ > 0) volcar_pagina();
        escritura_.close();
        if (!escritura_) throw std::runtime_error("no se pudo escribir el archivo temporal " + ruta_.string());
        escribiendo_ = false;
        reiniciar_lectura();
    }

    // vuelve a la primera página (por ejemplo, para recorrer otra vez la tabla interna de un join)
    void reiniciar_lectura() {
        if (escribiendo_) throw std::runtime_error("el archivo temporal sigue abierto para escritura");
        lectura_.close();
        lectura_.clear();
        lectura_.open(ruta_, std::ios::binary);
        if (!lectura_) throw std::runtime_error("no se pudo abrir el archivo temporal " + ruta_.string());
        restantes_en_pagina_ = 0;
    }

    // carga una página a la vez; false cuando ya no quedan filas
    bool siguiente(Fila& fila) {
        if (escribiendo_) throw std::runtime_error("el archivo temporal sigue abierto para escritura");
        if (restantes_en_pagina_ == 0 && !cargar_pagina()) return false;
        fila = decodificar_fila(buffer_, PAGINA_TEMPORAL, pos_);
        --restantes_en_pagina_;
        return true;
    }

    std::size_t filas() const { return filas_; }
    std::size_t paginas_escritas() const { return paginas_escritas_; }
    std::size_t paginas_leidas() const { return paginas_leidas_; }
    const std::filesystem::path& ruta() const { return ruta_; }

private:
    void volcar_pagina() {
        std::string pagina;
        pagina.reserve(PAGINA_TEMPORAL);
        detalle_temporal::escribir<std::uint16_t>(pagina, static_cast<std::uint16_t>(filas_en_pagina_));
        pagina += pagina_;
        pagina.resize(PAGINA_TEMPORAL, '\0');
        escritura_.write(pagina.data(), static_cast<std::streamsize>(pagina.size()));
        if (!escritura_) throw std::runtime_error("no se pudo escribir el archivo temporal " + ruta_.string());
        pagina_.clear();
        filas_en_pagina_ = 0;
        ++paginas_escritas_;
        if (traza_) ++traza_->paginas_escritas;
    }

    bool cargar_pagina() {
        while (true) {
            lectura_.read(buffer_, PAGINA_TEMPORAL);
            if (lectura_.gcount() == 0) return false;
            if (lectura_.gcount() != static_cast<std::streamsize>(PAGINA_TEMPORAL))
                throw std::runtime_error("archivo temporal corrupto: pagina incompleta");
            ++paginas_leidas_;
            if (traza_) ++traza_->paginas_leidas;
            pos_ = 0;
            restantes_en_pagina_ = detalle_temporal::leer<std::uint16_t>(buffer_, PAGINA_TEMPORAL, pos_);
            if (restantes_en_pagina_ > 0) return true;
        }
    }

    void borrar() noexcept {
        escritura_.close();
        lectura_.close();
        if (!ruta_.empty()) {
            std::error_code ec;
            std::filesystem::remove(ruta_, ec);
            ruta_.clear();
        }
    }

    void mover(ArchivoTemporal&& otro) noexcept {
        ruta_ = std::move(otro.ruta_);
        otro.ruta_.clear();
        traza_ = otro.traza_;
        escritura_ = std::move(otro.escritura_);
        lectura_ = std::move(otro.lectura_);
        pagina_ = std::move(otro.pagina_);
        filas_en_pagina_ = otro.filas_en_pagina_;
        filas_ = otro.filas_;
        paginas_escritas_ = otro.paginas_escritas_;
        paginas_leidas_ = otro.paginas_leidas_;
        escribiendo_ = otro.escribiendo_;
        std::memcpy(buffer_, otro.buffer_, PAGINA_TEMPORAL);
        pos_ = otro.pos_;
        restantes_en_pagina_ = otro.restantes_en_pagina_;
    }

    std::filesystem::path ruta_;
    PlanTrace* traza_ = nullptr;
    std::ofstream escritura_;
    std::ifstream lectura_;
    std::string pagina_;  // filas de la página en curso, sin la cabecera
    std::size_t filas_en_pagina_ = 0;
    std::size_t filas_ = 0;
    std::size_t paginas_escritas_ = 0;
    std::size_t paginas_leidas_ = 0;
    bool escribiendo_ = false;
    char buffer_[PAGINA_TEMPORAL];
    std::size_t pos_ = 0;
    std::size_t restantes_en_pagina_ = 0;
};

}  // namespace sql
}  // namespace motor
