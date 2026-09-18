// Comparación experimental de índices (sección 2.1.6): B+ agrupado vs B+ no
// agrupado (sobre heap) vs hash extensible (hoy en RAM). Misma clave (Index),
// mismos registros de 180 B, mismas consultas.
//
//   indices_bench [--n 10000] [--barajar] [--csv ruta] [--salida datos/resultados/indices_bench.csv]
//                 [--consultas 1000] [--rangos 100]
//
// Escribe una fila por estructura. Todas las medidas de índice arrancan con la
// caché fría (enfriar_cache) para que cuenten las lecturas reales de disco.

#include "../archivos/heap_file.h"
#include "../consultas/external_algorithms.h"
#include "../indices/bplus_agrupado.h"
#include "../indices/bplus_no_agrupado.h"
#include "../indices/extendible_hash.h"
#include "cargador_csv.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using Reloj = std::chrono::steady_clock;

struct Organizacion {
    int clave;
    int fundada;
    int empleados;
    char org_id[16];
    char nombre[40];
    char pais[56];
    char industria[56];
};
static_assert(sizeof(Organizacion) == 180, "Organizacion debe medir 180 bytes");

struct Opciones {
    std::size_t n = 10000;
    bool barajar = false;
    std::string csv;
    std::string salida = "datos/resultados/indices_bench.csv";
    std::size_t consultas = 1000;
    std::size_t rangos = 100;
    unsigned semilla = 42;
};

struct Medida {
    std::string estructura;
    double construccion_ms = 0;
    double construccion_masiva_ms = 0;  // solo agrupado
    double datos_ms = 0;                // carga del heap (no agrupado y hash)
    long bytes_total = 0;
    long bytes_adicional = 0;
    int altura = 0;
    double igualdad_us = 0;
    double igualdad_pag_indice = 0;
    double igualdad_pag_datos = 0;
    bool soporta_rango = true;
    double rango100_us = 0;
    double rango100_pag = 0;
    double rango1000_us = 0;
    double rango1000_pag = 0;
    double ordenamiento_ms = 0;
    double ordenamiento_pag = 0;
    double insercion_us = 0;
    double insercion_pag_escritas = 0;
    double eliminacion_us = 0;
    double eliminacion_pag_escritas = 0;
};

Opciones leer_opciones(int argc, char** argv) {
    Opciones o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto valor = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("falta valor para " + arg);
            return argv[++i];
        };
        if (arg == "--n") o.n = std::stoul(valor());
        else if (arg == "--barajar") o.barajar = true;
        else if (arg == "--csv") o.csv = valor();
        else if (arg == "--salida") o.salida = valor();
        else if (arg == "--consultas") o.consultas = std::stoul(valor());
        else if (arg == "--rangos") o.rangos = std::stoul(valor());
        else if (arg == "--semilla") o.semilla = static_cast<unsigned>(std::stoul(valor()));
        else throw std::runtime_error("opcion desconocida: " + arg);
    }
    if (o.csv.empty()) o.csv = "datos/organizations-" + std::to_string(o.n) + ".csv";
    return o;
}

void copiar(char* destino, std::size_t capacidad, const std::string& texto) {
    std::memset(destino, 0, capacidad);
    std::memcpy(destino, texto.data(), std::min(capacidad, texto.size()));
}

int entero(const std::string& texto) {
    try { return std::stoi(texto); } catch (...) { return 0; }
}

Organizacion convertir(const motor::Registro& registro) {
    const std::vector<std::string> campos = motor::pruebas::dividir_campos_csv(registro.valor);
    if (campos.size() != 8) throw std::runtime_error("fila con " + std::to_string(campos.size()) + " campos");
    Organizacion o{};
    o.clave = registro.clave;
    copiar(o.org_id, sizeof(o.org_id), campos[0]);
    copiar(o.nombre, sizeof(o.nombre), campos[1]);
    copiar(o.pais, sizeof(o.pais), campos[3]);
    o.fundada = entero(campos[5]);
    copiar(o.industria, sizeof(o.industria), campos[6]);
    o.empleados = entero(campos[7]);
    return o;
}

Organizacion nueva(int clave) {
    Organizacion o{};
    o.clave = clave;
    o.fundada = 2024;
    o.empleados = clave % 1000;
    std::snprintf(o.nombre, sizeof(o.nombre), "Nueva %d", clave);
    return o;
}

