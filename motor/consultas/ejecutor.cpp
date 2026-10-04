#include "ejecutor.h"

#include "external_algorithms.h"
#include "../archivos/heap_file.h"
#include "../archivos/sequential_file.h"
#include "../indices/bplus_agrupado.h"
#include "../indices/bplus_no_agrupado.h"
#include "../indices/hash_extensible_disco.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace motor {
namespace sql {

namespace {

using Reloj = std::chrono::steady_clock;

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string texto(std::size_t n) { return std::to_string(n); }

// el B+ no agrupado guarda un long long: página y slot del heap empaquetados
long long pos_de(const RecordId& rid) { return static_cast<long long>(rid.page_id) * 65536 + rid.slot_id; }
RecordId rid_de(long long pos) {
    RecordId rid;
    rid.page_id = static_cast<std::uint32_t>(pos / 65536);
    rid.slot_id = static_cast<std::uint16_t>(pos % 65536);
    return rid;
}

int a_int(long long v) {
    if (v < INT_MIN) return INT_MIN;
    if (v > INT_MAX) return INT_MAX;
    return static_cast<int>(v);
}

struct FilaFisica {
    Fila fila;
    RecordId rid;  // solo heap
};

// límites [desde, hasta] que impone una condición sobre una columna entera
bool rango_de(const Condicion& c, long long& desde, long long& hasta) {
    if (!c.valor.es_entero) return false;
    desde = LLONG_MIN;
    hasta = LLONG_MAX;
    if (c.op == "=") { desde = hasta = c.valor.entero; }
    else if (c.op == "BETWEEN") { if (!c.hasta.es_entero) return false; desde = c.valor.entero; hasta = c.hasta.entero; }
    else if (c.op == "<") hasta = c.valor.entero - 1;
    else if (c.op == "<=") hasta = c.valor.entero;
    else if (c.op == ">") desde = c.valor.entero + 1;
    else if (c.op == ">=") desde = c.valor.entero;
    else return false;
    return true;
}

bool cumple(const Valor& v, const Condicion& c) {
    if (c.op == "=") return v == c.valor;
    if (c.op == "!=") return v != c.valor;
    if (c.op == "<") return v < c.valor;
    if (c.op == "<=") return v <= c.valor;
    if (c.op == ">") return v > c.valor;
    if (c.op == ">=") return v >= c.valor;
    if (c.op == "BETWEEN") return v >= c.valor && v <= c.hasta;
    return false;
}

std::string describir_cond(const Condicion& c) {
    std::string s = c.columna + " " + c.op + " " + (c.valor.es_entero ? c.valor.a_texto() : "'" + c.valor.texto + "'");
    if (c.op == "BETWEEN") s += " AND " + c.hasta.a_texto();
    return s;
}

struct IndiceAbierto {
    const Indice* meta = nullptr;
    std::unique_ptr<BPlusNoAgrupado> bplus;
    std::unique_ptr<HashExtensibleDisco> hash;

    void insertar(int clave, long long pos) {
        if (hash) hash->insertar(clave, pos, false);
        else bplus->insertar(clave, pos);
    }
    bool eliminar_entrada(int clave, long long pos) {
        return hash ? hash->eliminar_entrada(clave, pos) : bplus->eliminar_entrada(clave, pos);
    }
    std::vector<long long> buscar(int clave) { return hash ? hash->buscar(clave) : bplus->buscar(clave); }
    std::vector<long long> buscar_rango(int desde, int hasta) {
        if (hash) throw std::runtime_error("un indice hash no resuelve rangos");
        return bplus->buscar_rango(desde, hasta);
    }
    void sincronizar() {
        if (hash) hash->sincronizar();
        else bplus->sincronizar();
    }
    long paginas_leidas() const { return hash ? hash->paginas_leidas() : bplus->paginas_leidas(); }
    long num_entradas() const { return hash ? hash->num_entradas() : bplus->num_entradas(); }
    long tamano_en_disco() { return hash ? hash->tamano_en_disco() : bplus->tamano_en_disco(); }
};

std::string estructura_indice(const Indice& indice) {
    return indice.tipo == "HASH" ? "hash_extensible" : "bplus_no_agrupado";
}

std::string nombre_tipo_indice(const Indice& indice) {
    return indice.tipo == "HASH" ? "hash extensible" : "B+ no agrupado";
}

// --- acceso a los archivos de una tabla ---

class Almacen {
public:
    Almacen(const Catalogo& catalogo, const Tabla& tabla, bool crear) : tabla_(tabla) {
        (void)catalogo;
        switch (tabla.organizacion) {
            case Organizacion::HEAP:
                heap_ = std::make_unique<HeapFile>(tabla.archivo, crear);
                break;
            case Organizacion::SEQUENTIAL:
                seq_ = std::make_unique<SequentialFile>(tabla.archivo, crear);
                break;
            case Organizacion::BPLUS:
                arbol_ = std::make_unique<BPlusAgrupado>(tabla.archivo, tabla.tam_registro_fijo(), crear);
                break;
        }
        for (const Indice& indice : tabla.indices) {
            IndiceAbierto abierto;
            abierto.meta = &indice;
            if (indice.tipo == "HASH") abierto.hash = std::make_unique<HashExtensibleDisco>(indice.archivo, false);
            else abierto.bplus = std::make_unique<BPlusNoAgrupado>(indice.archivo, false);
            indices_.push_back(std::move(abierto));
        }
    }

    ~Almacen() {
        for (IndiceAbierto& abierto : indices_) abierto.sincronizar();
    }

    std::string estructura() const {
        switch (tabla_.organizacion) {
            case Organizacion::HEAP: return "heap";
            case Organizacion::SEQUENTIAL: return "secuencial";
            case Organizacion::BPLUS: return "bplus_agrupado";
        }
        return "";
    }

    // --- lectura ---

    std::vector<FilaFisica> escanear() {
        std::vector<FilaFisica> salida;
        if (heap_) {
            heap_->recorrer([&](const RecordId& rid, const std::byte* datos, std::uint16_t largo) {
                Registro r;
                if (decodificar_registro(datos, largo, r)) salida.push_back({desempaquetar_variable(tabla_, r.clave, r.valor), rid});
                return true;
            });
        } else if (seq_) {
            for (const Registro& r : seq_->scan()) salida.push_back({desempaquetar_variable(tabla_, r.clave, r.valor), {}});
        } else {
            arbol_->buscar_rango_bytes(INT_MIN, INT_MAX, [&](const void* datos) {
                salida.push_back({desempaquetar_fijo(tabla_, static_cast<const char*>(datos)), {}});
                return true;
            });
        }
        return salida;
    }

    // búsqueda por clave primaria con la estructura de la tabla (sin índices secundarios)
    std::vector<FilaFisica> buscar_pk(long long clave) {
        std::vector<FilaFisica> salida;
        if (clave < INT_MIN || clave > INT_MAX) return salida;
        if (heap_) {
            heap_->recorrer([&](const RecordId& rid, const std::byte* datos, std::uint16_t largo) {
                int c = 0;
                if (!leer_clave(datos, largo, c) || c != clave) return true;
                Registro r;
                if (decodificar_registro(datos, largo, r)) salida.push_back({desempaquetar_variable(tabla_, r.clave, r.valor), rid});
                return false;
            });
        } else if (seq_) {
            if (auto r = seq_->buscar(static_cast<int>(clave))) salida.push_back({desempaquetar_variable(tabla_, r->clave, r->valor), {}});
        } else {
            std::vector<char> buf(tabla_.tam_registro_fijo());
            if (arbol_->buscar_bytes(static_cast<int>(clave), buf.data())) salida.push_back({desempaquetar_fijo(tabla_, buf.data()), {}});
        }
        return salida;
    }

    std::vector<FilaFisica> rango_pk(long long desde, long long hasta) {
        std::vector<FilaFisica> salida;
        if (heap_) {
            for (FilaFisica& f : escanear()) {
                const long long c = f.fila[tabla_.pk].entero;
                if (c >= desde && c <= hasta) salida.push_back(std::move(f));
            }
        } else if (seq_) {
            for (const Registro& r : seq_->buscar_rango(a_int(desde), a_int(hasta))) salida.push_back({desempaquetar_variable(tabla_, r.clave, r.valor), {}});
        } else {
            arbol_->buscar_rango_bytes(a_int(desde), a_int(hasta), [&](const void* datos) {
                salida.push_back({desempaquetar_fijo(tabla_, static_cast<const char*>(datos)), {}});
                return true;
            });
        }
        return salida;
    }

    bool sabe_rango_pk() const { return seq_ != nullptr || arbol_ != nullptr; }

    IndiceAbierto* indice(const Indice* i) {
        for (IndiceAbierto& abierto : indices_) if (abierto.meta == i) return &abierto;
        return nullptr;
    }

