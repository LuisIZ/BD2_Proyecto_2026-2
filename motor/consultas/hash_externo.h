#pragma once

#include "archivo_temporal.h"
#include "external_algorithms.h"
#include "valor.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace motor {
namespace sql {

// GROUP BY con external hashing en disco (Garcia-Molina, cap. 15.8; Ramakrishnan, cap. 14.6).
//
// Con B páginas de memoria:
//   1. Partición: cada fila va a hash(grupo, semilla) % P, con P = B - 1, y se escribe en el
//      ArchivoTemporal de esa partición. Cada partición tiene una página de buffer: cuando se
//      llena, se escribe a disco. La fila se guarda reducida al grupo y a las columnas que piden
//      los agregados.
//   2. Agregación: se lee una partición a la vez y se agrega con una tabla hash en memoria. Si la
//      tabla pasa de B páginas, esa partición se vuelve a repartir con otra semilla, hasta
//      MAX_NIVELES niveles; si aun así no cabe, se lanza un error.
//
// Todos los agregados del SELECT se calculan en la misma pasada.

struct AgregadoPedido {
    AggregateOp op = AggregateOp::COUNT;
    int columna = -1;  // posición en la fila de entrada; -1 es COUNT(*)
};

struct EstadoAgregado {
    std::size_t count = 0;
    double sum = 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    double average() const { return count == 0 ? 0.0 : sum / static_cast<double>(count); }
};

struct EstadisticasHash {
    std::size_t particiones = 0;        // archivos temporales creados, en todos los niveles
    std::size_t niveles = 0;            // 1 si no hubo que volver a particionar
    std::size_t reparticiones = 0;      // particiones que se volvieron a repartir
    std::size_t grupos_en_memoria = 0;  // la tabla hash más grande que se tuvo a la vez
    std::size_t grupos = 0;
    std::size_t filas = 0;
    std::size_t paginas_escritas = 0;
    std::size_t paginas_leidas = 0;
};

class ExternalHashAggregate {
public:
    static constexpr std::size_t MAX_NIVELES = 3;
    using Emitir = std::function<void(const Valor& grupo, const std::vector<EstadoAgregado>&)>;

    // base: carpeta de la base (los temporales van a <base>/tmp); paginas_buffer es B
    ExternalHashAggregate(std::filesystem::path base, std::size_t paginas_buffer, PlanTrace* traza = nullptr)
        : base_(std::move(base)), b_(paginas_buffer), traza_(traza) {
        if (b_ < 3) throw std::invalid_argument("el external hashing necesita al menos 3 paginas de buffer");
    }

    // col_grupo = -1: un solo grupo con todas las filas (agregados sin GROUP BY).
    // emitir se llama una vez por grupo, partición por partición.
    EstadisticasHash agrupar(const std::vector<Fila>& filas, int col_grupo,
                             const std::vector<AgregadoPedido>& agregados, const Emitir& emitir) {
        stats_ = {};
        stats_.filas = filas.size();
        agregados_ = agregados.size();
        // la fila reducida: [grupo, valor del agregado 1, valor del agregado 2, ...]
        auto particiones = nuevas_particiones();
        for (const Fila& f : filas) {
            Fila reducida;
            reducida.reserve(1 + agregados.size());
            reducida.push_back(col_grupo < 0 ? Valor::de_entero(0) : f[static_cast<std::size_t>(col_grupo)]);
            for (const AgregadoPedido& a : agregados) {
                reducida.push_back(a.columna < 0 ? Valor::de_entero(0) : f[static_cast<std::size_t>(a.columna)]);
            }
            particiones[particion(reducida[0], 1)]->agregar(reducida);
        }
        for (auto& p : particiones) {
            p->cerrar_escritura();
            procesar(*p, 1, agregados, emitir);
            contar(*p);
            p.reset();  // borra el archivo apenas se usó
        }
        if (traza_) {
            traza_->record("external_hash_aggregate", {
                {"buffer_pages", std::to_string(b_)},
                {"partitions", std::to_string(stats_.particiones)},
                {"levels", std::to_string(stats_.niveles)},
                {"repartitions", std::to_string(stats_.reparticiones)},
                {"peak_groups_in_memory", std::to_string(stats_.grupos_en_memoria)},
                {"pages_written", std::to_string(stats_.paginas_escritas)},
                {"pages_read", std::to_string(stats_.paginas_leidas)},
                {"records", std::to_string(stats_.filas)},
                {"groups", std::to_string(stats_.grupos)},
                {"aggregates", std::to_string(agregados.size())},
            });
        }
        return stats_;
    }

