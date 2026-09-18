#pragma once

#include "buffer_pool.h"
#include "gestor_paginas.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace detalle_hash_disco {

using detalle_bplus_no_agrupado::BufferPool;
using detalle_bplus_no_agrupado::GestorPaginas;
using detalle_bplus_no_agrupado::PAGINA_NULA;
using detalle_bplus_no_agrupado::PageId;
using detalle_bplus_no_agrupado::TAM_PAGINA;

const std::uint32_t MAGICO_HASH = 0x48455844u;
const std::uint16_t VERSION_HASH = 1;
const int PROFUNDIDAD_MAXIMA = 24;

struct EntradaHash {
    std::int64_t pos;
    std::int32_t clave;
    std::int32_t relleno;
};

struct CabeceraBucket {
    std::uint16_t profundidad_local;
    std::uint16_t num;
    PageId desborde;
};

struct CabeceraHash {
    std::uint32_t magico;
    std::uint16_t version;
    std::uint16_t profundidad_global;
    std::uint64_t num_entradas;
    PageId directorio;
    PageId libres;
    std::uint32_t paginas_bucket;
    std::uint32_t reservado;
};

const int CAPACIDAD_BUCKET =
    (TAM_PAGINA - static_cast<int>(sizeof(CabeceraBucket))) / static_cast<int>(sizeof(EntradaHash));
const int PUNTEROS_POR_PAGINA = TAM_PAGINA / static_cast<int>(sizeof(PageId));

static_assert(sizeof(EntradaHash) == 16, "entrada de hash inesperada");
static_assert(sizeof(CabeceraBucket) == 8, "cabecera de bucket inesperada");
static_assert(std::is_trivially_copyable<EntradaHash>::value, "EntradaHash debe copiarse a la pagina");
static_assert(CAPACIDAD_BUCKET == 255, "capacidad de bucket inesperada");

inline std::uint64_t mezclar(std::int32_t clave) {
    std::uint64_t x = static_cast<std::uint64_t>(static_cast<std::uint32_t>(clave)) + 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

struct VistaBucket {
    CabeceraBucket* cab;
    EntradaHash* entradas;
    explicit VistaBucket(char* pagina)
        : cab(reinterpret_cast<CabeceraBucket*>(pagina)),
          entradas(reinterpret_cast<EntradaHash*>(pagina + sizeof(CabeceraBucket))) {}
};

}

class HashExtensibleDisco {
    using PageId = detalle_bplus_no_agrupado::PageId;
    using GestorPaginas = detalle_bplus_no_agrupado::GestorPaginas;
    using BufferPool = detalle_bplus_no_agrupado::BufferPool;
    using VistaBucket = detalle_hash_disco::VistaBucket;
    using EntradaHash = detalle_hash_disco::EntradaHash;
    using CabeceraHash = detalle_hash_disco::CabeceraHash;
    static constexpr PageId PAGINA_NULA = detalle_bplus_no_agrupado::PAGINA_NULA;
    static constexpr int TAM_PAGINA = detalle_bplus_no_agrupado::TAM_PAGINA;

public:
    explicit HashExtensibleDisco(const std::string& ruta, bool truncar = false)
        : gestor_(ruta, truncar), pool_(gestor_) {
        if (gestor_.num_paginas == 0) crear();
        else abrir();
    }

    ~HashExtensibleDisco() {
        try {
            guardar_cabecera();
            pool_.vaciar();
        } catch (...) {
        }
    }

    HashExtensibleDisco(const HashExtensibleDisco&) = delete;
    HashExtensibleDisco& operator=(const HashExtensibleDisco&) = delete;

    void insertar(int clave, long long pos, bool verificar_duplicado = true) {
        const PageId cabeza = dir_leer(indice_dir(clave));
        if (verificar_duplicado && existe_par(cabeza, clave, pos)) return;
        if (!llena(cabeza)) {
            colocar_en(cabeza, clave, pos);
            ++cab_.num_entradas;
            return;
        }
        if (conviene_dividir(cabeza)) {
            dividir(indice_dir(clave));
            colocar_forzado(dir_leer(indice_dir(clave)), clave, pos);
        } else {
            colocar_forzado(cabeza, clave, pos);
        }
        ++cab_.num_entradas;
    }

