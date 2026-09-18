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
};

template <typename Registro, typename Clave>
class ExternalMergeSort {
public:
    using KeyFunction = std::function<Clave(const Registro&)>;

    explicit ExternalMergeSort(std::size_t buffer_pages = 10,
                               std::size_t records_per_page = 100,
                               std::size_t k = 0,
                               PlanTrace* trace = nullptr)
        : buffer_pages_(buffer_pages), records_per_page_(records_per_page),
          k_(k == 0 ? buffer_pages - 1 : k), trace_(trace) {
        if (buffer_pages_ < 3 || records_per_page_ == 0 || k_ < 2 || k_ > buffer_pages_ - 1) {
            throw std::invalid_argument("Presupuesto o k invalido para external merge sort");
        }
    }

    std::vector<Registro> ordenar(std::vector<Registro> records,
                                  const KeyFunction& key,
                                  bool reverse = false) {
        const auto chunk_size = buffer_pages_ * records_per_page_;
        std::vector<std::vector<Registro>> runs;
        for (std::size_t inicio = 0; inicio < records.size(); inicio += chunk_size) {
            const auto fin = std::min(records.size(), inicio + chunk_size);
            runs.emplace_back(records.begin() + inicio, records.begin() + fin);
            ordenar_run(runs.back(), key, reverse);
        }
        const auto initial_runs = runs.size();
        std::size_t merge_passes = 0;
        while (runs.size() > k_) {
            std::vector<std::vector<Registro>> siguiente;
            for (std::size_t inicio = 0; inicio < runs.size(); inicio += k_) {
                const auto fin = std::min(runs.size(), inicio + k_);
                std::vector<std::vector<Registro>> grupo(runs.begin() + inicio, runs.begin() + fin);
                siguiente.push_back(merge_k_way(grupo, key, reverse));
            }
            runs = std::move(siguiente);
            ++merge_passes;
        }
        std::vector<Registro> resultado = merge_k_way(runs, key, reverse);
        if (trace_) {
            trace_->record("external_merge_sort", {
                {"buffer_pages", std::to_string(buffer_pages_)},
                {"records_per_page", std::to_string(records_per_page_)},
                {"k", std::to_string(k_)},
                {"initial_runs", std::to_string(initial_runs)},
                {"merge_passes", std::to_string(merge_passes)},
                {"records", std::to_string(resultado.size())},
            });
        }
        return resultado;
    }

    std::size_t k() const noexcept { return k_; }

private:
    struct HeapEntry {
        Clave key;
        std::size_t run;
        std::size_t position;
    };
    struct HeapCompare {
        bool reverse;
        bool operator()(const HeapEntry& left, const HeapEntry& right) const {
            return reverse ? left.key < right.key : left.key > right.key;
        }
    };

    static void ordenar_run(std::vector<Registro>& run, const KeyFunction& key, bool reverse) {
        std::sort(run.begin(), run.end(), [&](const Registro& left, const Registro& right) {
            return reverse ? key(left) > key(right) : key(left) < key(right);
        });
    }

    static std::vector<Registro> merge_k_way(const std::vector<std::vector<Registro>>& runs,
                                             const KeyFunction& key,
                                             bool reverse) {
        std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapCompare> heap{HeapCompare{reverse}};
        for (std::size_t run = 0; run < runs.size(); ++run) {
            if (!runs[run].empty()) heap.push({key(runs[run][0]), run, 0});
        }
        std::vector<Registro> resultado;
        while (!heap.empty()) {
            const auto entry = heap.top();
            heap.pop();
            resultado.push_back(runs[entry.run][entry.position]);
            if (entry.position + 1 < runs[entry.run].size()) {
                const auto siguiente = entry.position + 1;
                heap.push({key(runs[entry.run][siguiente]), entry.run, siguiente});
            }
        }
        return resultado;
    }

    std::size_t buffer_pages_;
    std::size_t records_per_page_;
    std::size_t k_;
    PlanTrace* trace_;
};

enum class AggregateOp { COUNT, SUM, AVG, MIN, MAX };

struct AggregateResult {
    std::size_t count = 0;
    double sum = 0.0;
    double average = 0.0;
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
};

template <typename Registro, typename Grupo>
class ExternalHashAggregate {
public:
    using GroupFunction = std::function<Grupo(const Registro&)>;
    using ValueFunction = std::function<double(const Registro&)>;

    explicit ExternalHashAggregate(std::size_t buffer_pages = 10,
                                   std::size_t records_per_page = 100,
                                   PlanTrace* trace = nullptr)
        : buffer_pages_(buffer_pages), records_per_page_(records_per_page), trace_(trace) {
        if (buffer_pages_ < 3 || records_per_page_ == 0) {
            throw std::invalid_argument("Presupuesto invalido para external hashing");
        }
    }

    std::unordered_map<Grupo, AggregateResult> agrupar(
        const std::vector<Registro>& records,
        const GroupFunction& group_key,
        const ValueFunction& value,
        const std::vector<AggregateOp>& operations) {
        const auto max_groups = buffer_pages_ * records_per_page_;
        const auto partitions = std::max<std::size_t>(buffer_pages_ - 1,
            (records.size() + max_groups - 1) / max_groups);
        std::vector<std::vector<std::pair<Grupo, double>>> buckets(partitions);
        std::hash<Grupo> hash;
        for (const auto& record : records) {
            const auto group = group_key(record);
            buckets[hash(group) % partitions].push_back({group, value(record)});
        }
        // Una particion por vez: el hash manda todas las filas de un grupo a la misma
        // particion, asi que agregarlas por separado da el mismo resultado y la tabla
        // de hash viva solo contiene los grupos de la particion en curso. Ese es el
        // punto del external hashing: acotar el conjunto de trabajo, no el resultado.
        std::unordered_map<Grupo, AggregateResult> result;
        std::size_t pico_en_memoria = 0;
        for (auto& partition : buckets) {
            std::unordered_map<Grupo, AggregateResult> parcial;
            for (const auto& [group, item] : partition) {
                auto& state = parcial[group];
                ++state.count;
                state.sum += item;
                state.minimum = std::min(state.minimum, item);
                state.maximum = std::max(state.maximum, item);
            }
            pico_en_memoria = std::max(pico_en_memoria, parcial.size());
            for (auto& [group, state] : parcial) {
                state.average = state.sum / state.count;
                result.emplace(group, state);  // ningun grupo cae en dos particiones
            }
            partition.clear();
            partition.shrink_to_fit();
        }
        if (trace_) {
            trace_->record("external_hash_aggregate", {
                {"buffer_pages", std::to_string(buffer_pages_)},
                {"partitions", std::to_string(partitions)},
                {"max_groups_in_memory", std::to_string(max_groups)},
                {"peak_groups_in_memory", std::to_string(pico_en_memoria)},
                {"records", std::to_string(records.size())},
                {"groups", std::to_string(result.size())},
                {"aggregates", std::to_string(operations.size())},
            });
        }
        return result;
    }

private:
    std::size_t buffer_pages_;
    std::size_t records_per_page_;
    PlanTrace* trace_;
};

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