    std::vector<FilaFisica> rango_indice(const Indice* i, long long desde, long long hasta) {
        std::vector<FilaFisica> salida;
        IndiceAbierto* abierto = indice(i);
        const std::vector<long long> posiciones = desde == hasta ? abierto->buscar(a_int(desde))
                                                                  : abierto->buscar_rango(a_int(desde), a_int(hasta));
        for (long long pos : posiciones) {
            const RecordId rid = rid_de(pos);
            auto bytes = heap_->obtener(rid);
            Registro r;
            if (bytes && decodificar_registro(bytes->data(), static_cast<std::uint16_t>(bytes->size()), r)) {
                salida.push_back({desempaquetar_variable(tabla_, r.clave, r.valor), rid});
            }
        }
        return salida;
    }

    // --- escritura ---

    void insertar(const Fila& fila) {
        validar_fila(tabla_, fila);
        const int clave = clave_de(tabla_, fila);
        if (heap_) {
            const Indice* ipk = tabla_.indice_sobre(tabla_.columnas[tabla_.pk].nombre);
            const bool repetida = ipk ? !indice(ipk)->buscar(clave).empty() : !buscar_pk(clave).empty();
            if (repetida) throw std::runtime_error("clave primaria repetida: " + std::to_string(clave));
            const std::vector<std::byte> bytes = codificar_registro({clave, empaquetar_variable(tabla_, fila)});
            const RecordId rid = heap_->insertar_bytes(bytes.data(), static_cast<std::uint16_t>(bytes.size()));
            for (IndiceAbierto& abierto : indices_) {
                abierto.insertar(static_cast<int>(fila[tabla_.posicion_columna(abierto.meta->columna)].entero), pos_de(rid));
            }
        } else if (seq_) {
            if (seq_->buscar(clave)) throw std::runtime_error("clave primaria repetida: " + std::to_string(clave));
            if (!seq_->insertar({clave, empaquetar_variable(tabla_, fila)})) throw std::runtime_error("el registro no cabe en una pagina");
        } else {
            const std::vector<char> buf = empaquetar_fijo(tabla_, fila);
            if (!arbol_->insertar_bytes(buf.data())) throw std::runtime_error("clave primaria repetida: " + std::to_string(clave));
        }
    }

    void eliminar(const FilaFisica& f) {
        const int clave = clave_de(tabla_, f.fila);
        if (heap_) {
            heap_->eliminar_rid(f.rid);
            for (IndiceAbierto& abierto : indices_) {
                abierto.eliminar_entrada(static_cast<int>(f.fila[tabla_.posicion_columna(abierto.meta->columna)].entero), pos_de(f.rid));
            }
        } else if (seq_) {
            seq_->eliminar(clave);
        } else {
            arbol_->eliminar(clave);
        }
    }

    // carga inicial de una tabla recién creada (sin índices todavía)
    void cargar(std::vector<Fila>& filas) {
        for (const Fila& fila : filas) validar_fila(tabla_, fila);
        std::unordered_set<long long> vistas;
        for (const Fila& fila : filas) {
            if (!vistas.insert(fila[tabla_.pk].entero).second) {
                throw std::runtime_error("clave primaria repetida en el archivo: " + fila[tabla_.pk].a_texto());
            }
        }
        if (arbol_) {
            std::sort(filas.begin(), filas.end(), [&](const Fila& a, const Fila& b) { return a[tabla_.pk].entero < b[tabla_.pk].entero; });
            const std::size_t tam = tabla_.tam_registro_fijo();
            std::vector<char> todo(tam * filas.size());
            for (std::size_t i = 0; i < filas.size(); ++i) {
                const std::vector<char> buf = empaquetar_fijo(tabla_, filas[i]);
                std::memcpy(todo.data() + i * tam, buf.data(), tam);
            }
            arbol_->cargar_masivo_bytes(todo.data(), filas.size());
            return;
        }
        if (seq_) {
            // carga masiva: deja todo en el área principal, sin auxiliares ni
            // reorganizaciones (ver SequentialFile::cargar_masivo)
            std::vector<Registro> registros;
            registros.reserve(filas.size());
            for (const Fila& fila : filas) registros.push_back({clave_de(tabla_, fila), empaquetar_variable(tabla_, fila)});
            seq_->cargar_masivo(registros);
            return;
        }
        // el heap también escribe cada página una sola vez (ver HeapFile::cargar_masivo)
        std::vector<std::vector<std::byte>> bytes;
        bytes.reserve(filas.size());
        for (const Fila& fila : filas) {
            bytes.push_back(codificar_registro({clave_de(tabla_, fila), empaquetar_variable(tabla_, fila)}));
        }
        heap_->cargar_masivo(bytes);
    }

    // construye un índice secundario recorriendo el heap
    void construir_indice(const Indice& indice, IndiceAbierto& abierto) {
        const int col = tabla_.posicion_columna(indice.columna);
        heap_->recorrer([&](const RecordId& rid, const std::byte* datos, std::uint16_t largo) {
            Registro r;
            if (decodificar_registro(datos, largo, r)) {
                const Fila fila = desempaquetar_variable(tabla_, r.clave, r.valor);
                abierto.insertar(static_cast<int>(fila[col].entero), pos_de(rid));
            }
            return true;
        });
        abierto.sincronizar();
    }

    // --- contadores y estadísticas ---

    void reiniciar_contadores() {
        if (heap_) heap_->reiniciar_contadores();
        if (seq_) seq_->reiniciar_contadores();
        lect_arbol_ = arbol_ ? arbol_->paginas_leidas() : 0;
        escr_arbol_ = arbol_ ? arbol_->paginas_escritas() : 0;
        lect_idx_.clear();
        for (const IndiceAbierto& abierto : indices_) lect_idx_.push_back(abierto.paginas_leidas());
    }
    std::size_t paginas_leidas() const {
        if (heap_) return heap_->paginas_leidas();
        if (seq_) return seq_->paginas_leidas();
        return static_cast<std::size_t>(arbol_->paginas_leidas() - lect_arbol_);
    }
    std::size_t paginas_escritas() const {
        if (heap_) return heap_->paginas_escritas();
        if (seq_) return seq_->paginas_escritas();
        return static_cast<std::size_t>(arbol_->paginas_escritas() - escr_arbol_);
    }
    std::size_t paginas_leidas_indice(const Indice* i) const {
        for (std::size_t k = 0; k < indices_.size(); ++k) {
            if (indices_[k].meta == i) return static_cast<std::size_t>(indices_[k].paginas_leidas() - lect_idx_[k]);
        }
        return 0;
    }