    std::vector<long long> buscar(int clave) {
        std::vector<long long> salida;
        PageId p = dir_leer(indice_dir(clave));
        while (p != PAGINA_NULA) {
            char* pg = pool_.fijar(p);
            detalle_hash_disco::VistaBucket v(pg);
            for (int i = 0; i < static_cast<int>(v.cab->num); ++i) {
                if (v.entradas[i].clave == clave) salida.push_back(static_cast<long long>(v.entradas[i].pos));
            }
            const PageId sig = v.cab->desborde;
            pool_.soltar(p, false);
            p = sig;
        }
        return salida;
    }

    int eliminar(int clave) {
        int quitadas = 0;
        PageId p = dir_leer(indice_dir(clave));
        while (p != PAGINA_NULA) {
            char* pg = pool_.fijar(p);
            detalle_hash_disco::VistaBucket v(pg);
            int escritas = 0;
            for (int i = 0; i < static_cast<int>(v.cab->num); ++i) {
                if (v.entradas[i].clave == clave) ++quitadas;
                else v.entradas[escritas++] = v.entradas[i];
            }
            v.cab->num = static_cast<std::uint16_t>(escritas);
            const PageId sig = v.cab->desborde;
            pool_.soltar(p, quitadas > 0);
            p = sig;
        }
        cab_.num_entradas -= static_cast<std::uint64_t>(quitadas);
        return quitadas;
    }

    bool eliminar_entrada(int clave, long long pos) {
        PageId p = dir_leer(indice_dir(clave));
        while (p != PAGINA_NULA) {
            char* pg = pool_.fijar(p);
            detalle_hash_disco::VistaBucket v(pg);
            for (int i = 0; i < static_cast<int>(v.cab->num); ++i) {
                if (v.entradas[i].clave != clave || v.entradas[i].pos != pos) continue;
                for (int k = i; k + 1 < static_cast<int>(v.cab->num); ++k) v.entradas[k] = v.entradas[k + 1];
                --v.cab->num;
                pool_.soltar(p, true);
                --cab_.num_entradas;
                return true;
            }
            const PageId sig = v.cab->desborde;
            pool_.soltar(p, false);
            p = sig;
        }
        return false;
    }

    int profundidad_global() const { return cab_.profundidad_global; }
    long num_entradas() const { return static_cast<long>(cab_.num_entradas); }
    long paginas_bucket() const { return static_cast<long>(cab_.paginas_bucket); }
    long paginas_directorio() const { return static_cast<long>(paginas_para(cab_.profundidad_global)); }
    long tamano_en_disco() { return gestor_.tamano_en_disco(); }

    void sincronizar() {
        guardar_cabecera();
        pool_.vaciar();
    }
    void enfriar_cache() { pool_.limpiar(); }

    long paginas_leidas() const { return gestor_.lecturas; }
    long paginas_escritas() const { return gestor_.escrituras; }
    long aciertos_cache() const { return pool_.aciertos; }
    long fallos_cache() const { return pool_.fallos; }
    long desalojos_cache() const { return pool_.desalojos; }
    int paginas_en_cache() const { return pool_.paginas_residentes(); }

private:

    void crear() {
        gestor_.asignar();
        const PageId dir = gestor_.asignar();
        cab_.magico = detalle_hash_disco::MAGICO_HASH;
        cab_.version = detalle_hash_disco::VERSION_HASH;
        cab_.profundidad_global = 0;
        cab_.num_entradas = 0;
        cab_.directorio = dir;
        cab_.libres = PAGINA_NULA;
        cab_.paginas_bucket = 0;
        cab_.reservado = 0;
        dir_escribir(0, nuevo_bucket(0));
        guardar_cabecera();
    }

    void abrir() {
        cargar_cabecera();
        if (cab_.magico != detalle_hash_disco::MAGICO_HASH || cab_.version != detalle_hash_disco::VERSION_HASH ||
            cab_.directorio == PAGINA_NULA || cab_.directorio >= gestor_.num_paginas ||
            cab_.profundidad_global > detalle_hash_disco::PROFUNDIDAD_MAXIMA) {
            throw std::invalid_argument("archivo de indice hash invalido");
        }
    }

    void guardar_cabecera() {
        char* pagina = pool_.fijar(PAGINA_NULA);
        std::memset(pagina, 0, TAM_PAGINA);
        std::memcpy(pagina, &cab_, sizeof(CabeceraHash));
        pool_.soltar(PAGINA_NULA, true);
    }

    void cargar_cabecera() {
        char* pagina = pool_.fijar(PAGINA_NULA);
        std::memcpy(&cab_, pagina, sizeof(CabeceraHash));
        pool_.soltar(PAGINA_NULA, false);
    }

