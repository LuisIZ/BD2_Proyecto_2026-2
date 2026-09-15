#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace motor {

struct MetricasHash {
    long long construccion_ns = 0;
    std::size_t consultas = 0;
    long long consultas_ns = 0;
    std::size_t inserciones = 0;
    long long inserciones_ns = 0;
    std::size_t eliminaciones = 0;
    long long eliminaciones_ns = 0;
    std::size_t espacio_adicional_bytes = 0;

    double construccion_ms() const { return construccion_ns / 1'000'000.0; }
    double consulta_promedio_us() const {
        return consultas == 0 ? 0.0 : consultas_ns / static_cast<double>(consultas) / 1'000.0;
    }
    double insercion_promedio_us() const {
        return inserciones == 0 ? 0.0 : inserciones_ns / static_cast<double>(inserciones) / 1'000.0;
    }
    double eliminacion_promedio_us() const {
        return eliminaciones == 0 ? 0.0 : eliminaciones_ns / static_cast<double>(eliminaciones) / 1'000.0;
    }
};

inline std::filesystem::path ruta_log_hash(const std::filesystem::path& ruta = {}) {
    const auto destino = ruta.empty() ? std::filesystem::path("logs/extendible_hash_cpp.log") : ruta;
    if (!destino.parent_path().empty()) std::filesystem::create_directories(destino.parent_path());
    return destino;
}

inline void log_hash(const std::string& mensaje, const std::filesystem::path& ruta = {}) {
    std::ofstream archivo(ruta_log_hash(ruta), std::ios::app);
    if (archivo) {
        const auto ahora = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        archivo << ahora << " INFO " << mensaje << '\n';
    }
}

template <typename Clave, typename Valor, typename Hash = std::hash<Clave>>
class ExtendibleHashing {
public:
    explicit ExtendibleHashing(std::size_t capacidad_bucket = 4,
                               std::size_t profundidad_maxima = 64,
                               Hash hash = Hash(),
                               std::filesystem::path ruta_log = {})
        : capacidad_bucket_(capacidad_bucket),
          profundidad_maxima_(profundidad_maxima),
          hash_(std::move(hash)),
          ruta_log_(ruta_log_hash(ruta_log)),
          directorio_(1, std::make_shared<Bucket>(0)) {
        if (capacidad_bucket_ == 0) {
            throw std::invalid_argument("La capacidad del bucket debe ser positiva");
        }
        if (profundidad_maxima_ >= sizeof(std::size_t) * 8) {
            profundidad_maxima_ = sizeof(std::size_t) * 8 - 1;
        }
    }

    static ExtendibleHashing construir(const std::vector<std::pair<Clave, Valor>>& registros,
                                       std::size_t capacidad_bucket = 4,
                                       std::size_t profundidad_maxima = 64,
                                       Hash hash = Hash(),
                                       std::filesystem::path ruta_log = {}) {
        const auto inicio = reloj::now();
        ExtendibleHashing indice(capacidad_bucket, profundidad_maxima, std::move(hash), ruta_log);
        for (const auto& registro : registros) {
            indice.insertar(registro.first, registro.second);
        }
        indice.metricas_.construccion_ns = nanos(reloj::now() - inicio);
        indice.actualizar_espacio();
        log_hash("Indice hash construido: registros=" + std::to_string(indice.cantidad_) +
                     " tiempo_ms=" + std::to_string(indice.metricas_.construccion_ms()) +
                     " espacio_bytes=" + std::to_string(indice.metricas_.espacio_adicional_bytes),
                 indice.ruta_log_);
        return indice;
    }

    bool insertar(const Clave& clave, const Valor& valor) {
        const auto inicio = reloj::now();
        if (contiene(clave)) {
            registrar_insercion(inicio);
            return false;
        }
        while (true) {
            auto bucket = bucket_para(clave);
            if (bucket->registros.size() < capacidad_bucket_) {
                bucket->registros.emplace(clave, valor);
                ++cantidad_;
                registrar_insercion(inicio);
                return true;
            }
            if (!puede_separar(*bucket, clave)) {
                throw std::overflow_error("Las claves colisionan en todos los bits disponibles");
            }
            dividir(bucket);
        }
    }

    std::optional<Valor> buscar(const Clave& clave) const {
        const auto inicio = reloj::now();
        const auto bucket = bucket_para(clave);
        const auto encontrado = bucket->registros.find(clave);
        metricas_.consultas++;
        metricas_.consultas_ns += nanos(reloj::now() - inicio);
        if (encontrado == bucket->registros.end()) return std::nullopt;
        return encontrado->second;
    }

    bool contiene(const Clave& clave) const {
        const auto bucket = bucket_para(clave);
        return bucket->registros.find(clave) != bucket->registros.end();
    }

    bool eliminar(const Clave& clave) {
        const auto inicio = reloj::now();
        auto bucket = bucket_para(clave);
        const auto encontrado = bucket->registros.find(clave);
        if (encontrado == bucket->registros.end()) {
            registrar_eliminacion(inicio);
            return false;
        }
        bucket->registros.erase(encontrado);
        --cantidad_;
        fusionar(bucket);
        registrar_eliminacion(inicio);
        return true;
    }