    std::size_t registros() const {
        if (heap_) return heap_->stats_heap().registros_vivos;
        if (seq_) return seq_->stats_secuencial().registros_vivos;
        return static_cast<std::size_t>(arbol_->num_registros());
    }
    std::size_t paginas() const {
        if (heap_) return heap_->num_paginas();
        if (seq_) return seq_->num_paginas();
        long internas = 0, hojas = 0;
        arbol_->contar_paginas(internas, hojas);
        return static_cast<std::size_t>(internas + hojas);
    }
    std::size_t bytes() const {
        if (heap_) return heap_->tamano_en_disco();
        if (seq_) return seq_->tamano_en_disco();
        return static_cast<std::size_t>(arbol_->tamano_en_disco());
    }
    // tamaño del archivo entre el tamaño de página: EXPLAIN no puede permitirse
    // recorrer el árbol solo para contar, igual que PostgreSQL mira pg_class
    std::size_t paginas_aprox() const {
        if (heap_) return heap_->num_paginas();
        if (seq_) return seq_->num_paginas();
        return static_cast<std::size_t>(arbol_->tamano_en_disco() / 4096);
    }
    // páginas que hay que bajar para llegar a un registro por la clave: el B+
    // agrupado y el secuencial saben navegar, el heap no
    int altura() const {
        if (arbol_) return arbol_->altura();
        if (seq_) return 2;  // búsqueda binaria sobre el área principal, más la página
        return 0;
    }
    std::string detalle() const {
        if (seq_) {
            const auto e = seq_->stats_secuencial();
            return "principal=" + texto(e.paginas_principal) + " aux=" + texto(e.paginas_auxiliares) +
                   " tumbas=" + texto(e.tumbas) + " reorganizaciones=" + texto(e.reorganizaciones);
        }
        if (heap_) {
            const auto e = heap_->stats_heap();
            return "tumbas=" + texto(e.tumbas) + " bytes_desperdiciados=" + texto(e.bytes_desperdiciados);
        }
        return "altura=" + std::to_string(arbol_->altura()) + " regs/hoja=" + std::to_string(arbol_->max_regs_hoja());
    }

private:
    const Tabla& tabla_;
    std::unique_ptr<HeapFile> heap_;
    std::unique_ptr<SequentialFile> seq_;
    std::unique_ptr<BPlusAgrupado> arbol_;
    std::vector<IndiceAbierto> indices_;
    long lect_arbol_ = 0;
    long escr_arbol_ = 0;
    std::vector<long> lect_idx_;
};

// --- planificación del WHERE ---
//
// El planificador decide primero (sin leer datos) y el ejecutor obedece. Así
// EXPLAIN a secas puede mostrar el mismo plan que correrá EXPLAIN ANALYZE sin
// tocar una sola página de datos, como hace PostgreSQL.

enum class Via { SCAN, PK_IGUAL, PK_RANGO, INDICE_IGUAL, INDICE_RANGO };

struct Plan {
    Via via = Via::SCAN;
    const Indice* indice = nullptr;
    int condicion_usada = -1;
    long long desde = 0;
    long long hasta = 0;
    std::string columna;
    std::string condicion;
    std::vector<Condicion> restantes;
};

std::string nombre_nodo(Via via, const Tabla& tabla) {
    switch (via) {
        case Via::PK_IGUAL:
        case Via::PK_RANGO:
            // en el B+ agrupado y el secuencial la clave manda sobre el orden físico
            return tabla.organizacion == Organizacion::BPLUS ? "Clustered Index Scan" : "Ordered Key Scan";
        case Via::INDICE_IGUAL: return "Index Scan";
        case Via::INDICE_RANGO: return "Index Range Scan";
        case Via::SCAN: break;
    }
    return "Seq Scan";
}

std::string id_operacion(Via via) {
    switch (via) {
        case Via::PK_IGUAL: return "busqueda_por_clave";
        case Via::PK_RANGO: return "rango_por_clave";
        case Via::INDICE_IGUAL: return "busqueda_por_indice";
        case Via::INDICE_RANGO: return "rango_por_indice";
        case Via::SCAN: break;
    }
    return "scan_completo";
}

// valida tipos y elige con qué estructura resolver el WHERE; no toca disco
Plan planificar(const Tabla& tabla, const std::vector<Condicion>& condiciones, bool indices_disponibles) {
    Plan plan;
    const std::string pk = tabla.columnas[tabla.pk].nombre;

    for (std::size_t i = 0; i < condiciones.size(); ++i) {
        const Condicion& c = condiciones[i];
        const int col = tabla.posicion_columna(c.columna);
        if (col < 0) throw std::runtime_error("la columna " + c.columna + " no existe en " + tabla.nombre);
        if (tabla.columnas[col].tipo == TipoColumna::INT && !c.valor.es_entero) throw std::runtime_error("la columna " + c.columna + " es INT");
        if (tabla.columnas[col].tipo == TipoColumna::VARCHAR && c.valor.es_entero) throw std::runtime_error("la columna " + c.columna + " es VARCHAR");
        if (plan.condicion_usada >= 0) continue;

        long long desde, hasta;
        if (!rango_de(c, desde, hasta)) continue;

        const bool es_pk = minusculas(c.columna) == minusculas(pk);
        const Indice* indice = indices_disponibles ? tabla.indice_sobre(c.columna) : nullptr;
        if (indice && indice->tipo == "HASH" && desde != hasta) indice = nullptr;
        const bool sabe_rango = tabla.organizacion != Organizacion::HEAP;

        if (indice) {
            plan.via = desde == hasta ? Via::INDICE_IGUAL : Via::INDICE_RANGO;
            plan.indice = indice;
        } else if (es_pk && desde == hasta && sabe_rango) {
            plan.via = Via::PK_IGUAL;
        } else if (es_pk && sabe_rango) {
            plan.via = Via::PK_RANGO;
        } else {
            continue;  // sin estructura que ayude: que lo resuelva el filtro
        }
        plan.condicion_usada = static_cast<int>(i);
        plan.desde = desde;
        plan.hasta = hasta;
        plan.columna = c.columna;
        plan.condicion = describir_cond(c);
    }

    for (std::size_t i = 0; i < condiciones.size(); ++i) {
        if (static_cast<int>(i) != plan.condicion_usada) plan.restantes.push_back(condiciones[i]);
    }
    return plan;
}

// Filas que se esperan del acceso. Asume claves densas y repartidas parejo: es
// lo único que el catálogo permite suponer, y se dice en el plan.
long long filas_estimadas(const Plan& plan, std::size_t registros) {
    const long long n = static_cast<long long>(registros);
    switch (plan.via) {
        case Via::PK_IGUAL:
            return 1;  // la clave primaria es única
        case Via::INDICE_IGUAL:
            // sin estadísticas de valores distintos se usa el 0,5 % de la tabla,
            // la selectividad por omisión que aplica PostgreSQL en este caso
            return std::max<long long>(1, n / 200);
        case Via::PK_RANGO:
        case Via::INDICE_RANGO: {
            if (plan.desde == LLONG_MIN || plan.hasta == LLONG_MAX) return n;
            const long long ancho = plan.hasta - plan.desde + 1;
            return std::max<long long>(1, std::min(n, ancho));
        }
        case Via::SCAN: break;
    }
    return n;
}

// Páginas que se espera leer. El acceso por índice no agrupado paga una página
// de datos por fila encontrada: eso es justo lo que lo vuelve caro en rangos
// anchos, y verlo en el costo es el punto del plan.
double costo_estimado(const Plan& plan, std::size_t paginas, std::size_t registros, long long filas, int altura) {
    const double alto = altura > 0 ? altura : 3;
    const double total = static_cast<double>(paginas);
    switch (plan.via) {
        case Via::PK_IGUAL: return alto;
        case Via::PK_RANGO: {
            // las filas están juntas en disco: solo se leen las páginas que cubren el rango
            const double fraccion = registros == 0 ? 1.0 : static_cast<double>(filas) / static_cast<double>(registros);
            return alto + std::min(total, std::max(1.0, total * fraccion));
        }
        case Via::INDICE_IGUAL:
        case Via::INDICE_RANGO: return alto + static_cast<double>(filas);
        case Via::SCAN: break;
    }
    return total;
}

struct Acceso {
    std::vector<FilaFisica> filas;
    std::vector<Condicion> restantes;
    PasoPlan paso;
};

// ejecuta la vía que eligió el planificador y mide lo que costó de verdad
Acceso acceder(Almacen& almacen, const Tabla& tabla, const std::vector<Condicion>& condiciones) {
    const Plan plan = planificar(tabla, condiciones, true);
    Acceso acceso;
    acceso.restantes = plan.restantes;

    const auto inicio = Reloj::now();
    almacen.reiniciar_contadores();
    switch (plan.via) {
        case Via::PK_IGUAL: acceso.filas = almacen.buscar_pk(plan.desde); break;
        case Via::PK_RANGO: acceso.filas = almacen.rango_pk(plan.desde, plan.hasta); break;
        case Via::INDICE_IGUAL:
        case Via::INDICE_RANGO: acceso.filas = almacen.rango_indice(plan.indice, plan.desde, plan.hasta); break;
        case Via::SCAN: acceso.filas = almacen.escanear(); break;
    }
    const double ms = std::chrono::duration<double, std::milli>(Reloj::now() - inicio).count();

    PasoPlan& paso = acceso.paso;
    paso.operacion = id_operacion(plan.via);
    paso.nodo = nombre_nodo(plan.via, tabla);
    paso.relacion = tabla.nombre;
    paso.columna = plan.columna;
    paso.condicion = plan.condicion;
    paso.filas_reales = static_cast<long long>(acceso.filas.size());
    paso.tiempo_ms = ms;
    paso.filas_estimadas = filas_estimadas(plan, almacen.registros());
    paso.costo = costo_estimado(plan, almacen.paginas_aprox(), almacen.registros(), paso.filas_estimadas, almacen.altura());

    if (plan.indice) {
        paso.indice = plan.indice->nombre;
        const std::size_t p_indice = almacen.paginas_leidas_indice(plan.indice);
        const std::size_t p_datos = almacen.paginas_leidas();
        paso.paginas_leidas = static_cast<long long>(p_indice + p_datos);
        paso.detalles = {{"indice", plan.indice->nombre}, {"estructura", estructura_indice(*plan.indice)}, {"columna", plan.columna},
                         {"condicion", plan.condicion}, {"paginas_indice", texto(p_indice)},
                         {"paginas_heap", texto(p_datos)}, {"filas", texto(acceso.filas.size())}};
    } else {
        paso.paginas_leidas = static_cast<long long>(almacen.paginas_leidas());
        paso.detalles = {{"estructura", almacen.estructura()}};
        if (!plan.columna.empty()) {
            paso.detalles.push_back({"columna", plan.columna});
            paso.detalles.push_back({"condicion", plan.condicion});
        }
        paso.detalles.push_back({"paginas_leidas", texto(almacen.paginas_leidas())});
        paso.detalles.push_back({"filas", texto(acceso.filas.size())});
        if (plan.via == Via::SCAN && !condiciones.empty()) {
            paso.detalles.push_back({"nota", tabla.organizacion == Organizacion::HEAP
                                                 ? "no hay indice sobre la columna: recorrido completo"
                                                 : "la condicion no usa la clave: recorrido completo"});
        }
    }
    return acceso;
}

void filtrar(Acceso& acceso, const Tabla& tabla, std::vector<PasoPlan>& plan) {
    if (acceso.restantes.empty()) return;
    std::vector<FilaFisica> salida;
    std::string descripcion;
    std::string columnas;
    for (const Condicion& c : acceso.restantes) {
        if (tabla.posicion_columna(c.columna) < 0) throw std::runtime_error("la columna " + c.columna + " no existe en " + tabla.nombre);
        descripcion += (descripcion.empty() ? "" : " AND ") + describir_cond(c);
        columnas += (columnas.empty() ? "" : ", ") + c.columna;
    }
    const auto inicio = Reloj::now();
    for (FilaFisica& f : acceso.filas) {
        bool ok = true;
        for (const Condicion& c : acceso.restantes) {
            if (!cumple(f.fila[tabla.posicion_columna(c.columna)], c)) { ok = false; break; }
        }
        if (ok) salida.push_back(std::move(f));
    }
    PasoPlan paso;
    paso.operacion = "filtro";
    paso.nodo = "Filter";
    paso.relacion = tabla.nombre;
    paso.columna = columnas;
    paso.condicion = descripcion;
    paso.filas_reales = static_cast<long long>(salida.size());
    paso.tiempo_ms = std::chrono::duration<double, std::milli>(Reloj::now() - inicio).count();
    paso.detalles = {{"condicion", descripcion}, {"entrada", texto(acceso.filas.size())}, {"salida", texto(salida.size())},
                     {"descartadas", texto(acceso.filas.size() - salida.size())}};
    plan.push_back(paso);
    acceso.filas = std::move(salida);
}

// El plan se construye en orden de ejecución; EXPLAIN lo lee al revés, con la
// raíz arriba y el acceso a disco al fondo.
void ordenar_como_explain(std::vector<PasoPlan>& plan) {
    std::reverse(plan.begin(), plan.end());
    for (std::size_t i = 0; i < plan.size(); ++i) plan[i].nivel = static_cast<int>(i);
}

std::string con_decimales(double v, int n) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", n, v);
    return buf;
}

std::vector<std::string> partir_lineas(const std::string& texto) {
    std::vector<std::string> salida;
    std::size_t inicio = 0;
    while (true) {
        const std::size_t fin = texto.find('\n', inicio);
        salida.push_back(texto.substr(inicio, fin == std::string::npos ? std::string::npos : fin - inicio));
        if (fin == std::string::npos) return salida;
        inicio = fin + 1;
    }
}

// una línea por nodo, con la sangría y los paréntesis de EXPLAIN
std::string linea_explain(const PasoPlan& p, bool analizado) {
    std::string s(static_cast<std::size_t>(p.nivel) * 2, ' ');
    if (p.nivel > 0) s += "-> ";
    s += p.nodo.empty() ? p.operacion : p.nodo;
    if (!p.indice.empty()) s += " using " + p.indice;
    if (!p.relacion.empty()) s += " on " + p.relacion;
    if (p.costo >= 0) s += "  (cost=" + con_decimales(p.costo, 2) + " rows=" + std::to_string(std::max<long long>(0, p.filas_estimadas)) + ")";
    else if (p.filas_estimadas >= 0) s += "  (rows=" + std::to_string(p.filas_estimadas) + ")";
    if (analizado && p.filas_reales >= 0) {
        s += " (actual ";
        if (p.tiempo_ms >= 0) s += "time=" + con_decimales(p.tiempo_ms, 3) + " ";
        s += "rows=" + std::to_string(p.filas_reales);
        if (p.paginas_leidas >= 0) s += " pages=" + std::to_string(p.paginas_leidas);
        s += ")";
    }
    // los detalles cuelgan alineados con el nombre del nodo, no con la flecha
    const std::string sangria = std::string(static_cast<std::size_t>(p.nivel) * 2 + (p.nivel > 0 ? 3 : 0), ' ');
    if (!p.condicion.empty()) {
        const char* etiqueta = "Filter: ";
        if (!p.indice.empty()) etiqueta = "Index Cond: ";
        else if (p.operacion == "ordenamiento") etiqueta = "Sort Key: ";
        else if (p.operacion.rfind("busqueda", 0) == 0 || p.operacion.rfind("rango", 0) == 0) etiqueta = "Key Cond: ";
        s += "\n" + sangria + etiqueta + p.condicion;
    }
    if (p.operacion == "agrupacion" && !p.columna.empty()) s += "\n" + sangria + "Group Key: " + p.columna;
    if (p.operacion == "proyeccion" && !p.columna.empty()) s += "\n" + sangria + "Output: " + p.columna;
    for (const auto& [k, v] : p.detalles) {
        if (k == "nota") s += "\n" + sangria + "Nota: " + v;
    }
    return s;
}

// --- CSV ---

std::vector<std::vector<std::string>> leer_csv(const std::string& ruta) {
    std::ifstream entrada(ruta, std::ios::binary);
    if (!entrada) throw std::runtime_error("no se pudo abrir " + ruta);
    std::vector<std::vector<std::string>> filas;
    std::string linea;
    while (std::getline(entrada, linea)) {
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.empty()) continue;
        std::vector<std::string> campos;
        std::string actual;
        bool comillas = false;
        for (std::size_t i = 0; i < linea.size(); ++i) {
            const char c = linea[i];
            if (c == '"') {
                if (comillas && i + 1 < linea.size() && linea[i + 1] == '"') { actual.push_back('"'); ++i; }
                else comillas = !comillas;
            } else if (c == ',' && !comillas) {
                campos.push_back(actual);
                actual.clear();
            } else {
                actual.push_back(c);
            }
        }
        campos.push_back(actual);
        filas.push_back(std::move(campos));
    }
    return filas;
}