    static std::uint32_t paginas_para(int profundidad) {
        const std::uint64_t n = 1ull << profundidad;
        return static_cast<std::uint32_t>((n + detalle_hash_disco::PUNTEROS_POR_PAGINA - 1) /
                                          detalle_hash_disco::PUNTEROS_POR_PAGINA);
    }

    std::uint32_t indice_dir(int clave) const {
        if (cab_.profundidad_global == 0) return 0;
        return static_cast<std::uint32_t>(detalle_hash_disco::mezclar(clave) &
                                          ((1ull << cab_.profundidad_global) - 1));
    }

    PageId dir_leer(std::uint64_t j) {
        const PageId pag = cab_.directorio + static_cast<PageId>(j / detalle_hash_disco::PUNTEROS_POR_PAGINA);
        char* p = pool_.fijar(pag);
        PageId valor;
        std::memcpy(&valor, p + (j % detalle_hash_disco::PUNTEROS_POR_PAGINA) * sizeof(PageId), sizeof(PageId));
        pool_.soltar(pag, false);
        return valor;
    }

    void dir_escribir(std::uint64_t j, PageId valor) {
        const PageId pag = cab_.directorio + static_cast<PageId>(j / detalle_hash_disco::PUNTEROS_POR_PAGINA);
        char* p = pool_.fijar(pag);
        std::memcpy(p + (j % detalle_hash_disco::PUNTEROS_POR_PAGINA) * sizeof(PageId), &valor, sizeof(PageId));
        pool_.soltar(pag, true);
    }

    void duplicar_directorio() {
        if (cab_.profundidad_global >= detalle_hash_disco::PROFUNDIDAD_MAXIMA) {
            throw std::runtime_error("el directorio del indice hash llego a su profundidad maxima");
        }
        const std::uint64_t total = 1ull << cab_.profundidad_global;
        std::vector<PageId> copia(total);
        for (std::uint64_t j = 0; j < total; ++j) copia[j] = dir_leer(j);

        const std::uint32_t viejas = paginas_para(cab_.profundidad_global);
        const PageId anterior = cab_.directorio;
        const std::uint32_t nuevas = paginas_para(cab_.profundidad_global + 1);
        PageId base = PAGINA_NULA;
        for (std::uint32_t k = 0; k < nuevas; ++k) {
            const PageId p = gestor_.asignar();
            if (k == 0) base = p;
        }

        cab_.directorio = base;
        ++cab_.profundidad_global;
        for (std::uint64_t j = 0; j < total * 2; ++j) dir_escribir(j, copia[j & (total - 1)]);
        for (std::uint32_t k = 0; k < viejas; ++k) liberar_pagina(anterior + k);
    }

    PageId tomar_pagina() {
        if (cab_.libres == PAGINA_NULA) return gestor_.asignar();
        const PageId p = cab_.libres;
        char* pg = pool_.fijar(p);
        cab_.libres = VistaBucket(pg).cab->desborde;
        pool_.soltar(p, false);
        return p;
    }

    void liberar_pagina(PageId p) {
        char* pg = pool_.fijar(p);
        std::memset(pg, 0, TAM_PAGINA);
        VistaBucket v(pg);
        v.cab->desborde = cab_.libres;
        pool_.soltar(p, true);
        cab_.libres = p;
    }

    PageId nuevo_bucket(int profundidad_local) {
        const PageId p = tomar_pagina();
        char* pg = pool_.fijar(p);
        std::memset(pg, 0, TAM_PAGINA);
        VistaBucket v(pg);
        v.cab->profundidad_local = static_cast<std::uint16_t>(profundidad_local);
        v.cab->num = 0;
        v.cab->desborde = PAGINA_NULA;
        pool_.soltar(p, true);
        ++cab_.paginas_bucket;
        return p;
    }

    int profundidad_local(PageId p) {
        char* pg = pool_.fijar(p);
        const int pl = VistaBucket(pg).cab->profundidad_local;
        pool_.soltar(p, false);
        return pl;
    }

    bool existe_par(PageId cabeza, int clave, long long pos) {
        PageId p = cabeza;
        while (p != PAGINA_NULA) {
            char* pg = pool_.fijar(p);
            VistaBucket v(pg);
            for (int i = 0; i < static_cast<int>(v.cab->num); ++i) {
                if (v.entradas[i].clave == clave && v.entradas[i].pos == pos) {
                    pool_.soltar(p, false);
                    return true;
                }
            }
            const PageId sig = v.cab->desborde;
            pool_.soltar(p, false);
            p = sig;
        }
        return false;
    }