double ms(Reloj::time_point inicio) { return std::chrono::duration<double, std::milli>(Reloj::now() - inicio).count(); }
double us(Reloj::time_point inicio) { return std::chrono::duration<double, std::micro>(Reloj::now() - inicio).count(); }

long long pos_de(const motor::RecordId& rid) { return static_cast<long long>(rid.page_id) * 65536 + rid.slot_id; }
motor::RecordId rid_de(long long pos) {
    motor::RecordId rid;
    rid.page_id = static_cast<std::uint32_t>(pos / 65536);
    rid.slot_id = static_cast<std::uint16_t>(pos % 65536);
    return rid;
}

// claves para igualdad, inicios de rango, claves nuevas y claves a borrar: iguales para las tres
struct CargaDeTrabajo {
    std::vector<int> igualdad;
    std::vector<int> inicios100;
    std::vector<int> inicios1000;
    std::vector<int> nuevas;
    std::vector<int> borrar;
};

CargaDeTrabajo generar(const Opciones& o, int n) {
    CargaDeTrabajo c;
    std::mt19937 g(o.semilla);
    std::uniform_int_distribution<int> clave(1, n);
    for (std::size_t i = 0; i < std::min<std::size_t>(o.consultas, static_cast<std::size_t>(n)); ++i) c.igualdad.push_back(clave(g));
    std::uniform_int_distribution<int> inicio100(1, std::max(1, n - 100));
    std::uniform_int_distribution<int> inicio1000(1, std::max(1, n - 1000));
    for (std::size_t i = 0; i < o.rangos; ++i) { c.inicios100.push_back(inicio100(g)); c.inicios1000.push_back(inicio1000(g)); }
    const int extra = std::max(1, n / 10);
    for (int k = 1; k <= extra; ++k) c.nuevas.push_back(n + k);
    std::vector<int> todas(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) todas[static_cast<std::size_t>(k)] = k + 1;
    std::shuffle(todas.begin(), todas.end(), g);
    todas.resize(static_cast<std::size_t>(extra));
    c.borrar = todas;
    return c;
}

// --- B+ agrupado ---