std::string identificador_valido(const std::string& original) {
    std::string s;
    for (const char c : original) s.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) s = "c_" + s;
    return s;
}

bool es_entero(const std::string& s, long long& v) {
    if (s.empty() || s.size() > 11) return false;
    std::size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    v = std::stoll(s);
    return v >= INT_MIN && v <= INT_MAX;
}

}  // namespace

// --- Ejecutor ---

Resultado Ejecutor::ejecutar(const std::string& sql) {
    const auto inicio = Reloj::now();
    const Sentencia s = parsear(sql);
    parseo_ms_ = std::chrono::duration<double, std::milli>(Reloj::now() - inicio).count();
    Resultado r;
    try {
        r = ejecutar(s);
    } catch (...) {
        parseo_ms_ = 0.0;
        throw;
    }
    parseo_ms_ = 0.0;
    return r;
}

Resultado Ejecutor::ejecutar(const Sentencia& s) {
    const auto inicio = Reloj::now();
    Resultado r;
    switch (s.tipo) {
        case TipoSentencia::CREATE_TABLE: r = crear_tabla(s); break;
        case TipoSentencia::CREATE_TABLE_FROM_FILE: r = crear_tabla_desde_csv(s); break;
        case TipoSentencia::COPY_FROM_FILE: r = copiar_desde_csv(s); break;
        case TipoSentencia::CREATE_INDEX: r = crear_indice(s); break;
        case TipoSentencia::DROP_TABLE: r = borrar_tabla(s); break;
        case TipoSentencia::INSERT: r = insertar(s); break;
        case TipoSentencia::DELETE_FROM: r = eliminar(s); break;
        case TipoSentencia::SELECT: r = seleccionar(s); break;
        case TipoSentencia::SHOW_TABLES: r = mostrar_tablas(); break;
        case TipoSentencia::DESCRIBE: r = describir(s); break;
        case TipoSentencia::EXPLAIN: r = explicar(s); break;
    }
    r.tiempo_ms = std::chrono::duration<double, std::milli>(Reloj::now() - inicio).count();
    return r;
}

