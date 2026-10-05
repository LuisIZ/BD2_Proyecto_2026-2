#pragma once

#include "buffer_pool.h"
#include "gestor_paginas.h"
#include "../espacial/distancia.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace detalle_rtree {

using detalle_bplus_no_agrupado::PageId;
using detalle_bplus_no_agrupado::TAM_PAGINA;

const std::uint32_t MAGICO_RTREE = 0x52545245u;
const std::uint16_t VERSION_RTREE = 1;

// rectángulo en microgrados; un punto es un rectángulo de área cero
struct Mbr {
    std::int32_t min_lat;
    std::int32_t min_lon;
    std::int32_t max_lat;
    std::int32_t max_lon;
};

// en una hoja ref es la posición del registro; en un nodo interno, la página del hijo
struct EntradaRTree {
    Mbr mbr;
    std::int64_t ref;
};

struct CabeceraNodo {
    std::uint16_t es_hoja;
    std::uint16_t num;
    std::uint32_t reservado;
};

struct CabeceraRTree {
    std::uint32_t magico;
    std::uint16_t version;
    std::uint16_t altura;
    PageId raiz;
    std::uint32_t num_nodos;
    std::uint64_t num_entradas;
};

const int CAPACIDAD_NODO =
    (TAM_PAGINA - static_cast<int>(sizeof(CabeceraNodo))) / static_cast<int>(sizeof(EntradaRTree));
const int MINIMO_NODO = CAPACIDAD_NODO * 2 / 5;

static_assert(sizeof(EntradaRTree) == 24, "entrada de r-tree inesperada");
static_assert(sizeof(CabeceraNodo) == 8, "cabecera de nodo inesperada");
static_assert(std::is_trivially_copyable<EntradaRTree>::value, "EntradaRTree debe copiarse a la pagina");
static_assert(CAPACIDAD_NODO == 170, "capacidad de nodo inesperada");

inline Mbr mbr_de(motor::espacial::Punto p) { return {p.lat_e6, p.lon_e6, p.lat_e6, p.lon_e6}; }

inline Mbr unir(const Mbr& a, const Mbr& b) {
    return {std::min(a.min_lat, b.min_lat), std::min(a.min_lon, b.min_lon), std::max(a.max_lat, b.max_lat),
            std::max(a.max_lon, b.max_lon)};
}

inline double area(const Mbr& m) {
    return (static_cast<double>(m.max_lat) - m.min_lat) * (static_cast<double>(m.max_lon) - m.min_lon);
}

inline double margen(const Mbr& m) {
    return (static_cast<double>(m.max_lat) - m.min_lat) + (static_cast<double>(m.max_lon) - m.min_lon);
}

inline bool contiene(const Mbr& fuera, const Mbr& dentro) {
    return fuera.min_lat <= dentro.min_lat && fuera.min_lon <= dentro.min_lon && fuera.max_lat >= dentro.max_lat &&
           fuera.max_lon >= dentro.max_lon;
}

inline bool se_cortan(const Mbr& a, const Mbr& b) {
    return a.min_lat <= b.max_lat && b.min_lat <= a.max_lat && a.min_lon <= b.max_lon && b.min_lon <= a.max_lon;
}

// distancia del punto al punto más cercano del rectángulo: cota inferior para el k-NN
inline double distancia_minima(motor::espacial::Punto p, const Mbr& m, motor::espacial::Metrica metrica) {
    const motor::espacial::Punto cerca{std::clamp(p.lat_e6, m.min_lat, m.max_lat), std::clamp(p.lon_e6, m.min_lon, m.max_lon)};
    return motor::espacial::distancia_m(p, cerca, metrica);
}

}  // namespace detalle_rtree

// R-Tree de puntos 2D paginado en disco: nodos de 4 KB, inserción por menor
// agrandamiento y split cuadrático de Guttman
class RTree {
    using PageId = detalle_bplus_no_agrupado::PageId;
    using GestorPaginas = detalle_bplus_no_agrupado::GestorPaginas;
    using BufferPool = detalle_bplus_no_agrupado::BufferPool;
    using Mbr = detalle_rtree::Mbr;
    using Entrada = detalle_rtree::EntradaRTree;
    using Punto = motor::espacial::Punto;
    using Metrica = motor::espacial::Metrica;
    static constexpr PageId PAGINA_NULA = detalle_bplus_no_agrupado::PAGINA_NULA;
    static constexpr int TAM_PAGINA = detalle_bplus_no_agrupado::TAM_PAGINA;

public:
    explicit RTree(const std::string& ruta, bool truncar = false) : gestor_(ruta, truncar), pool_(gestor_) {
        if (gestor_.num_paginas == 0) crear();
        else abrir();
    }