    bool llena(PageId p) {
        char* pg = pool_.fijar(p);
        const bool sin_sitio = static_cast<int>(VistaBucket(pg).cab->num) >= detalle_hash_disco::CAPACIDAD_BUCKET;
        pool_.soltar(p, false);
        return sin_sitio;
    }

    void colocar_en(PageId p, int clave, long long pos) {
        char* pg = pool_.fijar(p);
        VistaBucket v(pg);
        EntradaHash& e = v.entradas[v.cab->num++];
        e.clave = clave;
        e.relleno = 0;
        e.pos = pos;
        pool_.soltar(p, true);
    }

    void desalojar_cabeza(PageId cabeza) {
        const PageId nueva = nuevo_bucket(profundidad_local(cabeza));
        char* pc = pool_.fijar(cabeza);
        VistaBucket vc(pc);
        char* pn = pool_.fijar(nueva);
        VistaBucket vn(pn);
        std::memcpy(vn.entradas, vc.entradas, sizeof(EntradaHash) * vc.cab->num);
        vn.cab->num = vc.cab->num;
        vn.cab->desborde = vc.cab->desborde;
        vc.cab->num = 0;
        vc.cab->desborde = nueva;
        pool_.soltar(nueva, true);
        pool_.soltar(cabeza, true);
    }

    void colocar_forzado(PageId cabeza, int clave, long long pos) {
        if (llena(cabeza)) desalojar_cabeza(cabeza);
        colocar_en(cabeza, clave, pos);
    }

    bool conviene_dividir(PageId cabeza) {
        const int pl = profundidad_local(cabeza);
        if (pl >= detalle_hash_disco::PROFUNDIDAD_MAXIMA) return false;
        const std::uint64_t bit = 1ull << pl;
        bool primero = true;
        bool referencia = false;
        PageId p = cabeza;
        while (p != PAGINA_NULA) {
            char* pg = pool_.fijar(p);
            VistaBucket v(pg);
            for (int i = 0; i < static_cast<int>(v.cab->num); ++i) {
                const bool lado = (detalle_hash_disco::mezclar(v.entradas[i].clave) & bit) != 0;
                if (primero) {
                    referencia = lado;
                    primero = false;
                } else if (lado != referencia) {
                    pool_.soltar(p, false);
                    return true;
                }
            }
            const PageId sig = v.cab->desborde;
            pool_.soltar(p, false);
            p = sig;
        }
        return false;
    }

    std::vector<EntradaHash> vaciar_cadena(PageId cabeza) {
        std::vector<EntradaHash> entradas;
        PageId p = cabeza;
        std::vector<PageId> a_liberar;
        while (p != PAGINA_NULA) {
            char* pg = pool_.fijar(p);
            VistaBucket v(pg);
            for (int i = 0; i < static_cast<int>(v.cab->num); ++i) entradas.push_back(v.entradas[i]);
            const PageId sig = v.cab->desborde;
            v.cab->num = 0;
            if (p == cabeza) v.cab->desborde = PAGINA_NULA;
            pool_.soltar(p, true);
            if (p != cabeza) a_liberar.push_back(p);
            p = sig;
        }
        for (PageId q : a_liberar) {
            liberar_pagina(q);
            --cab_.paginas_bucket;
        }
        return entradas;
    }

    void fijar_profundidad_local(PageId p, int pl) {
        char* pg = pool_.fijar(p);
        VistaBucket(pg).cab->profundidad_local = static_cast<std::uint16_t>(pl);
        pool_.soltar(p, true);
    }

    void dividir(std::uint64_t j) {
        const PageId b = dir_leer(j);
        const int pl = profundidad_local(b);
        if (pl == cab_.profundidad_global) duplicar_directorio();

        std::vector<EntradaHash> entradas = vaciar_cadena(b);
        const std::uint64_t bit = 1ull << pl;
        const PageId b2 = nuevo_bucket(pl + 1);
        fijar_profundidad_local(b, pl + 1);

        const std::uint64_t total = 1ull << cab_.profundidad_global;
        for (std::uint64_t i = 0; i < total; ++i) {
            if ((i & bit) && dir_leer(i) == b) dir_escribir(i, b2);
        }
        for (const EntradaHash& e : entradas) {
            colocar_forzado((detalle_hash_disco::mezclar(e.clave) & bit) ? b2 : b, e.clave, e.pos);
        }
    }

    GestorPaginas gestor_;
    BufferPool pool_;
    CabeceraHash cab_{};
};