    std::size_t particiones_por_nivel() const { return b_ - 1; }

    // bytes que ocupa un grupo en la tabla hash: su clave codificada y el estado de cada agregado
    std::size_t bytes_de_grupo(const Valor& grupo) const {
        return codificar_fila({grupo}).size() + agregados_ * sizeof(EstadoAgregado);
    }

private:
    using Particiones = std::vector<std::unique_ptr<ArchivoTemporal>>;

    Particiones nuevas_particiones() {
        Particiones particiones;
        for (std::size_t i = 0; i < b_ - 1; ++i) particiones.push_back(std::make_unique<ArchivoTemporal>(base_));
        stats_.particiones += particiones.size();
        return particiones;
    }

    // FNV-1a sobre el texto del grupo, mezclado con el nivel: cada nivel reparte distinto
    std::size_t particion(const Valor& grupo, std::size_t nivel) const {
        std::uint64_t h = 1469598103934665603ULL ^ (nivel * 0x9E3779B97F4A7C15ULL);
        for (unsigned char c : grupo.a_texto()) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        h ^= h >> 33;
        return static_cast<std::size_t>(h % (b_ - 1));
    }

    void contar(const ArchivoTemporal& p) {
        stats_.paginas_escritas += p.paginas_escritas();
        stats_.paginas_leidas += p.paginas_leidas();
    }

    struct Entrada {
        Valor grupo;
        std::vector<EstadoAgregado> estados;
    };

    // agrega una partición; si su tabla hash pasa de B páginas, la reparte en el nivel siguiente
    void procesar(ArchivoTemporal& entrada, std::size_t nivel, const std::vector<AgregadoPedido>& agregados,
                  const Emitir& emitir) {
        stats_.niveles = std::max(stats_.niveles, nivel);
        const std::size_t capacidad = b_ * PAGINA_TEMPORAL;
        std::unordered_map<std::string, Entrada> tabla;
        std::size_t bytes = 0;
        bool desborda = false;
        Fila f;
        while (entrada.siguiente(f)) {
            auto [it, nuevo] = tabla.try_emplace(f[0].a_texto());
            if (nuevo) {
                it->second.grupo = f[0];
                it->second.estados.resize(agregados.size());
                bytes += bytes_de_grupo(f[0]);
                if (bytes > capacidad) {
                    desborda = true;
                    break;
                }
            }
            for (std::size_t i = 0; i < agregados.size(); ++i) {
                EstadoAgregado& estado = it->second.estados[i];
                const double x = agregados[i].columna < 0 ? 0.0 : f[1 + i].numero();
                ++estado.count;
                estado.sum += x;
                estado.minimum = std::min(estado.minimum, x);
                estado.maximum = std::max(estado.maximum, x);
            }
        }
        if (!desborda) {
            stats_.grupos_en_memoria = std::max(stats_.grupos_en_memoria, tabla.size());
            stats_.grupos += tabla.size();
            for (const auto& [clave, e] : tabla) emitir(e.grupo, e.estados);
            return;
        }
        stats_.grupos_en_memoria = std::max(stats_.grupos_en_memoria, tabla.size());
        tabla.clear();
        if (nivel == MAX_NIVELES) {
            throw std::runtime_error("GROUP BY: una particion no cabe en " + std::to_string(b_) +
                                     " paginas de memoria despues de " + std::to_string(MAX_NIVELES) +
                                     " niveles de particion; hay demasiados grupos para el buffer");
        }
        // se relee la partición entera y se reparte con la semilla del nivel siguiente
        ++stats_.reparticiones;
        entrada.reiniciar_lectura();
        auto hijas = nuevas_particiones();
        while (entrada.siguiente(f)) hijas[particion(f[0], nivel + 1)]->agregar(f);
        for (auto& h : hijas) {
            h->cerrar_escritura();
            procesar(*h, nivel + 1, agregados, emitir);
            contar(*h);
            h.reset();
        }
    }

    std::filesystem::path base_;
    std::size_t b_;
    PlanTrace* traza_;
    std::size_t agregados_ = 0;
    EstadisticasHash stats_;
};

}  // namespace sql
}  // namespace motor