    ~RTree() {
        try {
            guardar_cabecera();
            pool_.vaciar();
        } catch (...) {
        }
    }

    RTree(const RTree&) = delete;
    RTree& operator=(const RTree&) = delete;

    void insertar(Punto p, long long pos) {
        const Insercion r = insertar_en(cab_.raiz, {detalle_rtree::mbr_de(p), pos});
        if (r.partio) {
            const PageId raiz = nuevo_nodo();
            escribir_nodo(raiz, {false, {{r.mbr, static_cast<std::int64_t>(cab_.raiz)}, r.hermano}});
            cab_.raiz = raiz;
            ++cab_.altura;
        }
        ++cab_.num_entradas;
    }

    bool eliminar(Punto p, long long pos) {
        if (!eliminar_en(cab_.raiz, detalle_rtree::mbr_de(p), pos)) return false;
        --cab_.num_entradas;
        return true;
    }

    // posiciones a menos de radio_m metros del centro
    std::vector<long long> rango(Punto centro, double radio_m, Metrica metrica) {
        nodos_visitados_ = 0;
        const double grados = radio_m / (motor::espacial::RADIO_TIERRA_M * std::acos(-1.0) / 180.0) * 1.05;
        const double lat_extrema = std::min(89.0, std::fabs(centro.lat_e6 / 1e6) + grados);
        const double grados_lon = std::min(360.0, grados / std::max(0.01, std::cos(lat_extrema * std::acos(-1.0) / 180.0)));
        const Mbr caja{a_micro(centro.lat_e6 - grados * 1e6), a_micro(centro.lon_e6 - grados_lon * 1e6),
                       a_micro(centro.lat_e6 + grados * 1e6), a_micro(centro.lon_e6 + grados_lon * 1e6)};
        std::vector<long long> salida;
        std::vector<PageId> pendientes{cab_.raiz};
        while (!pendientes.empty()) {
            const PageId p = pendientes.back();
            pendientes.pop_back();
            const Nodo n = leer_nodo(p);
            ++nodos_visitados_;
            for (const Entrada& e : n.entradas) {
                if (!detalle_rtree::se_cortan(caja, e.mbr)) continue;
                if (!n.hoja) {
                    pendientes.push_back(static_cast<PageId>(e.ref));
                } else if (motor::espacial::distancia_m(centro, {e.mbr.min_lat, e.mbr.min_lon}, metrica) <= radio_m) {
                    salida.push_back(e.ref);
                }
            }
        }
        return salida;
    }

    // los k más cercanos, del más cercano al más lejano (búsqueda best-first)
    std::vector<long long> knn(Punto centro, int k, Metrica metrica) {
        nodos_visitados_ = 0;
        struct Candidato {
            double distancia;
            bool es_punto;
            std::int64_t ref;
            bool operator>(const Candidato& o) const { return distancia > o.distancia; }
        };
        std::priority_queue<Candidato, std::vector<Candidato>, std::greater<Candidato>> cola;
        cola.push({0.0, false, static_cast<std::int64_t>(cab_.raiz)});
        std::vector<long long> salida;
        while (!cola.empty() && static_cast<int>(salida.size()) < k) {
            const Candidato c = cola.top();
            cola.pop();
            if (c.es_punto) {
                salida.push_back(c.ref);
                continue;
            }
            const Nodo n = leer_nodo(static_cast<PageId>(c.ref));
            ++nodos_visitados_;
            for (const Entrada& e : n.entradas) {
                cola.push({detalle_rtree::distancia_minima(centro, e.mbr, metrica), n.hoja, e.ref});
            }
        }
        return salida;
    }

    // el MBR de cada entrada interna contiene a todo su hijo y ningún nodo pasa la capacidad
    bool verificar_invariante() {
        long long hojas = 0;
        return verificar(cab_.raiz, nullptr, hojas) && hojas == static_cast<long long>(cab_.num_entradas);
    }

    long nodos_visitados() const { return nodos_visitados_; }
    long num_entradas() const { return static_cast<long>(cab_.num_entradas); }
    long num_nodos() const { return static_cast<long>(cab_.num_nodos); }
    int altura() const { return cab_.altura; }
    long tamano_en_disco() { return gestor_.tamano_en_disco(); }

    void sincronizar() {
        guardar_cabecera();
        pool_.vaciar();
    }
    void enfriar_cache() { pool_.limpiar(); }

    long paginas_leidas() const { return gestor_.lecturas; }
    long paginas_escritas() const { return gestor_.escrituras; }

private:
    struct Nodo {
        bool hoja = true;
        std::vector<Entrada> entradas;
    };