// EXPLAIN: arma el plan sin leer datos. EXPLAIN ANALYZE ejecuta de verdad y
// devuelve el mismo plan con las filas, páginas y tiempos que costó.
Resultado Ejecutor::explicar(const Sentencia& s) {
    if (!s.explicada) throw std::runtime_error("EXPLAIN necesita una sentencia");
    const Sentencia& interna = *s.explicada;

    Resultado r;
    r.tipo = "explain";
    r.columnas = {"QUERY PLAN"};
    r.analizado = s.analyze;

    const bool consulta = interna.tipo == TipoSentencia::SELECT || interna.tipo == TipoSentencia::DELETE_FROM;
    if (!s.analyze && !consulta) {
        throw std::runtime_error("EXPLAIN sin ANALYZE solo describe SELECT y DELETE; usa EXPLAIN ANALYZE para el resto");
    }

    // Con ANALYZE se cronometran las dos fases por separado, igual que
    // PostgreSQL: primero decidir el plan, después ejecutarlo.
    const auto inicio_plan = Reloj::now();
    Resultado ejecutada;
    if (s.analyze) {
        if (consulta) {
            const Tabla& tabla = catalogo_.tabla(interna.tabla);
            Almacen almacen(catalogo_, tabla, false);
            const Plan elegido = planificar(tabla, interna.condiciones, true);
            const long long filas = filas_estimadas(elegido, almacen.registros());
            (void)costo_estimado(elegido, almacen.paginas_aprox(), almacen.registros(), filas, almacen.altura());
        }
        r.planificacion_ms = parseo_ms_ + std::chrono::duration<double, std::milli>(Reloj::now() - inicio_plan).count();
        ejecutada = ejecutar(interna);
        r.ejecucion_ms = ejecutada.tiempo_ms;
        r.plan = ejecutada.plan;
    } else {
        const Tabla& tabla = catalogo_.tabla(interna.tabla);
        // abrir la tabla solo lee la cabecera: es lo que PostgreSQL saca de pg_class
        Almacen almacen(catalogo_, tabla, false);
        const Plan plan = planificar(tabla, interna.condiciones, true);

        PasoPlan paso;
        paso.operacion = id_operacion(plan.via);
        paso.nodo = nombre_nodo(plan.via, tabla);
        paso.relacion = tabla.nombre;
        paso.columna = plan.columna;
        paso.condicion = plan.condicion;
        const std::size_t paginas = almacen.paginas_aprox();
        paso.filas_estimadas = filas_estimadas(plan, almacen.registros());
        paso.costo = costo_estimado(plan, paginas, almacen.registros(), paso.filas_estimadas, almacen.altura());
        paso.detalles = {{"estructura", plan.indice ? estructura_indice(*plan.indice) : almacen.estructura()},
                         {"paginas_tabla", texto(paginas)}, {"registros", texto(almacen.registros())}};
        if (plan.indice) {
            paso.indice = plan.indice->nombre;
            paso.detalles.insert(paso.detalles.begin(), {"indice", plan.indice->nombre});
            paso.detalles.push_back({"columna", plan.columna});
            paso.detalles.push_back({"nota", "el indice no agrupado lee una pagina de datos por fila encontrada"});
        } else if (!plan.columna.empty()) {
            paso.detalles.push_back({"columna", plan.columna});
        } else if (!interna.condiciones.empty()) {
            paso.detalles.push_back({"nota", "ninguna condicion se resuelve con una estructura"});
        }
        r.plan.push_back(paso);

        long long filas = paso.filas_estimadas;
        if (!plan.restantes.empty()) {
            std::string descripcion;
            std::string columnas;
            for (const Condicion& c : plan.restantes) {
                if (tabla.posicion_columna(c.columna) < 0) throw std::runtime_error("la columna " + c.columna + " no existe en " + tabla.nombre);
                descripcion += (descripcion.empty() ? "" : " AND ") + describir_cond(c);
                columnas += (columnas.empty() ? "" : ", ") + c.columna;
            }
            filas = std::max<long long>(1, filas / 10);  // selectividad por omisión del filtro
            PasoPlan f;
            f.operacion = "filtro";
            f.nodo = "Filter";
            f.relacion = tabla.nombre;
            f.columna = columnas;
            f.condicion = descripcion;
            f.filas_estimadas = filas;
            f.costo = paso.costo;
            f.detalles = {{"condicion", descripcion}, {"nota", "estimacion: 10% de las filas pasa el filtro"}};
            r.plan.push_back(f);
        }
        if (interna.tipo == TipoSentencia::SELECT) {
            if (!interna.group_by.empty() || std::any_of(interna.items.begin(), interna.items.end(),
                                                         [](const ItemSelect& i) { return !i.agregado.empty(); })) {
                PasoPlan g;
                g.operacion = "agrupacion";
                g.nodo = "HashAggregate";
                g.relacion = tabla.nombre;
                g.columna = interna.group_by;
                g.filas_estimadas = interna.group_by.empty() ? 1 : std::max<long long>(1, filas / 10);
                g.costo = paso.costo;
                g.detalles = {{"algoritmo", "external_hash_aggregate"}, {"columna", interna.group_by.empty() ? "(todo)" : interna.group_by}};
                r.plan.push_back(g);
                filas = g.filas_estimadas;
            }
            if (!interna.order_by.empty()) {
                PasoPlan o;
                o.operacion = "ordenamiento";
                o.nodo = "Sort";
                o.relacion = tabla.nombre;
                o.columna = interna.order_by;
                o.condicion = interna.order_by + (interna.descendente ? " DESC" : " ASC");
                o.filas_estimadas = filas;
                o.costo = paso.costo;
                o.detalles = {{"algoritmo", "external_merge_sort"}, {"orden", interna.descendente ? "DESC" : "ASC"}};
                r.plan.push_back(o);
            }
            if (interna.limite >= 0) {
                PasoPlan l;
                l.operacion = "limite";
                l.nodo = "Limit";
                l.relacion = tabla.nombre;
                l.filas_estimadas = std::min(filas, interna.limite);
                l.costo = paso.costo;
                l.detalles = {{"filas", std::to_string(interna.limite)}};
                r.plan.push_back(l);
            }
        } else {
            PasoPlan d;
            d.operacion = "eliminar";
            d.nodo = "Delete";
            d.relacion = tabla.nombre;
            d.filas_estimadas = filas;
            d.costo = paso.costo;
            d.detalles = {{"estructura", almacen.estructura()}, {"modo", "lazy: se marcan tumbas"}};
            r.plan.push_back(d);
        }
        ordenar_como_explain(r.plan);
        r.planificacion_ms = parseo_ms_ + std::chrono::duration<double, std::milli>(Reloj::now() - inicio_plan).count();
    }

    for (const PasoPlan& p : r.plan) {
        for (const std::string& linea : partir_lineas(linea_explain(p, r.analizado))) {
            r.filas.push_back({Valor::de_texto(linea)});
        }
    }
    if (s.analyze) {
        r.filas.push_back({Valor::de_texto("Rows: " + std::to_string(ejecutada.afectadas))});
        r.filas.push_back({Valor::de_texto("Planning Time: " + con_decimales(r.planificacion_ms, 3) + " ms")});
        r.filas.push_back({Valor::de_texto("Execution Time: " + con_decimales(r.ejecucion_ms, 3) + " ms")});
    } else {
        r.filas.push_back({Valor::de_texto("Nota: costo en paginas estimadas; rows supone claves densas y repartidas parejo")});
        r.filas.push_back({Valor::de_texto("Planning Time: " + con_decimales(r.planificacion_ms, 3) + " ms")});
    }
    r.afectadas = r.filas.size();
    r.mensaje = s.analyze ? "plan con medidas reales" : "plan estimado, sin ejecutar la consulta";
    return r;
}

Resultado Ejecutor::crear_tabla(const Sentencia& s) {
    if (catalogo_.existe(s.tabla)) throw std::runtime_error("la tabla " + s.tabla + " ya existe");
    if (s.columnas.empty()) throw std::runtime_error("la tabla necesita columnas");

    Tabla tabla;
    tabla.nombre = s.tabla;
    tabla.organizacion = organizacion_desde(s.organizacion);
    int pk = -1;
    std::set<std::string> nombres;
    for (std::size_t i = 0; i < s.columnas.size(); ++i) {
        const ColumnaDef& def = s.columnas[i];
        if (!nombres.insert(minusculas(def.nombre)).second) throw std::runtime_error("columna repetida: " + def.nombre);
        Columna col;
        col.nombre = def.nombre;
        col.tipo = def.tipo == "INT" ? TipoColumna::INT : TipoColumna::VARCHAR;
        col.tam = static_cast<std::uint16_t>(def.tam);
        if (def.pk) {
            if (pk >= 0) throw std::runtime_error("solo una columna puede ser PRIMARY KEY");
            if (col.tipo != TipoColumna::INT) throw std::runtime_error("la clave primaria debe ser INT");
            pk = static_cast<int>(i);
        }
        tabla.columnas.push_back(col);
    }
    if (!s.pk.empty()) {
        pk = tabla.posicion_columna(s.pk);
        if (pk < 0) throw std::runtime_error("la columna " + s.pk + " no existe");
    }
    if (pk < 0) {
        for (std::size_t i = 0; i < tabla.columnas.size() && pk < 0; ++i) if (tabla.columnas[i].tipo == TipoColumna::INT) pk = static_cast<int>(i);
        if (pk < 0) throw std::runtime_error("la tabla necesita una columna INT como clave primaria");
    }
    tabla.pk = static_cast<std::size_t>(pk);
    if (tabla.organizacion == Organizacion::BPLUS && tabla.tam_registro_fijo() > 2040) {
        throw std::runtime_error("el registro fijo mide " + texto(tabla.tam_registro_fijo()) + " B y el B+ agrupado admite hasta 2040");
    }
    const char* ext = tabla.organizacion == Organizacion::HEAP ? ".heap" : tabla.organizacion == Organizacion::SEQUENTIAL ? ".seq" : ".bpa";
    tabla.archivo = catalogo_.ruta_datos(tabla.nombre, ext);

    { Almacen almacen(catalogo_, tabla, true); }
    catalogo_.agregar(tabla);
    catalogo_.guardar();

    Resultado r;
    r.tipo = "create_table";
    r.mensaje = "tabla " + tabla.nombre + " creada con organizacion " + nombre_organizacion(tabla.organizacion) +
                ", clave primaria " + tabla.columnas[tabla.pk].nombre;
    r.plan.push_back({"crear_tabla", {{"estructura", nombre_organizacion(tabla.organizacion)}, {"archivo", tabla.archivo}}});
    return r;
}