Medida medir_agrupado(const std::vector<Organizacion>& filas, const CargaDeTrabajo& carga) {
    Medida m;
    m.estructura = "bplus_agrupado";
    const std::string ruta = ".build/idx_agrupado.dat";
    {
        motor::BPlusAgrupado masivo(".build/idx_agrupado_masivo.dat", sizeof(Organizacion), true);
        std::vector<Organizacion> copia = filas;
        const auto t = Reloj::now();
        masivo.cargar_masivo(copia);
        masivo.sincronizar();
        m.construccion_masiva_ms = ms(t);
    }
    motor::BPlusAgrupado arbol(ruta, sizeof(Organizacion), true);
    {
        const auto t = Reloj::now();
        for (const Organizacion& f : filas) arbol.insertar(f);
        arbol.sincronizar();
        m.construccion_ms = ms(t);
    }
    m.bytes_total = arbol.tamano_en_disco();
    m.bytes_adicional = m.bytes_total - static_cast<long>(filas.size() * sizeof(Organizacion));
    m.altura = arbol.altura();

    // igualdad
    {
        long leidas = 0;
        double total = 0;
        for (int clave : carga.igualdad) {
            arbol.enfriar_cache();
            const long antes = arbol.paginas_leidas();
            const auto t = Reloj::now();
            Organizacion salida{};
            if (!arbol.buscar(clave, salida)) throw std::runtime_error("agrupado: no encontro " + std::to_string(clave));
            total += us(t);
            leidas += arbol.paginas_leidas() - antes;
        }
        m.igualdad_us = total / carga.igualdad.size();
        m.igualdad_pag_indice = static_cast<double>(leidas) / carga.igualdad.size();
    }
    // rangos
    auto rango = [&](const std::vector<int>& inicios, int ancho, double& t_us, double& pag) {
        long leidas = 0;
        double total = 0;
        for (int desde : inicios) {
            arbol.enfriar_cache();
            const long antes = arbol.paginas_leidas();
            const auto t = Reloj::now();
            std::size_t vistos = 0;
            arbol.buscar_rango_bytes(desde, desde + ancho - 1, [&](const void*) { ++vistos; return true; });
            total += us(t);
            leidas += arbol.paginas_leidas() - antes;
            if (vistos != static_cast<std::size_t>(ancho)) throw std::runtime_error("agrupado: rango incompleto");
        }
        t_us = total / inicios.size();
        pag = static_cast<double>(leidas) / inicios.size();
    };
    rango(carga.inicios100, 100, m.rango100_us, m.rango100_pag);
    rango(carga.inicios1000, 1000, m.rango1000_us, m.rango1000_pag);
    // ordenamiento: recorrer las hojas encadenadas
    {
        arbol.enfriar_cache();
        const long antes = arbol.paginas_leidas();
        const auto t = Reloj::now();
        std::size_t vistos = 0;
        int anterior = INT_MIN;
        arbol.buscar_rango_bytes(INT_MIN, INT_MAX, [&](const void* r) {
            const int c = motor::BPlusAgrupado::clave_de(r);
            if (c <= anterior) throw std::runtime_error("agrupado: desorden");
            anterior = c;
            ++vistos;
            return true;
        });
        m.ordenamiento_ms = ms(t);
        m.ordenamiento_pag = static_cast<double>(arbol.paginas_leidas() - antes);
        if (vistos != filas.size()) throw std::runtime_error("agrupado: ordenamiento incompleto");
    }
    // inserciones y eliminaciones frecuentes
    {
        const long antes = arbol.paginas_escritas();
        const auto t = Reloj::now();
        for (int clave : carga.nuevas) arbol.insertar(nueva(clave));
        arbol.sincronizar();
        m.insercion_us = us(t) / carga.nuevas.size();
        m.insercion_pag_escritas = static_cast<double>(arbol.paginas_escritas() - antes) / carga.nuevas.size();
    }
    {
        const long antes = arbol.paginas_escritas();
        const auto t = Reloj::now();
        for (int clave : carga.borrar) if (!arbol.eliminar(clave)) throw std::runtime_error("agrupado: no borro");
        arbol.sincronizar();
        m.eliminacion_us = us(t) / carga.borrar.size();
        m.eliminacion_pag_escritas = static_cast<double>(arbol.paginas_escritas() - antes) / carga.borrar.size();
    }
    if (!arbol.verificar_invariantes()) throw std::runtime_error("agrupado: invariantes rotos");
    return m;
}

// --- heap compartido por el no agrupado y el hash ---

struct Heap {
    motor::HeapFile archivo;
    std::vector<long long> pos;  // pos[i] del registro i (solo para construir los indices)
    double carga_ms = 0;

    Heap(const std::string& ruta, const std::vector<Organizacion>& filas) : archivo(ruta, true) {
        const auto t = Reloj::now();
        pos.reserve(filas.size());
        for (const Organizacion& f : filas) {
            pos.push_back(pos_de(archivo.insertar_bytes(reinterpret_cast<const std::byte*>(&f), sizeof(f))));
        }
        carga_ms = ms(t);
    }
    Organizacion leer(long long p) {
        auto bytes = archivo.obtener(rid_de(p));
        if (!bytes || bytes->size() != sizeof(Organizacion)) throw std::runtime_error("heap: registro invalido");
        Organizacion o;
        std::memcpy(&o, bytes->data(), sizeof(o));
        return o;
    }
};

// --- B+ no agrupado ---

