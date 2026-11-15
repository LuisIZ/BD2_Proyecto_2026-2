#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace motor {

struct PlanEvent {
    std::string algorithm;
    std::map<std::string, std::string> details;
};

class PlanTrace {
public:
    explicit PlanTrace(std::filesystem::path ruta_log = {})
        : ruta_log_(ruta_log.empty() ? std::filesystem::path("logs/external_algorithms_cpp.log")
                                     : std::move(ruta_log)) {
        if (!ruta_log_.parent_path().empty()) {
            std::filesystem::create_directories(ruta_log_.parent_path());
        }
    }

    void record(const std::string& algorithm,
                std::map<std::string, std::string> details = {}) {
        events.push_back({algorithm, details});
        std::ofstream log(ruta_log_, std::ios::app);
        if (log) {
            log << "INFO Plan externo: " << algorithm;
            for (const auto& [clave, valor] : details) log << ' ' << clave << '=' << valor;
            log << '\n';
        }
    }

    const PlanEvent& last(const std::string& algorithm = {}) const {
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            if (algorithm.empty() || it->algorithm == algorithm) return *it;
        }
        throw std::out_of_range("La traza no contiene eventos");
    }

    std::vector<PlanEvent> events;
    std::filesystem::path ruta_log_;
    // páginas de 4 KB de los archivos temporales de la consulta (ver archivo_temporal.h)
    std::size_t paginas_escritas = 0;
    std::size_t paginas_leidas = 0;
};

// corrida guardada en memoria; las filas del ejecutor usan ArchivoTemporal (archivo_temporal.h)
template <typename Registro>
class CorridaEnMemoria {
public:
    void agregar(const Registro& registro) { registros_.push_back(registro); }
    void cerrar_escritura() {}
    bool siguiente(Registro& registro) {
        if (pos_ == registros_.size()) return false;
        registro = registros_[pos_];
        ++pos_;
        return true;
    }

private:
    std::vector<Registro> registros_;
    std::size_t pos_ = 0;
};

template <typename Registro, typename Clave, typename Corrida = CorridaEnMemoria<Registro>>
class ExternalMergeSort {
public:
    using KeyFunction = std::function<Clave(const Registro&)>;
    using NuevaCorrida = std::function<Corrida()>;

    explicit ExternalMergeSort(std::size_t buffer_pages = 10,
                               std::size_t records_per_page = 100,
                               std::size_t k = 0,
                               PlanTrace* trace = nullptr,
                               NuevaCorrida nueva_corrida = [] { return Corrida(); })
        : buffer_pages_(buffer_pages), records_per_page_(records_per_page),
          k_(k == 0 ? buffer_pages - 1 : k), trace_(trace), nueva_corrida_(std::move(nueva_corrida)) {
        if (buffer_pages_ < 3 || records_per_page_ == 0 || k_ < 2 || k_ > buffer_pages_ - 1) {
            throw std::invalid_argument("Presupuesto o k invalido para external merge sort");
        }
    }

    std::vector<Registro> ordenar(std::vector<Registro> records,
                                  const KeyFunction& key,
                                  bool reverse = false) {
        const std::size_t escritas_antes = trace_ ? trace_->paginas_escritas : 0;
        const std::size_t leidas_antes = trace_ ? trace_->paginas_leidas : 0;

        // fase 1: corridas de B páginas
        const auto chunk_size = buffer_pages_ * records_per_page_;
        std::vector<Corrida> corridas;
        for (std::size_t inicio = 0; inicio < records.size(); inicio += chunk_size) {
            const auto fin = std::min(records.size(), inicio + chunk_size);
            ordenar_run(records.begin() + inicio, records.begin() + fin, key, reverse);
            Corrida corrida = nueva_corrida_();
            for (std::size_t i = inicio; i < fin; ++i) {
                corrida.agregar(records[i]);
            }
            corrida.cerrar_escritura();
            corridas.push_back(std::move(corrida));
        }
        records.clear();
        const auto initial_runs = corridas.size();

        // fase 2: pasadas de k corridas hasta que quede una
        std::size_t merge_passes = 0;
        while (corridas.size() > 1) {
            std::vector<Corrida> siguientes;
            for (std::size_t inicio = 0; inicio < corridas.size(); inicio += k_) {
                const auto fin = std::min(corridas.size(), inicio + k_);
                std::vector<Corrida> grupo;
                for (std::size_t i = inicio; i < fin; ++i) {
                    grupo.push_back(std::move(corridas[i]));
                }
                siguientes.push_back(merge_k_way(grupo, key, reverse));
            }
            corridas = std::move(siguientes);
            ++merge_passes;
        }

        // resultado
        std::vector<Registro> resultado;
        Registro registro;
        while (!corridas.empty() && corridas[0].siguiente(registro)) {
            resultado.push_back(registro);
        }
        corridas.clear();
        if (trace_) {
            trace_->record("external_merge_sort", {
                {"buffer_pages", std::to_string(buffer_pages_)},
                {"records_per_page", std::to_string(records_per_page_)},
                {"k", std::to_string(k_)},
                {"initial_runs", std::to_string(initial_runs)},
                {"merge_passes", std::to_string(merge_passes)},
                {"records", std::to_string(resultado.size())},
                {"paginas_escritas", std::to_string(trace_->paginas_escritas - escritas_antes)},
                {"paginas_leidas", std::to_string(trace_->paginas_leidas - leidas_antes)},
            });
        }
        return resultado;
    }