// COPY t FROM FILE 'ruta.csv': carga datos en una tabla que ya existe. Separa
// la definición del esquema de la carga, que es lo que hace `COPY` en
// PostgreSQL o `LOAD DATA` en MySQL.
Resultado Ejecutor::copiar_desde_csv(const Sentencia& s) {
    Tabla& tabla = catalogo_.tabla(s.tabla);
    const auto inicio_lectura = Reloj::now();
    const std::vector<std::vector<std::string>> csv = leer_csv(s.archivo_csv);
    if (csv.size() < 2) throw std::runtime_error("el CSV no tiene filas de datos");
    const std::size_t ncol = csv[0].size();
    if (ncol != tabla.columnas.size()) {
        throw std::runtime_error("el CSV tiene " + texto(ncol) + " columnas y " + tabla.nombre +
                                 " tiene " + texto(tabla.columnas.size()));
    }
    // los encabezados deben coincidir en nombre y orden: evita cargar datos en la columna equivocada
    for (std::size_t c = 0; c < ncol; ++c) {
        const std::string encabezado = identificador_valido(csv[0][c]);
        if (minusculas(encabezado) != minusculas(tabla.columnas[c].nombre)) {
            throw std::runtime_error("la columna " + texto(c + 1) + " del CSV es '" + encabezado +
                                     "' y en la tabla es '" + tabla.columnas[c].nombre + "'");
        }
    }

    std::vector<Fila> filas;
    filas.reserve(csv.size() - 1);
    for (std::size_t f = 1; f < csv.size(); ++f) {
        if (csv[f].size() != ncol) {
            throw std::runtime_error("la fila " + texto(f + 1) + " tiene " + texto(csv[f].size()) + " campos, esperaba " + texto(ncol));
        }
        Fila fila(ncol);
        for (std::size_t c = 0; c < ncol; ++c) {
            if (tabla.columnas[c].tipo != TipoColumna::INT) { fila[c] = Valor::de_texto(csv[f][c]); continue; }
            long long v;
            if (!es_entero(csv[f][c], v)) {
                throw std::runtime_error("la fila " + texto(f + 1) + " trae '" + csv[f][c] + "' en la columna INT " + tabla.columnas[c].nombre);
            }
            fila[c] = Valor::de_entero(v);
        }
        filas.push_back(std::move(fila));
    }
    const double t_lectura = std::chrono::duration<double, std::milli>(Reloj::now() - inicio_lectura).count();

    const auto inicio_carga = Reloj::now();
    Almacen almacen(catalogo_, tabla, false);
    const bool vacia = almacen.registros() == 0 && tabla.indices.empty();
    almacen.reiniciar_contadores();
    // con la tabla vacía se puede escribir de una vez; si ya tiene datos hay que
    // insertar fila a fila para respetar el orden y mantener los índices
    if (vacia) {
        almacen.cargar(filas);
    } else {
        for (const Fila& fila : filas) almacen.insertar(fila);
    }
    const double t_carga = std::chrono::duration<double, std::milli>(Reloj::now() - inicio_carga).count();

    Resultado r;
    r.tipo = "copy";
    r.afectadas = filas.size();
    r.mensaje = texto(filas.size()) + " filas cargadas de " + s.archivo_csv + " en " + tabla.nombre;
    r.plan.push_back({"leer_csv", {{"archivo", s.archivo_csv}, {"filas", texto(filas.size())}, {"columnas", texto(ncol)},
                                   {"tiempo_ms", std::to_string(t_lectura)}}});
    r.plan.push_back({vacia ? "carga_masiva" : "insercion_una_a_una",
                      {{"estructura", nombre_organizacion(tabla.organizacion)},
                       {"paginas_escritas", texto(almacen.paginas_escritas())},
                       {"indices_actualizados", texto(tabla.indices.size())},
                       {"modo", vacia ? "tabla vacia: se escribe el archivo de una vez"
                                      : "la tabla ya tenia datos o indices: una insercion por fila"},
                       {"tiempo_ms", std::to_string(t_carga)}}});
    return r;
}

Resultado Ejecutor::crear_tabla_desde_csv(const Sentencia& s) {
    if (catalogo_.existe(s.tabla)) throw std::runtime_error("la tabla " + s.tabla + " ya existe");
    const auto inicio_lectura = Reloj::now();
    std::vector<std::vector<std::string>> csv = leer_csv(s.archivo_csv);
    if (csv.size() < 2) throw std::runtime_error("el CSV no tiene filas de datos");
    const std::size_t ncol = csv[0].size();

    // esquema inferido: INT si toda la columna son enteros, si no VARCHAR del largo maximo
    Sentencia definicion = s;
    definicion.tipo = TipoSentencia::CREATE_TABLE;
    std::set<std::string> usados;
    for (std::size_t c = 0; c < ncol; ++c) {
        ColumnaDef def;
        def.nombre = identificador_valido(csv[0][c]);
        while (!usados.insert(minusculas(def.nombre)).second) def.nombre += "_";
        bool entera = true;
        std::size_t maximo = 1;
        for (std::size_t f = 1; f < csv.size(); ++f) {
            if (csv[f].size() != ncol) throw std::runtime_error("la fila " + texto(f + 1) + " tiene " + texto(csv[f].size()) + " campos, esperaba " + texto(ncol));
            long long v;
            if (entera && !es_entero(csv[f][c], v)) entera = false;
            maximo = std::max(maximo, csv[f][c].size());
        }
        def.tipo = entera ? "INT" : "VARCHAR";
        def.tam = entera ? 0 : static_cast<int>(maximo);
        definicion.columnas.push_back(def);
    }
    Resultado r = crear_tabla(definicion);
    Tabla& tabla = catalogo_.tabla(s.tabla);

    std::vector<Fila> filas;
    filas.reserve(csv.size() - 1);
    for (std::size_t f = 1; f < csv.size(); ++f) {
        Fila fila(ncol);
        for (std::size_t c = 0; c < ncol; ++c) {
            if (tabla.columnas[c].tipo == TipoColumna::INT) fila[c] = Valor::de_entero(std::stoll(csv[f][c]));
            else fila[c] = Valor::de_texto(csv[f][c]);
        }
        filas.push_back(std::move(fila));
    }
    const double t_lectura = std::chrono::duration<double, std::milli>(Reloj::now() - inicio_lectura).count();

    const auto inicio_carga = Reloj::now();
    std::size_t escritas = 0;
    try {
        Almacen almacen(catalogo_, tabla, false);
        almacen.reiniciar_contadores();
        almacen.cargar(filas);
        escritas = almacen.paginas_escritas();
    } catch (...) {
        std::filesystem::remove(tabla.archivo);
        catalogo_.quitar(s.tabla);
        catalogo_.guardar();
        throw;
    }
    const double t_carga = std::chrono::duration<double, std::milli>(Reloj::now() - inicio_carga).count();

    r.afectadas = filas.size();
    r.mensaje += "; " + texto(filas.size()) + " filas cargadas de " + s.archivo_csv;
    r.plan.push_back({"leer_csv", {{"archivo", s.archivo_csv}, {"filas", texto(filas.size())}, {"columnas", texto(ncol)}, {"tiempo_ms", std::to_string(t_lectura)}}});
    r.plan.push_back({tabla.organizacion == Organizacion::BPLUS ? "carga_masiva" : "insercion_secuencial",
                      {{"estructura", nombre_organizacion(tabla.organizacion)},
                       {"paginas_escritas", texto(escritas)},
                       {"bytes_por_registro", tabla.organizacion == Organizacion::BPLUS
                                                  ? texto(tabla.tam_registro_fijo()) + " (fijo: los VARCHAR se rellenan)"
                                                  : "variable"},
                       {"tiempo_ms", std::to_string(t_carga)}}});

    // INDEX (a, b): los índices pedidos se construyen con la tabla ya cargada
    for (const std::string& columna : s.indices) {
        Sentencia crear;
        crear.tipo = TipoSentencia::CREATE_INDEX;
        crear.tabla = s.tabla;
        crear.indice_columna = columna;
        crear.indice_nombre = minusculas(s.tabla) + "_" + minusculas(columna) + "_idx";
        crear.indice_tipo = "BPLUS";
        const Resultado ri = crear_indice(crear);
        r.mensaje += "; " + ri.mensaje;
        for (const PasoPlan& p : ri.plan) r.plan.push_back(p);
    }
    return r;
}