Medida medir_no_agrupado(const std::vector<Organizacion>& filas, const CargaDeTrabajo& carga) {
    Medida m;
    m.estructura = "bplus_no_agrupado";
    Heap heap(".build/idx_heap_bplus.heap", filas);
    m.datos_ms = heap.carga_ms;

    BPlusNoAgrupado indice(".build/idx_no_agrupado.bplus", true);
    {
        const auto t = Reloj::now();
        for (std::size_t i = 0; i < filas.size(); ++i) indice.insertar(filas[i].clave, heap.pos[i]);
        indice.sincronizar();
        m.construccion_ms = ms(t);
    }
    m.bytes_adicional = indice.tamano_en_disco();
    m.bytes_total = m.bytes_adicional + static_cast<long>(heap.archivo.tamano_en_disco());
    m.altura = indice.altura();

    auto frio = [&]() { indice.enfriar_cache(); heap.archivo.reiniciar_contadores(); };

    // igualdad: indice + salto al heap
    {
        long pag_idx = 0, pag_heap = 0;
        double total = 0;
        for (int clave : carga.igualdad) {
            frio();
            const long antes = indice.paginas_leidas();
            const auto t = Reloj::now();
            const std::vector<long long> p = indice.buscar(clave);
            if (p.size() != 1) throw std::runtime_error("no agrupado: clave " + std::to_string(clave));
            const Organizacion o = heap.leer(p[0]);
            total += us(t);
            if (o.clave != clave) throw std::runtime_error("no agrupado: registro equivocado");
            pag_idx += indice.paginas_leidas() - antes;
            pag_heap += static_cast<long>(heap.archivo.paginas_leidas());
        }
        m.igualdad_us = total / carga.igualdad.size();
        m.igualdad_pag_indice = static_cast<double>(pag_idx) / carga.igualdad.size();
        m.igualdad_pag_datos = static_cast<double>(pag_heap) / carga.igualdad.size();
    }
    auto rango = [&](const std::vector<int>& inicios, int ancho, double& t_us, double& pag) {
        long leidas = 0;
        double total = 0;
        for (int desde : inicios) {
            frio();
            const long antes = indice.paginas_leidas();
            const auto t = Reloj::now();
            const std::vector<long long> ps = indice.buscar_rango(desde, desde + ancho - 1);
            for (long long p : ps) heap.leer(p);
            total += us(t);
            if (ps.size() != static_cast<std::size_t>(ancho)) throw std::runtime_error("no agrupado: rango incompleto");
            leidas += (indice.paginas_leidas() - antes) + static_cast<long>(heap.archivo.paginas_leidas());
        }
        t_us = total / inicios.size();
        pag = static_cast<double>(leidas) / inicios.size();
    };
    rango(carga.inicios100, 100, m.rango100_us, m.rango100_pag);
    rango(carga.inicios1000, 1000, m.rango1000_us, m.rango1000_pag);
    // ordenamiento: hojas del indice en orden + un salto al heap por registro
    {
        frio();
        const long antes = indice.paginas_leidas();
        const auto t = Reloj::now();
        const std::vector<long long> ps = indice.buscar_rango(INT_MIN, INT_MAX);
        int anterior = INT_MIN;
        for (long long p : ps) {
            const Organizacion o = heap.leer(p);
            if (o.clave <= anterior) throw std::runtime_error("no agrupado: desorden");
            anterior = o.clave;
        }
        m.ordenamiento_ms = ms(t);
        m.ordenamiento_pag = static_cast<double>((indice.paginas_leidas() - antes) + static_cast<long>(heap.archivo.paginas_leidas()));
        if (ps.size() != filas.size()) throw std::runtime_error("no agrupado: ordenamiento incompleto");
    }
    // inserciones: heap + indice; eliminaciones: buscar en el indice, borrar en ambos
    {
        heap.archivo.reiniciar_contadores();
        const long antes = indice.paginas_escritas();
        const auto t = Reloj::now();
        for (int clave : carga.nuevas) {
            const Organizacion o = nueva(clave);
            const long long p = pos_de(heap.archivo.insertar_bytes(reinterpret_cast<const std::byte*>(&o), sizeof(o)));
            indice.insertar(clave, p);
        }
        indice.sincronizar();
        m.insercion_us = us(t) / carga.nuevas.size();
        m.insercion_pag_escritas = static_cast<double>((indice.paginas_escritas() - antes) + static_cast<long>(heap.archivo.paginas_escritas())) / carga.nuevas.size();
    }
    {
        heap.archivo.reiniciar_contadores();
        const long antes = indice.paginas_escritas();
        const auto t = Reloj::now();
        for (int clave : carga.borrar) {
            const std::vector<long long> ps = indice.buscar(clave);
            if (ps.size() != 1 || !indice.eliminar_entrada(clave, ps[0]) || !heap.archivo.eliminar_rid(rid_de(ps[0]))) {
                throw std::runtime_error("no agrupado: no borro " + std::to_string(clave));
            }
        }
        indice.sincronizar();
        m.eliminacion_us = us(t) / carga.borrar.size();
        m.eliminacion_pag_escritas = static_cast<double>((indice.paginas_escritas() - antes) + static_cast<long>(heap.archivo.paginas_escritas())) / carga.borrar.size();
    }
    return m;
}