    struct Insercion {
        Mbr mbr;
        bool partio = false;
        Entrada hermano{};
    };

    static std::int32_t a_micro(double v) {
        return static_cast<std::int32_t>(std::max(-2.0e9, std::min(2.0e9, std::floor(v))));
    }

    static Mbr mbr_de_todas(const std::vector<Entrada>& entradas) {
        Mbr m = entradas.front().mbr;
        for (const Entrada& e : entradas) m = detalle_rtree::unir(m, e.mbr);
        return m;
    }

    void crear() {
        gestor_.asignar();
        cab_ = {detalle_rtree::MAGICO_RTREE, detalle_rtree::VERSION_RTREE, 1, PAGINA_NULA, 0, 0};
        cab_.raiz = nuevo_nodo();
        escribir_nodo(cab_.raiz, {true, {}});
        guardar_cabecera();
    }

    void abrir() {
        char* pagina = pool_.fijar(PAGINA_NULA);
        std::memcpy(&cab_, pagina, sizeof(detalle_rtree::CabeceraRTree));
        pool_.soltar(PAGINA_NULA, false);
        if (cab_.magico != detalle_rtree::MAGICO_RTREE || cab_.version != detalle_rtree::VERSION_RTREE ||
            cab_.raiz == PAGINA_NULA || cab_.raiz >= gestor_.num_paginas) {
            throw std::invalid_argument("archivo de indice r-tree invalido");
        }
    }

    void guardar_cabecera() {
        char* pagina = pool_.fijar(PAGINA_NULA);
        std::memset(pagina, 0, TAM_PAGINA);
        std::memcpy(pagina, &cab_, sizeof(detalle_rtree::CabeceraRTree));
        pool_.soltar(PAGINA_NULA, true);
    }

    PageId nuevo_nodo() {
        ++cab_.num_nodos;
        return gestor_.asignar();
    }

    Nodo leer_nodo(PageId p) {
        char* pagina = pool_.fijar(p);
        detalle_rtree::CabeceraNodo cab;
        std::memcpy(&cab, pagina, sizeof(cab));
        Nodo n;
        n.hoja = cab.es_hoja != 0;
        n.entradas.resize(cab.num);
        if (cab.num) std::memcpy(n.entradas.data(), pagina + sizeof(cab), sizeof(Entrada) * cab.num);
        pool_.soltar(p, false);
        return n;
    }

    void escribir_nodo(PageId p, const Nodo& n) {
        char* pagina = pool_.fijar(p);
        std::memset(pagina, 0, TAM_PAGINA);
        const detalle_rtree::CabeceraNodo cab{static_cast<std::uint16_t>(n.hoja ? 1 : 0),
                                              static_cast<std::uint16_t>(n.entradas.size()), 0};
        std::memcpy(pagina, &cab, sizeof(cab));
        if (!n.entradas.empty()) std::memcpy(pagina + sizeof(cab), n.entradas.data(), sizeof(Entrada) * n.entradas.size());
        pool_.soltar(p, true);
    }