Resultado Ejecutor::crear_indice(const Sentencia& s) {
    Tabla& tabla = catalogo_.tabla(s.tabla);
    if (tabla.organizacion != Organizacion::HEAP) throw std::runtime_error("los indices secundarios solo se admiten sobre tablas HEAP (las otras reubican registros)");
    const int col = tabla.posicion_columna(s.indice_columna);
    if (col < 0) throw std::runtime_error("la columna " + s.indice_columna + " no existe");
    if (tabla.columnas[col].tipo != TipoColumna::INT) throw std::runtime_error("solo se indexan columnas INT");
    if (tabla.indice_sobre(s.indice_columna)) throw std::runtime_error("la columna " + s.indice_columna + " ya tiene indice");
    for (const Indice& i : tabla.indices) if (minusculas(i.nombre) == minusculas(s.indice_nombre)) throw std::runtime_error("el indice " + s.indice_nombre + " ya existe");

    const bool es_hash = s.indice_tipo == "HASH";
    Indice indice{s.indice_nombre, tabla.columnas[col].nombre, es_hash ? "HASH" : "BPLUS",
                  catalogo_.ruta_datos(tabla.nombre + "__" + minusculas(s.indice_nombre), es_hash ? ".hash" : ".bplus")};
    long entradas = 0;
    long disco = 0;
    {
        Almacen almacen(catalogo_, tabla, false);
        IndiceAbierto abierto;
        abierto.meta = &indice;
        if (es_hash) abierto.hash = std::make_unique<HashExtensibleDisco>(indice.archivo, true);
        else abierto.bplus = std::make_unique<BPlusNoAgrupado>(indice.archivo, true);
        almacen.construir_indice(indice, abierto);
        entradas = abierto.num_entradas();
        disco = abierto.tamano_en_disco();
    }
    tabla.indices.push_back(indice);
    catalogo_.guardar();

    Resultado r;
    r.tipo = "create_index";
    r.afectadas = static_cast<std::size_t>(entradas);
    r.mensaje = "indice " + indice.nombre + " (" + nombre_tipo_indice(indice) + ") creado sobre " + tabla.nombre + "." + indice.columna + " con " + std::to_string(entradas) + " entradas";
    r.plan.push_back({"construir_indice", {{"estructura", estructura_indice(indice)}, {"entradas", std::to_string(entradas)}, {"bytes", std::to_string(disco)}, {"archivo", indice.archivo}}});
    return r;
}

Resultado Ejecutor::borrar_tabla(const Sentencia& s) {
    const Tabla tabla = catalogo_.tabla(s.tabla);
    std::filesystem::remove(tabla.archivo);
    for (const Indice& i : tabla.indices) std::filesystem::remove(i.archivo);
    catalogo_.quitar(s.tabla);
    catalogo_.guardar();
    Resultado r;
    r.tipo = "drop_table";
    r.mensaje = "tabla " + tabla.nombre + " eliminada";
    r.plan.push_back({"eliminar_archivos", {{"tabla", tabla.nombre}, {"archivo", tabla.archivo}, {"indices_borrados", texto(tabla.indices.size())}}});
    return r;
}

Resultado Ejecutor::insertar(const Sentencia& s) {
    Tabla& tabla = catalogo_.tabla(s.tabla);
    Almacen almacen(catalogo_, tabla, false);
    almacen.reiniciar_contadores();
    almacen.insertar(s.valores);
    Resultado r;
    r.tipo = "insert";
    r.afectadas = 1;
    r.mensaje = "1 fila insertada en " + tabla.nombre;
    PasoPlan paso{"insertar", {{"estructura", almacen.estructura()}, {"paginas_leidas", texto(almacen.paginas_leidas())},
                               {"paginas_escritas", texto(almacen.paginas_escritas())}, {"indices_actualizados", texto(tabla.indices.size())}}};
    paso.nodo = "Insert";
    paso.relacion = tabla.nombre;
    paso.columna = tabla.columnas[tabla.pk].nombre;
    paso.filas_reales = 1;
    paso.paginas_leidas = static_cast<long long>(almacen.paginas_leidas());
    paso.paginas_escritas = static_cast<long long>(almacen.paginas_escritas());
    r.plan.push_back(paso);
    return r;
}

Resultado Ejecutor::eliminar(const Sentencia& s) {
    Tabla& tabla = catalogo_.tabla(s.tabla);
    Almacen almacen(catalogo_, tabla, false);
    Resultado r;
    r.tipo = "delete";

    Acceso acceso = acceder(almacen, tabla, s.condiciones);
    r.plan.push_back(acceso.paso);
    filtrar(acceso, tabla, r.plan);

    almacen.reiniciar_contadores();
    for (const FilaFisica& f : acceso.filas) almacen.eliminar(f);
    r.afectadas = acceso.filas.size();
    r.mensaje = texto(r.afectadas) + " filas eliminadas de " + tabla.nombre;
    PasoPlan paso{"eliminar", {{"estructura", almacen.estructura()}, {"filas", texto(r.afectadas)},
                               {"paginas_escritas", texto(almacen.paginas_escritas())}, {"modo", "lazy: se marcan tumbas"}}};
    paso.nodo = "Delete";
    paso.relacion = tabla.nombre;
    paso.filas_reales = static_cast<long long>(r.afectadas);
    paso.paginas_escritas = static_cast<long long>(almacen.paginas_escritas());
    r.plan.push_back(paso);
    ordenar_como_explain(r.plan);
    return r;
}