    bool supportsRange() const noexcept { return false; }
    bool supports_range() const noexcept { return false; }
    std::size_t cantidad() const noexcept { return cantidad_; }
    std::size_t profundidad_global() const noexcept { return profundidad_global_; }
    std::size_t cantidad_buckets() const { return buckets_unicos().size(); }
    std::size_t capacidad_bucket() const noexcept { return capacidad_bucket_; }
    const MetricasHash& metricas() {
        actualizar_espacio();
        log_hash("Metricas del indice hash: consultas=" + std::to_string(metricas_.consultas) +
                     " inserciones=" + std::to_string(metricas_.inserciones) +
                     " eliminaciones=" + std::to_string(metricas_.eliminaciones) +
                     " espacio_bytes=" + std::to_string(metricas_.espacio_adicional_bytes),
                 ruta_log_);
        return metricas_;
    }

    void resetear_metricas() {
        const auto construccion = metricas_.construccion_ns;
        metricas_ = MetricasHash{};
        metricas_.construccion_ns = construccion;
        actualizar_espacio();
    }

private:
    using reloj = std::chrono::steady_clock;
    struct Bucket;
    using BucketPtr = std::shared_ptr<Bucket>;
    struct Bucket {
        explicit Bucket(std::size_t profundidad) : profundidad_local(profundidad) {}
        std::size_t profundidad_local;
        std::unordered_map<Clave, Valor, Hash> registros;
    };

    static long long nanos(reloj::duration duracion) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(duracion).count();
    }

    std::size_t hash_completo(const Clave& clave) const {
        return hash_(clave);
    }
    std::size_t indice(const Clave& clave) const {
        if (profundidad_global_ == 0) return 0;
        return hash_completo(clave) & ((std::size_t{1} << profundidad_global_) - 1);
    }
    BucketPtr bucket_para(const Clave& clave) const { return directorio_[indice(clave)]; }

    bool puede_separar(const Bucket& bucket, const Clave& clave) const {
        const auto primero = hash_completo(clave);
        for (const auto& registro : bucket.registros) {
            if (hash_completo(registro.first) != primero) return true;
        }
        return false;
    }

    void dividir(const BucketPtr& bucket) {
        if (bucket->profundidad_local >= profundidad_maxima_) {
            throw std::overflow_error("Se alcanzo la profundidad maxima del hash");
        }
        if (bucket->profundidad_local == profundidad_global_) {
            const auto copia = directorio_;
            directorio_.insert(directorio_.end(), copia.begin(), copia.end());
            ++profundidad_global_;
        }
        const auto nueva_profundidad = bucket->profundidad_local + 1;
        auto hermano = std::make_shared<Bucket>(nueva_profundidad);
        bucket->profundidad_local = nueva_profundidad;
        const auto bit = std::size_t{1} << (nueva_profundidad - 1);
        for (std::size_t i = 0; i < directorio_.size(); ++i) {
            if (directorio_[i] == bucket && (i & bit)) directorio_[i] = hermano;
        }
        auto registros = std::move(bucket->registros);
        bucket->registros = std::unordered_map<Clave, Valor, Hash>();
        for (auto& registro : registros) {
            bucket_para(registro.first)->registros.emplace(std::move(registro));
        }
    }

    void fusionar(BucketPtr bucket) {
        while (bucket->profundidad_local > 0) {
            auto posicion = std::find(directorio_.begin(), directorio_.end(), bucket);
            const auto indice_hermano = static_cast<std::size_t>(posicion - directorio_.begin()) ^
                                        (std::size_t{1} << (bucket->profundidad_local - 1));
            auto hermano = directorio_[indice_hermano];
            if (hermano == bucket || hermano->profundidad_local != bucket->profundidad_local ||
                hermano->registros.size() + bucket->registros.size() > capacidad_bucket_) break;
            hermano->registros.insert(std::make_move_iterator(bucket->registros.begin()),
                                      std::make_move_iterator(bucket->registros.end()));
            hermano->profundidad_local--;
            for (auto& referencia : directorio_) if (referencia == bucket) referencia = hermano;
            bucket = hermano;
        }
        while (profundidad_global_ > 0) {
            bool hay_profundidad_maxima = false;
            for (const auto& bucket_unico : buckets_unicos()) {
                if (bucket_unico->profundidad_local == profundidad_global_) {
                    hay_profundidad_maxima = true;
                    break;
                }
            }
            if (hay_profundidad_maxima) break;
            directorio_.resize(directorio_.size() / 2);
            --profundidad_global_;
        }
    }

    std::vector<BucketPtr> buckets_unicos() const {
        std::vector<BucketPtr> resultado;
        for (const auto& bucket : directorio_) {
            if (std::find(resultado.begin(), resultado.end(), bucket) == resultado.end()) {
                resultado.push_back(bucket);
            }
        }
        return resultado;
    }

    void actualizar_espacio() {
        std::size_t espacio = directorio_.capacity() * sizeof(BucketPtr);
        for (const auto& bucket : buckets_unicos()) {
            espacio += sizeof(Bucket) + bucket->registros.size() * (sizeof(Clave) + sizeof(Valor));
        }
        metricas_.espacio_adicional_bytes = espacio;
    }
    void registrar_insercion(reloj::time_point inicio) {
        ++metricas_.inserciones;
        metricas_.inserciones_ns += nanos(reloj::now() - inicio);
    }
    void registrar_eliminacion(reloj::time_point inicio) {
        ++metricas_.eliminaciones;
        metricas_.eliminaciones_ns += nanos(reloj::now() - inicio);
    }

    std::size_t capacidad_bucket_;
    std::size_t profundidad_maxima_;
    Hash hash_;
    std::filesystem::path ruta_log_;
    std::size_t profundidad_global_ = 0;
    std::size_t cantidad_ = 0;
    std::vector<BucketPtr> directorio_;
    mutable MetricasHash metricas_;
};

}  // namespace motor