    static std::size_t elegir_subarbol(const Nodo& n, const Mbr& m) {
        std::size_t mejor = 0;
        double mejor_agrandamiento = std::numeric_limits<double>::max();
        double mejor_area = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < n.entradas.size(); ++i) {
            const double a = detalle_rtree::area(n.entradas[i].mbr);
            const double agrandamiento = detalle_rtree::area(detalle_rtree::unir(n.entradas[i].mbr, m)) - a;
            if (agrandamiento < mejor_agrandamiento || (agrandamiento == mejor_agrandamiento && a < mejor_area)) {
                mejor = i;
                mejor_agrandamiento = agrandamiento;
                mejor_area = a;
            }
        }
        return mejor;
    }

    Insercion insertar_en(PageId p, const Entrada& e) {
        Nodo n = leer_nodo(p);
        if (n.hoja) {
            n.entradas.push_back(e);
        } else {
            const std::size_t i = elegir_subarbol(n, e.mbr);
            const Insercion hijo = insertar_en(static_cast<PageId>(n.entradas[i].ref), e);
            n.entradas[i].mbr = hijo.mbr;
            if (hijo.partio) n.entradas.push_back(hijo.hermano);
        }
        Insercion r;
        if (static_cast<int>(n.entradas.size()) > detalle_rtree::CAPACIDAD_NODO) {
            std::vector<Entrada> otra = dividir(n.entradas);
            const PageId q = nuevo_nodo();
            escribir_nodo(q, {n.hoja, otra});
            r.partio = true;
            r.hermano = {mbr_de_todas(otra), static_cast<std::int64_t>(q)};
        }
        escribir_nodo(p, n);
        r.mbr = n.entradas.empty() ? Mbr{0, 0, 0, 0} : mbr_de_todas(n.entradas);
        return r;
    }

    // split cuadrático: deja un grupo en entradas y devuelve el otro
    static std::vector<Entrada> dividir(std::vector<Entrada>& entradas) {
        std::size_t s1 = 0, s2 = 1;
        double peor = -1;
        for (std::size_t i = 0; i < entradas.size(); ++i) {
            for (std::size_t j = i + 1; j < entradas.size(); ++j) {
                const Mbr u = detalle_rtree::unir(entradas[i].mbr, entradas[j].mbr);
                const double desperdicio = detalle_rtree::area(u) - detalle_rtree::area(entradas[i].mbr) -
                                           detalle_rtree::area(entradas[j].mbr) + detalle_rtree::margen(u) * 1e-9;
                if (desperdicio > peor) {
                    peor = desperdicio;
                    s1 = i;
                    s2 = j;
                }
            }
        }
        std::vector<Entrada> g1{entradas[s1]}, g2{entradas[s2]};
        Mbr m1 = entradas[s1].mbr, m2 = entradas[s2].mbr;
        std::vector<Entrada> resto;
        for (std::size_t i = 0; i < entradas.size(); ++i) if (i != s1 && i != s2) resto.push_back(entradas[i]);

        while (!resto.empty()) {
            const int faltan = static_cast<int>(resto.size());
            if (static_cast<int>(g1.size()) + faltan == detalle_rtree::MINIMO_NODO) {
                for (const Entrada& e : resto) { g1.push_back(e); m1 = detalle_rtree::unir(m1, e.mbr); }
                break;
            }
            if (static_cast<int>(g2.size()) + faltan == detalle_rtree::MINIMO_NODO) {
                for (const Entrada& e : resto) { g2.push_back(e); m2 = detalle_rtree::unir(m2, e.mbr); }
                break;
            }
            std::size_t elegido = 0;
            double mayor_diferencia = -1, d1_elegido = 0, d2_elegido = 0;
            for (std::size_t i = 0; i < resto.size(); ++i) {
                const double d1 = detalle_rtree::area(detalle_rtree::unir(m1, resto[i].mbr)) - detalle_rtree::area(m1) +
                                  detalle_rtree::margen(detalle_rtree::unir(m1, resto[i].mbr)) * 1e-9;
                const double d2 = detalle_rtree::area(detalle_rtree::unir(m2, resto[i].mbr)) - detalle_rtree::area(m2) +
                                  detalle_rtree::margen(detalle_rtree::unir(m2, resto[i].mbr)) * 1e-9;
                if (std::fabs(d1 - d2) > mayor_diferencia) {
                    mayor_diferencia = std::fabs(d1 - d2);
                    elegido = i;
                    d1_elegido = d1;
                    d2_elegido = d2;
                }
            }
            const Entrada e = resto[elegido];
            resto.erase(resto.begin() + static_cast<long>(elegido));
            const bool al_primero = d1_elegido < d2_elegido ||
                                    (d1_elegido == d2_elegido && g1.size() <= g2.size());
            if (al_primero) { g1.push_back(e); m1 = detalle_rtree::unir(m1, e.mbr); }
            else { g2.push_back(e); m2 = detalle_rtree::unir(m2, e.mbr); }
        }
        entradas = g1;
        return g2;
    }

    bool eliminar_en(PageId p, const Mbr& m, long long pos) {
        Nodo n = leer_nodo(p);
        for (std::size_t i = 0; i < n.entradas.size(); ++i) {
            const Entrada& e = n.entradas[i];
            if (!detalle_rtree::contiene(e.mbr, m)) continue;
            if (n.hoja) {
                if (e.ref != pos) continue;
                n.entradas.erase(n.entradas.begin() + static_cast<long>(i));
                escribir_nodo(p, n);
                return true;
            }
            if (eliminar_en(static_cast<PageId>(e.ref), m, pos)) return true;
        }
        return false;
    }

    bool verificar(PageId p, const Mbr* padre, long long& hojas) {
        const Nodo n = leer_nodo(p);
        if (static_cast<int>(n.entradas.size()) > detalle_rtree::CAPACIDAD_NODO) return false;
        for (const Entrada& e : n.entradas) {
            if (padre && !detalle_rtree::contiene(*padre, e.mbr)) return false;
            if (n.hoja) ++hojas;
            else if (!verificar(static_cast<PageId>(e.ref), &e.mbr, hojas)) return false;
        }
        return true;
    }

    GestorPaginas gestor_;
    BufferPool pool_;
    detalle_rtree::CabeceraRTree cab_{};
    long nodos_visitados_ = 0;
};