Resultado Ejecutor::seleccionar(const Sentencia& s) {
    const Tabla& tabla = catalogo_.tabla(s.tabla);
    Almacen almacen(catalogo_, tabla, false);
    Resultado r;
    r.tipo = "select";

    Acceso acceso = acceder(almacen, tabla, s.condiciones);
    r.plan.push_back(acceso.paso);
    filtrar(acceso, tabla, r.plan);

    std::vector<Fila> filas;
    filas.reserve(acceso.filas.size());
    for (FilaFisica& f : acceso.filas) filas.push_back(std::move(f.fila));

    std::vector<ItemSelect> items = s.items;
    if (s.todas_las_columnas) {
        for (const Columna& c : tabla.columnas) items.push_back({c.nombre, ""});
    }
    bool hay_agregados = false;
    for (const ItemSelect& item : items) {
        if (!item.agregado.empty()) hay_agregados = true;
        if (!item.columna.empty() && tabla.posicion_columna(item.columna) < 0) throw std::runtime_error("la columna " + item.columna + " no existe en " + tabla.nombre);
    }

    PlanTrace traza;
    if (hay_agregados || !s.group_by.empty()) {
        const auto inicio_agrupacion = Reloj::now();
        // GROUP BY con external hashing: una pasada por cada agregado sobre columna
        const int gcol = s.group_by.empty() ? -1 : tabla.posicion_columna(s.group_by);
        if (!s.group_by.empty() && gcol < 0) throw std::runtime_error("la columna " + s.group_by + " no existe");
        for (const ItemSelect& item : items) {
            if (item.agregado.empty() && (gcol < 0 || minusculas(item.columna) != minusculas(s.group_by))) {
                throw std::runtime_error("la columna " + item.columna + " debe estar en GROUP BY o dentro de un agregado");
            }
            if (!item.agregado.empty() && !item.columna.empty() && tabla.columnas[tabla.posicion_columna(item.columna)].tipo != TipoColumna::INT) {
                throw std::runtime_error(item.agregado + " solo se aplica a columnas INT");
            }
        }
        auto clave_grupo = [&](const Fila& f) { return gcol < 0 ? std::string() : f[gcol].a_texto(); };
        std::map<std::string, Valor> valor_grupo;
        for (const Fila& f : filas) valor_grupo.emplace(clave_grupo(f), gcol < 0 ? Valor::de_texto("") : f[gcol]);

        std::vector<std::unordered_map<std::string, AggregateResult>> resultados;
        for (const ItemSelect& item : items) {
            if (item.agregado.empty()) { resultados.emplace_back(); continue; }
            const int col = item.columna.empty() ? -1 : tabla.posicion_columna(item.columna);
            ExternalHashAggregate<Fila, std::string> agregador(10, 100, &traza);
            resultados.push_back(agregador.agrupar(filas, clave_grupo,
                                                   [&](const Fila& f) { return col < 0 ? 0.0 : static_cast<double>(f[col].entero); },
                                                   {AggregateOp::COUNT}));
        }
        for (const ItemSelect& item : items) r.columnas.push_back(item.etiqueta());
        for (const auto& [clave, valor] : valor_grupo) {
            Fila fila;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const ItemSelect& item = items[i];
                if (item.agregado.empty()) { fila.push_back(valor); continue; }
                const AggregateResult& a = resultados[i].at(clave);
                if (item.agregado == "COUNT") fila.push_back(Valor::de_entero(static_cast<long long>(a.count)));
                else if (item.agregado == "SUM") fila.push_back(Valor::de_entero(static_cast<long long>(a.sum)));
                else if (item.agregado == "MIN") fila.push_back(Valor::de_entero(static_cast<long long>(a.minimum)));
                else if (item.agregado == "MAX") fila.push_back(Valor::de_entero(static_cast<long long>(a.maximum)));
                else {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.2f", a.average);
                    fila.push_back(Valor::de_texto(buf));
                }
            }
            r.filas.push_back(std::move(fila));
        }
        if (gcol < 0 && r.filas.empty()) {
            Fila fila;
            for (const ItemSelect& item : items) fila.push_back(item.agregado == "COUNT" ? Valor::de_entero(0) : Valor::de_texto(""));
            r.filas.push_back(fila);
        }
        PasoPlan paso{"agrupacion", {{"algoritmo", "external_hash_aggregate"}, {"columna", s.group_by.empty() ? "(todo)" : s.group_by}, {"grupos", texto(r.filas.size())}}};
        if (!traza.events.empty()) {
            for (const auto& [k, v] : traza.last("external_hash_aggregate").details) {
                if (k == "partitions") paso.detalles.push_back({"particiones", v});
                if (k == "peak_groups_in_memory") paso.detalles.push_back({"grupos_en_memoria", v});
            }
        }
        paso.nodo = "HashAggregate";
        paso.relacion = tabla.nombre;
        paso.columna = s.group_by;
        paso.filas_reales = static_cast<long long>(r.filas.size());
        paso.tiempo_ms = std::chrono::duration<double, std::milli>(Reloj::now() - inicio_agrupacion).count();
        r.plan.push_back(paso);
    } else {
        for (const ItemSelect& item : items) r.columnas.push_back(item.columna);
    }

    // ORDER BY con external merge sort (k-way)
    if (!s.order_by.empty()) {
        int col = -1;
        std::vector<Fila>& objetivo = hay_agregados || !s.group_by.empty() ? r.filas : filas;
        if (hay_agregados || !s.group_by.empty()) {
            for (std::size_t i = 0; i < r.columnas.size(); ++i) if (minusculas(r.columnas[i]) == minusculas(s.order_by)) col = static_cast<int>(i);
        } else {
            col = tabla.posicion_columna(s.order_by);
        }
        if (col < 0) throw std::runtime_error("no se puede ordenar por " + s.order_by);
        const auto inicio_orden = Reloj::now();
        ExternalMergeSort<Fila, Valor> ordenador(10, 100, 0, &traza);
        objetivo = ordenador.ordenar(std::move(objetivo), [col](const Fila& f) { return f[col]; }, s.descendente);
        PasoPlan paso{"ordenamiento", {{"algoritmo", "external_merge_sort"}, {"columna", s.order_by}, {"orden", s.descendente ? "DESC" : "ASC"}}};
        for (const auto& [k, v] : traza.last("external_merge_sort").details) if (k == "initial_runs" || k == "merge_passes" || k == "k") paso.detalles.push_back({k, v});
        paso.nodo = "Sort";
        paso.relacion = tabla.nombre;
        paso.columna = s.order_by;
        paso.condicion = s.order_by + (s.descendente ? " DESC" : " ASC");
        paso.filas_reales = static_cast<long long>(objetivo.size());
        paso.tiempo_ms = std::chrono::duration<double, std::milli>(Reloj::now() - inicio_orden).count();
        r.plan.push_back(paso);
    }

    // proyección
    if (!(hay_agregados || !s.group_by.empty())) {
        std::vector<int> posiciones;
        for (const ItemSelect& item : items) posiciones.push_back(tabla.posicion_columna(item.columna));
        for (Fila& f : filas) {
            Fila salida;
            for (int p : posiciones) salida.push_back(f[p]);
            r.filas.push_back(std::move(salida));
        }
        if (!s.todas_las_columnas) {
            std::string nombres;
            for (const ItemSelect& item : items) nombres += (nombres.empty() ? "" : ", ") + item.columna;
            PasoPlan paso{"proyeccion", {{"columnas", texto(posiciones.size())}, {"lista", nombres}}};
            paso.nodo = "Projection";
            paso.relacion = tabla.nombre;
            paso.columna = nombres;
            paso.filas_reales = static_cast<long long>(r.filas.size());
            r.plan.push_back(paso);
        }
    }

    if (s.limite >= 0 && r.filas.size() > static_cast<std::size_t>(s.limite)) {
        r.filas.resize(static_cast<std::size_t>(s.limite));
        PasoPlan paso{"limite", {{"filas", texto(r.filas.size())}}};
        paso.nodo = "Limit";
        paso.relacion = tabla.nombre;
        paso.filas_reales = static_cast<long long>(r.filas.size());
        paso.filas_estimadas = s.limite;
        r.plan.push_back(paso);
    }
    r.afectadas = r.filas.size();
    r.mensaje = texto(r.filas.size()) + " filas";
    ordenar_como_explain(r.plan);
    return r;
}

Resultado Ejecutor::mostrar_tablas() {
    Resultado r;
    r.tipo = "show_tables";
    r.columnas = {"tabla", "organizacion", "clave", "columnas", "registros", "paginas", "bytes", "indices", "detalle"};
    for (const auto& [clave, tabla] : catalogo_.tablas()) {
        Almacen almacen(catalogo_, tabla, false);
        // el B+ agrupado no es un indice secundario: es la organizacion de la tabla, sobre su clave primaria
        std::string indices = tabla.organizacion == Organizacion::BPLUS ? "PRIMARY(" + tabla.columnas[tabla.pk].nombre + ") B+ agrupado" : "";
        for (const Indice& i : tabla.indices) indices += (indices.empty() ? "" : ", ") + i.nombre + "(" + i.columna + ")";
        r.filas.push_back({Valor::de_texto(tabla.nombre), Valor::de_texto(nombre_organizacion(tabla.organizacion)),
                           Valor::de_texto(tabla.columnas[tabla.pk].nombre), Valor::de_entero(static_cast<long long>(tabla.columnas.size())),
                           Valor::de_entero(static_cast<long long>(almacen.registros())), Valor::de_entero(static_cast<long long>(almacen.paginas())),
                           Valor::de_entero(static_cast<long long>(almacen.bytes())), Valor::de_texto(indices), Valor::de_texto(almacen.detalle())});
    }
    r.afectadas = r.filas.size();
    r.mensaje = texto(r.filas.size()) + " tablas";
    r.plan.push_back({"leer_catalogo", {{"directorio", catalogo_.dir()}, {"tablas", texto(r.filas.size())}, {"nota", "solo metadatos; no lee paginas de datos"}}});
    return r;
}

Resultado Ejecutor::describir(const Sentencia& s) {
    const Tabla& tabla = catalogo_.tabla(s.tabla);
    Resultado r;
    r.tipo = "describe";
    r.columnas = {"columna", "tipo", "clave", "indice"};
    for (std::size_t i = 0; i < tabla.columnas.size(); ++i) {
        const Columna& c = tabla.columnas[i];
        const Indice* indice = tabla.indice_sobre(c.nombre);
        r.filas.push_back({Valor::de_texto(c.nombre),
                           Valor::de_texto(c.tipo == TipoColumna::INT ? "INT" : "VARCHAR(" + texto(c.tam) + ")"),
                           Valor::de_texto(i == tabla.pk ? "PK" : ""),
                           Valor::de_texto(indice ? indice->nombre + " (" + nombre_tipo_indice(*indice) + ")"
                                           : i == tabla.pk && tabla.organizacion == Organizacion::BPLUS ? "PRIMARY (B+ agrupado)"
                                           : "")});
    }
    r.afectadas = r.filas.size();
    r.mensaje = tabla.nombre + ": " + nombre_organizacion(tabla.organizacion) + " en " + tabla.archivo;
    r.plan.push_back({"leer_catalogo", {{"tabla", tabla.nombre}, {"organizacion", nombre_organizacion(tabla.organizacion)}, {"columnas", texto(tabla.columnas.size())}, {"indices_secundarios", texto(tabla.indices.size())}, {"nota", "solo metadatos; no lee paginas de datos"}}});
    return r;
}

}  // namespace sql
}  // namespace motor