// --- hash extensible (en RAM) ---

Medida medir_hash(const std::vector<Organizacion>& filas, const CargaDeTrabajo& carga) {
    Medida m;
    m.estructura = "hash_ram";
    m.soporta_rango = false;
    Heap heap(".build/idx_heap_hash.heap", filas);
    m.datos_ms = heap.carga_ms;

    motor::ExtendibleHashing<int, long long> indice(64);
    {
        const auto t = Reloj::now();
        for (std::size_t i = 0; i < filas.size(); ++i) indice.insertar(filas[i].clave, heap.pos[i]);
        m.construccion_ms = ms(t);
    }
    m.bytes_adicional = static_cast<long>(indice.metricas().espacio_adicional_bytes);
    m.bytes_total = m.bytes_adicional + static_cast<long>(heap.archivo.tamano_en_disco());
    m.altura = static_cast<int>(indice.profundidad_global());

    // igualdad: hash en RAM + salto al heap
    {
        long pag_heap = 0;
        double total = 0;
        for (int clave : carga.igualdad) {
            heap.archivo.reiniciar_contadores();
            const auto t = Reloj::now();
            const auto p = indice.buscar(clave);
            if (!p) throw std::runtime_error("hash: clave " + std::to_string(clave));
            const Organizacion o = heap.leer(*p);
            total += us(t);
            if (o.clave != clave) throw std::runtime_error("hash: registro equivocado");
            pag_heap += static_cast<long>(heap.archivo.paginas_leidas());
        }
        m.igualdad_us = total / carga.igualdad.size();
        m.igualdad_pag_datos = static_cast<double>(pag_heap) / carga.igualdad.size();
    }
    // rango: el hash no lo soporta; el unico camino es recorrer el heap y filtrar
    auto rango = [&](const std::vector<int>& inicios, int ancho, double& t_us, double& pag) {
        long leidas = 0;
        double total = 0;
        for (int desde : inicios) {
            heap.archivo.reiniciar_contadores();
            const auto t = Reloj::now();
            std::size_t vistos = 0;
            heap.archivo.recorrer([&](const motor::RecordId&, const std::byte* d, std::uint16_t) {
                int c;
                std::memcpy(&c, d, sizeof(c));
                if (c >= desde && c < desde + ancho) ++vistos;
                return true;
            });
            total += us(t);
            leidas += static_cast<long>(heap.archivo.paginas_leidas());
            if (vistos != static_cast<std::size_t>(ancho)) throw std::runtime_error("hash: rango incompleto");
        }
        t_us = total / inicios.size();
        pag = static_cast<double>(leidas) / inicios.size();
    };
    rango(carga.inicios100, 100, m.rango100_us, m.rango100_pag);
    rango(carga.inicios1000, 1000, m.rango1000_us, m.rango1000_pag);
    // ordenamiento: recorrer el heap, ordenar (external merge sort) y releer en orden
    {
        heap.archivo.reiniciar_contadores();
        const auto t = Reloj::now();
        std::vector<std::pair<int, long long>> pares;
        heap.archivo.recorrer([&](const motor::RecordId& rid, const std::byte* d, std::uint16_t) {
            int c;
            std::memcpy(&c, d, sizeof(c));
            pares.push_back({c, pos_de(rid)});
            return true;
        });
        motor::ExternalMergeSort<std::pair<int, long long>, int> ordenador(10, 1000);
        pares = ordenador.ordenar(std::move(pares), [](const std::pair<int, long long>& p) { return p.first; });
        int anterior = INT_MIN;
        for (const auto& [c, p] : pares) {
            const Organizacion o = heap.leer(p);
            if (o.clave <= anterior) throw std::runtime_error("hash: desorden");
            anterior = o.clave;
        }
        m.ordenamiento_ms = ms(t);
        m.ordenamiento_pag = static_cast<double>(heap.archivo.paginas_leidas());
        if (pares.size() != filas.size()) throw std::runtime_error("hash: ordenamiento incompleto");
    }
    {
        heap.archivo.reiniciar_contadores();
        const auto t = Reloj::now();
        for (int clave : carga.nuevas) {
            const Organizacion o = nueva(clave);
            indice.insertar(clave, pos_de(heap.archivo.insertar_bytes(reinterpret_cast<const std::byte*>(&o), sizeof(o))));
        }
        m.insercion_us = us(t) / carga.nuevas.size();
        m.insercion_pag_escritas = static_cast<double>(heap.archivo.paginas_escritas()) / carga.nuevas.size();
    }
    {
        heap.archivo.reiniciar_contadores();
        const auto t = Reloj::now();
        for (int clave : carga.borrar) {
            const auto p = indice.buscar(clave);
            if (!p || !indice.eliminar(clave) || !heap.archivo.eliminar_rid(rid_de(*p))) throw std::runtime_error("hash: no borro");
        }
        m.eliminacion_us = us(t) / carga.borrar.size();
        m.eliminacion_pag_escritas = static_cast<double>(heap.archivo.paginas_escritas()) / carga.borrar.size();
    }
    return m;
}