    std::size_t k() const noexcept { return k_; }

private:
    struct HeapEntry {
        Clave key;
        std::size_t run;
    };
    // con claves iguales sale primero la corrida anterior, así el orden es estable
    struct HeapCompare {
        bool reverse;
        bool operator()(const HeapEntry& left, const HeapEntry& right) const {
            if (left.key < right.key) return reverse;
            if (right.key < left.key) return !reverse;
            return left.run > right.run;
        }
    };

    using Iterador = typename std::vector<Registro>::iterator;

    static void ordenar_run(Iterador inicio, Iterador fin, const KeyFunction& key, bool reverse) {
        std::stable_sort(inicio, fin, [&](const Registro& left, const Registro& right) {
            return reverse ? key(left) > key(right) : key(left) < key(right);
        });
    }

    // una página de entrada por corrida y una de salida
    Corrida merge_k_way(std::vector<Corrida>& grupo, const KeyFunction& key, bool reverse) {
        Corrida salida = nueva_corrida_();
        std::vector<Registro> actuales(grupo.size());
        std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapCompare> heap{HeapCompare{reverse}};
        for (std::size_t run = 0; run < grupo.size(); ++run) {
            if (grupo[run].siguiente(actuales[run])) heap.push({key(actuales[run]), run});
        }
        while (!heap.empty()) {
            const auto entry = heap.top();
            heap.pop();
            salida.agregar(actuales[entry.run]);
            if (grupo[entry.run].siguiente(actuales[entry.run])) {
                heap.push({key(actuales[entry.run]), entry.run});
            }
        }
        salida.cerrar_escritura();
        return salida;
    }

    std::size_t buffer_pages_;
    std::size_t records_per_page_;
    std::size_t k_;
    PlanTrace* trace_;
    NuevaCorrida nueva_corrida_;
};

enum class AggregateOp { COUNT, SUM, AVG, MIN, MAX };  // los agregados del GROUP BY

// el GROUP BY con external hashing en disco esta en hash_externo.h (usa ArchivoTemporal)

template <typename Left, typename Right, typename Key>
std::vector<std::pair<Left, Right>> hash_join(
    const std::vector<Left>& left,
    const std::vector<Right>& right,
    const std::function<Key(const Left&)>& left_key,
    const std::function<Key(const Right&)>& right_key) {
    std::unordered_map<Key, std::vector<Right>> hash_table;
    for (const auto& record : right) hash_table[right_key(record)].push_back(record);
    std::vector<std::pair<Left, Right>> result;
    for (const auto& left_record : left) {
        for (const auto& right_record : hash_table[left_key(left_record)]) {
            result.push_back({left_record, right_record});
        }
    }
    return result;
}

template <typename Outer, typename Inner, typename Key>
std::vector<std::pair<Outer, Inner>> index_nested_loop_join(
    const std::vector<Outer>& outer,
    const std::function<std::vector<Inner>(const Key&)>& index,
    const std::function<Key(const Outer&)>& outer_key) {
    std::vector<std::pair<Outer, Inner>> result;
    for (const auto& outer_record : outer) {
        for (const auto& match : index(outer_key(outer_record))) {
            result.push_back({outer_record, match});
        }
    }
    return result;
}

class JoinPlanner {
public:
    explicit JoinPlanner(PlanTrace* trace = nullptr) : trace_(trace) {}

    std::string choose(std::size_t outer_rows, std::size_t inner_rows, bool index_available) {
        const std::string algorithm = index_available && outer_rows <= inner_rows
                                          ? "index_nested_loop_join"
                                          : "hash_join";
        if (trace_) {
            trace_->record(algorithm, {
                {"outer_rows", std::to_string(outer_rows)},
                {"inner_rows", std::to_string(inner_rows)},
                {"index_available", index_available ? "true" : "false"},
            });
        }
        return algorithm;
    }

private:
    PlanTrace* trace_;
};

}  // namespace motor