const char* kEncabezado =
    "estructura,n,barajado,construccion_ms,construccion_masiva_ms,datos_ms,bytes_total,bytes_adicional,altura,"
    "igualdad_us,igualdad_pag_indice,igualdad_pag_datos,soporta_rango,rango100_us,rango100_pag,rango1000_us,rango1000_pag,"
    "ordenamiento_ms,ordenamiento_pag,insercion_us,insercion_pag_escritas,eliminacion_us,eliminacion_pag_escritas";

void guardar(const Medida& m, const Opciones& o) {
    std::filesystem::create_directories(std::filesystem::path(o.salida).parent_path());
    const bool nuevo = !std::filesystem::exists(o.salida);
    std::ofstream f(o.salida, std::ios::app);
    if (nuevo) f << kEncabezado << '\n';
    f << m.estructura << ',' << o.n << ',' << (o.barajar ? 1 : 0) << ',' << m.construccion_ms << ',' << m.construccion_masiva_ms << ','
      << m.datos_ms << ',' << m.bytes_total << ',' << m.bytes_adicional << ',' << m.altura << ',' << m.igualdad_us << ','
      << m.igualdad_pag_indice << ',' << m.igualdad_pag_datos << ',' << (m.soporta_rango ? 1 : 0) << ',' << m.rango100_us << ','
      << m.rango100_pag << ',' << m.rango1000_us << ',' << m.rango1000_pag << ',' << m.ordenamiento_ms << ',' << m.ordenamiento_pag << ','
      << m.insercion_us << ',' << m.insercion_pag_escritas << ',' << m.eliminacion_us << ',' << m.eliminacion_pag_escritas << '\n';
}

void imprimir(const Medida& m) {
    std::cout << "  " << m.estructura << ": construccion=" << m.construccion_ms << " ms"
              << (m.construccion_masiva_ms > 0 ? " (masiva " + std::to_string(m.construccion_masiva_ms) + " ms)" : "")
              << " adicional=" << m.bytes_adicional / 1024 << " KB"
              << " | igualdad=" << m.igualdad_us << " us (" << m.igualdad_pag_indice << "+" << m.igualdad_pag_datos << " pag)"
              << " | rango100=" << m.rango100_us << " us (" << m.rango100_pag << " pag)"
              << (m.soporta_rango ? "" : " [scan]")
              << " | orden=" << m.ordenamiento_ms << " ms"
              << " | ins=" << m.insercion_us << " us del=" << m.eliminacion_us << " us\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Opciones o = leer_opciones(argc, argv);
        std::filesystem::create_directories(".build");
        motor::pruebas::OpcionesCarga carga_csv;
        carga_csv.limite = o.n;
        carga_csv.barajar = o.barajar;
        carga_csv.semilla = o.semilla;
        std::vector<Organizacion> filas;
        for (const motor::Registro& r : motor::pruebas::cargar_csv(o.csv, carga_csv)) filas.push_back(convertir(r));
        std::cout << "n=" << filas.size() << (o.barajar ? " barajado" : " ordenado") << "\n";
        const CargaDeTrabajo carga = generar(o, static_cast<int>(filas.size()));

        for (const Medida& m : {medir_agrupado(filas, carga), medir_no_agrupado(filas, carga), medir_hash(filas, carga)}) {
            imprimir(m);
            guardar(m, o);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
