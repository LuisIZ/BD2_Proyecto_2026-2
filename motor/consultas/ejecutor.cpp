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
    RecordId rid;
};

struct IndiceAbierto {
    const Indice* meta = nullptr;
    std::unique_ptr<BPlusNoAgrupado> bplus;
    std::unique_ptr<HashExtensibleDisco> hash;

    bool es_hash() const { return hash != nullptr; }
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
    std::string estructura() const { return hash ? "hash_extensible" : "bplus_no_agrupado"; }
};

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

std::string describir(const Condicion& c) {
    std::string s = c.columna + " " + c.op + " " + (c.valor.es_entero ? c.valor.a_texto() : "'" + c.valor.texto + "'");
    if (c.op == "BETWEEN") s += " AND " + c.hasta.a_texto();
    return s;
}

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
        for (auto& abierto : indices_) abierto.sincronizar();
    }

    std::string estructura() const {
        switch (tabla_.organizacion) {
            case Organizacion::HEAP: return "heap";
            case Organizacion::SEQUENTIAL: return "secuencial";
            case Organizacion::BPLUS: return "bplus_agrupado";
        }
        return "";
    }

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
        for (auto& abierto : indices_) if (abierto.meta == i) return &abierto;
        return nullptr;
    }

    std::vector<FilaFisica> rango_indice(const Indice* i, long long desde, long long hasta) {
        std::vector<FilaFisica> salida;
        IndiceAbierto* arbol = indice(i);
        const std::vector<long long> posiciones = desde == hasta ? arbol->buscar(a_int(desde))
                                                                  : arbol->buscar_rango(a_int(desde), a_int(hasta));
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

    void insertar(const Fila& fila) {
        validar_fila(tabla_, fila);
        const int clave = clave_de(tabla_, fila);
        if (heap_) {
            const Indice* ipk = tabla_.indice_sobre(tabla_.columnas[tabla_.pk].nombre);
            const bool repetida = ipk ? !indice(ipk)->buscar(clave).empty() : !buscar_pk(clave).empty();
            if (repetida) throw std::runtime_error("clave primaria repetida: " + std::to_string(clave));
            const std::vector<std::byte> bytes = codificar_registro({clave, empaquetar_variable(tabla_, fila)});
            const RecordId rid = heap_->insertar_bytes(bytes.data(), static_cast<std::uint16_t>(bytes.size()));
            for (auto& abierto : indices_) {
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
            for (auto& abierto : indices_) {
                abierto.eliminar_entrada(static_cast<int>(f.fila[tabla_.posicion_columna(abierto.meta->columna)].entero), pos_de(f.rid));
            }
        } else if (seq_) {
            seq_->eliminar(clave);
        } else {
            arbol_->eliminar(clave);
        }
    }

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
        for (const Fila& fila : filas) {
            const Registro r{clave_de(tabla_, fila), empaquetar_variable(tabla_, fila)};
            if (heap_) {
                const std::vector<std::byte> bytes = codificar_registro(r);
                heap_->insertar_bytes(bytes.data(), static_cast<std::uint16_t>(bytes.size()));
            } else if (!seq_->insertar(r)) {
                throw std::runtime_error("el registro no cabe en una pagina");
            }
        }
    }

    void construir_indice(const Indice& indice, IndiceAbierto& arbol) {
        const int col = tabla_.posicion_columna(indice.columna);
        heap_->recorrer([&](const RecordId& rid, const std::byte* datos, std::uint16_t largo) {
            Registro r;
            if (decodificar_registro(datos, largo, r)) {
                const Fila fila = desempaquetar_variable(tabla_, r.clave, r.valor);
                arbol.insertar(static_cast<int>(fila[col].entero), pos_de(rid));
            }
            return true;
        });
        arbol.sincronizar();
    }

    void reiniciar_contadores() {
        if (heap_) heap_->reiniciar_contadores();
        if (seq_) seq_->reiniciar_contadores();
        lect_arbol_ = arbol_ ? arbol_->paginas_leidas() : 0;
        escr_arbol_ = arbol_ ? arbol_->paginas_escritas() : 0;
        lect_idx_.clear();
        for (auto& abierto : indices_) lect_idx_.push_back(abierto.paginas_leidas());
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

std::string calificar(const std::string& calificador, const std::string& columna) {
    return calificador.empty() ? columna : calificador + "." + columna;
}

struct ColumnaEsquema {
    std::string fuente;
    std::string nombre;
    TipoColumna tipo;
};

class EsquemaResultado {
public:
    void agregar_tabla(const Tabla& t) {
        fuentes_.push_back(t.nombre);
        for (const Columna& c : t.columnas) cols_.push_back({t.nombre, c.nombre, c.tipo});
    }

    int posicion(const std::string& calificador, const std::string& columna) const {
        if (!calificador.empty() && !conoce(calificador)) {
            throw std::runtime_error("no hay ninguna tabla llamada " + calificador + " en la consulta");
        }
        int encontrada = -1;
        int repetidas = 0;
        for (std::size_t i = 0; i < cols_.size(); ++i) {
            if (minusculas(cols_[i].nombre) != minusculas(columna)) continue;
            if (!calificador.empty() && minusculas(cols_[i].fuente) != minusculas(calificador)) continue;
            if (encontrada < 0) encontrada = static_cast<int>(i);
            ++repetidas;
        }
        if (repetidas > 1) throw std::runtime_error("la columna " + columna + " es ambigua: " + sugerencia(columna));
        return encontrada;
    }

    const ColumnaEsquema& en(std::size_t i) const { return cols_[i]; }
    bool multiple() const { return fuentes_.size() > 1; }
    const std::vector<ColumnaEsquema>& columnas() const { return cols_; }

    std::string nombre_salida(std::size_t i) const {
        return multiple() ? cols_[i].fuente + "." + cols_[i].nombre : cols_[i].nombre;
    }

private:
    bool conoce(const std::string& fuente) const {
        for (const std::string& f : fuentes_) if (minusculas(f) == minusculas(fuente)) return true;
        return false;
    }
    std::string sugerencia(const std::string& columna) const {
        std::string s;
        for (const ColumnaEsquema& c : cols_) {
            if (minusculas(c.nombre) == minusculas(columna)) s += (s.empty() ? "use " : " o ") + c.fuente + "." + c.nombre;
        }
        return s;
    }

    std::vector<ColumnaEsquema> cols_;
    std::vector<std::string> fuentes_;
};

struct Acceso {
    std::vector<FilaFisica> filas;
    std::vector<Condicion> restantes;
    PasoPlan paso;
};

Acceso acceder(Almacen& almacen, const Tabla& tabla, const std::vector<Condicion>& condiciones) {
    Acceso acceso;
    const std::string pk = tabla.columnas[tabla.pk].nombre;
    int usada = -1;
    bool hash_descartado = false;

    for (std::size_t i = 0; i < condiciones.size() && usada < 0; ++i) {
        const Condicion& c = condiciones[i];
        const int col = tabla.posicion_columna(c.columna);
        if (col < 0) throw std::runtime_error("la columna " + c.columna + " no existe en " + tabla.nombre);
        if (tabla.columnas[col].tipo == TipoColumna::INT && !c.valor.es_entero) throw std::runtime_error("la columna " + c.columna + " es INT");
        if (tabla.columnas[col].tipo == TipoColumna::VARCHAR && c.valor.es_entero) throw std::runtime_error("la columna " + c.columna + " es VARCHAR");
        long long desde, hasta;
        if (!rango_de(c, desde, hasta)) continue;

        const bool es_pk = minusculas(c.columna) == minusculas(pk);
        const Indice* indice = almacen.indice(tabla.indice_sobre(c.columna)) ? tabla.indice_sobre(c.columna) : nullptr;
        const bool hash = indice && almacen.indice(indice)->es_hash();
        if (hash && desde != hasta) {
            indice = nullptr;
            hash_descartado = true;
        }

        if (indice) {
            almacen.reiniciar_contadores();
            acceso.filas = almacen.rango_indice(indice, desde, hasta);
            acceso.paso.operacion = desde == hasta ? "busqueda_por_indice" : "rango_por_indice";
            acceso.paso.detalles = {{"indice", indice->nombre}, {"estructura", almacen.indice(indice)->estructura()},
                                    {"columna", c.columna},
                                    {"condicion", describir(c)}, {"paginas_indice", texto(almacen.paginas_leidas_indice(indice))},
                                    {"paginas_heap", texto(almacen.paginas_leidas())}, {"filas", texto(acceso.filas.size())}};
            usada = static_cast<int>(i);
        } else if (es_pk && (desde == hasta || almacen.sabe_rango_pk())) {
            almacen.reiniciar_contadores();
            acceso.filas = desde == hasta ? almacen.buscar_pk(desde) : almacen.rango_pk(desde, hasta);
            const bool scan = tabla.organizacion == Organizacion::HEAP;
            acceso.paso.operacion = scan ? "scan_completo" : (desde == hasta ? "busqueda_por_clave" : "rango_por_clave");
            acceso.paso.detalles = {{"estructura", almacen.estructura()}, {"columna", c.columna}, {"condicion", describir(c)},
                                    {"paginas_leidas", texto(almacen.paginas_leidas())}, {"filas", texto(acceso.filas.size())}};
            if (scan) acceso.paso.detalles.push_back({"nota", "heap sin indice sobre la clave: recorrido completo"});
            usada = static_cast<int>(i);
        }
    }

    if (usada < 0) {
        almacen.reiniciar_contadores();
        acceso.filas = almacen.escanear();
        acceso.paso.operacion = "scan_completo";
        acceso.paso.detalles = {{"estructura", almacen.estructura()}, {"paginas_leidas", texto(almacen.paginas_leidas())},
                                {"filas", texto(acceso.filas.size())}};
        if (hash_descartado) {
            acceso.paso.detalles.push_back({"nota", "el indice hash no resuelve rangos: recorrido completo"});
        }
    }
    for (std::size_t i = 0; i < condiciones.size(); ++i) {
        if (static_cast<int>(i) != usada) acceso.restantes.push_back(condiciones[i]);
    }
    return acceso;
}

void filtrar(Acceso& acceso, const Tabla& tabla, std::vector<PasoPlan>& plan) {
    if (acceso.restantes.empty()) return;
    std::vector<FilaFisica> salida;
    std::string descripcion;
    for (const Condicion& c : acceso.restantes) {
        if (tabla.posicion_columna(c.columna) < 0) throw std::runtime_error("la columna " + c.columna + " no existe en " + tabla.nombre);
        descripcion += (descripcion.empty() ? "" : " AND ") + describir(c);
    }
    for (FilaFisica& f : acceso.filas) {
        bool ok = true;
        for (const Condicion& c : acceso.restantes) {
            if (!cumple(f.fila[tabla.posicion_columna(c.columna)], c)) { ok = false; break; }
        }
        if (ok) salida.push_back(std::move(f));
    }
    plan.push_back({"filtro", {{"condicion", descripcion}, {"entrada", texto(acceso.filas.size())}, {"salida", texto(salida.size())}}});
    acceso.filas = std::move(salida);
}

void repartir_condiciones(const std::vector<Condicion>& todas, const Tabla& ti, const Tabla& td,
                          std::vector<Condicion>& izq, std::vector<Condicion>& der) {
    for (const Condicion& c : todas) {
        bool a_izquierda;
        if (!c.calificador.empty()) {
            const bool es_i = minusculas(c.calificador) == minusculas(ti.nombre);
            const bool es_d = minusculas(c.calificador) == minusculas(td.nombre);
            if (!es_i && !es_d) throw std::runtime_error("no hay ninguna tabla llamada " + c.calificador + " en la consulta");
            a_izquierda = es_i;
            const Tabla& duena = a_izquierda ? ti : td;
            if (duena.posicion_columna(c.columna) < 0) throw std::runtime_error("la columna " + c.nombre_completo() + " no existe");
        } else {
            const bool en_i = ti.posicion_columna(c.columna) >= 0;
            const bool en_d = td.posicion_columna(c.columna) >= 0;
            if (en_i && en_d) {
                throw std::runtime_error("la columna " + c.columna + " es ambigua: use " + ti.nombre + "." + c.columna +
                                         " o " + td.nombre + "." + c.columna);
            }
            if (!en_i && !en_d) throw std::runtime_error("la columna " + c.columna + " no existe");
            a_izquierda = en_i;
        }
        Condicion copia = c;
        copia.calificador.clear();
        (a_izquierda ? izq : der).push_back(copia);
    }
}

std::vector<Fila> ejecutar_join(const Catalogo& catalogo, const Sentencia& s, EsquemaResultado& esquema,
                                std::vector<PasoPlan>& plan, PlanTrace& traza) {
    if (s.joins.size() > 1) throw std::runtime_error("por ahora solo se admite un JOIN por consulta");
    const JoinSpec& j = s.joins[0];

    const Tabla& ti = catalogo.tabla(s.tabla);
    const Tabla& td = catalogo.tabla(j.tabla_derecha);
    if (minusculas(ti.nombre) == minusculas(td.nombre)) {
        throw std::runtime_error("unir una tabla consigo misma todavia no esta soportado");
    }

    auto lado = [&](const std::string& cal, const std::string& col) -> int {
        if (!cal.empty()) {
            if (minusculas(cal) == minusculas(ti.nombre)) return 1;
            if (minusculas(cal) == minusculas(td.nombre)) return 2;
            throw std::runtime_error("no hay ninguna tabla llamada " + cal + " en la consulta");
        }
        const bool en_i = ti.posicion_columna(col) >= 0;
        const bool en_d = td.posicion_columna(col) >= 0;
        if (en_i && en_d) return 0;
        if (en_i) return 1;
        if (en_d) return 2;
        throw std::runtime_error("la columna " + col + " no existe en " + ti.nombre + " ni en " + td.nombre);
    };
    int la = lado(j.izq_calificador, j.izq_columna);
    int lb = lado(j.der_calificador, j.der_columna);
    if (la == 0 && lb == 0) { la = 1; lb = 2; }
    else if (la == 0) la = lb == 1 ? 2 : 1;
    else if (lb == 0) lb = la == 1 ? 2 : 1;
    if (la == lb) throw std::runtime_error("el ON debe comparar una columna de " + ti.nombre + " con una de " + td.nombre);

    const std::string col_i = la == 1 ? j.izq_columna : j.der_columna;
    const std::string col_d = la == 1 ? j.der_columna : j.izq_columna;
    const int ci = ti.posicion_columna(col_i);
    const int cd = td.posicion_columna(col_d);
    if (ci < 0) throw std::runtime_error("la columna " + ti.nombre + "." + col_i + " no existe");
    if (cd < 0) throw std::runtime_error("la columna " + td.nombre + "." + col_d + " no existe");
    if (ti.columnas[ci].tipo != td.columnas[cd].tipo) {
        throw std::runtime_error("no se puede unir " + ti.nombre + "." + col_i + " con " + td.nombre + "." + col_d +
                                 ": son de tipos distintos");
    }

    std::vector<Condicion> cond_i, cond_d;
    repartir_condiciones(s.condiciones, ti, td, cond_i, cond_d);

    Almacen ai(catalogo, ti, false);
    Acceso acc_i = acceder(ai, ti, cond_i);
    acc_i.paso.detalles.insert(acc_i.paso.detalles.begin(), {"tabla", ti.nombre});
    plan.push_back(acc_i.paso);
    std::size_t pasos = plan.size();
    filtrar(acc_i, ti, plan);
    if (plan.size() > pasos) plan.back().detalles.insert(plan.back().detalles.begin(), {"tabla", ti.nombre});
    std::vector<Fila> filas_i;
    filas_i.reserve(acc_i.filas.size());
    for (FilaFisica& f : acc_i.filas) filas_i.push_back(std::move(f.fila));
    const std::size_t pag_i = ai.paginas_leidas();

    Almacen ad(catalogo, td, false);
    const Indice* idx_d = td.indice_sobre(col_d);
    if (idx_d && !ad.indice(idx_d)) idx_d = nullptr;
    const bool pk_util = static_cast<std::size_t>(cd) == td.pk && td.organizacion != Organizacion::HEAP;
    const bool hay_indice = cond_d.empty() && td.columnas[cd].tipo == TipoColumna::INT && (idx_d != nullptr || pk_util);

    JoinPlanner planificador(&traza);
    std::size_t filas_d_usadas = ad.registros();
    const std::string algoritmo = planificador.choose(filas_i.size(), filas_d_usadas, hay_indice);

    std::vector<std::pair<Fila, Fila>> pares;
    std::size_t pag_d = 0;
    std::size_t entradas_hash = 0;

    if (algoritmo == "index_nested_loop_join") {
        ad.reiniciar_contadores();
        const std::function<long long(const Fila&)> clave_i = [ci](const Fila& f) { return f[ci].entero; };
        const std::function<std::vector<Fila>(const long long&)> sonda = [&](const long long& k) {
            std::vector<FilaFisica> fisicas = idx_d ? ad.rango_indice(idx_d, k, k) : ad.buscar_pk(k);
            std::vector<Fila> salida;
            salida.reserve(fisicas.size());
            for (FilaFisica& f : fisicas) salida.push_back(std::move(f.fila));
            return salida;
        };
        pares = index_nested_loop_join<Fila, Fila, long long>(filas_i, sonda, clave_i);
        pag_d = ad.paginas_leidas() + (idx_d ? ad.paginas_leidas_indice(idx_d) : 0);
    } else {
        Acceso acc_d = acceder(ad, td, cond_d);
        acc_d.paso.detalles.insert(acc_d.paso.detalles.begin(), {"tabla", td.nombre});
        plan.push_back(acc_d.paso);
        pasos = plan.size();
        filtrar(acc_d, td, plan);
        if (plan.size() > pasos) plan.back().detalles.insert(plan.back().detalles.begin(), {"tabla", td.nombre});
        std::vector<Fila> filas_d;
        filas_d.reserve(acc_d.filas.size());
        for (FilaFisica& f : acc_d.filas) filas_d.push_back(std::move(f.fila));
        pag_d = ad.paginas_leidas();
        filas_d_usadas = filas_d.size();
        entradas_hash = filas_d.size();
        const std::function<std::string(const Fila&)> clave_i = [ci](const Fila& f) { return f[ci].a_texto(); };
        const std::function<std::string(const Fila&)> clave_d = [cd](const Fila& f) { return f[cd].a_texto(); };
        pares = hash_join<Fila, Fila, std::string>(filas_i, filas_d, clave_i, clave_d);
    }

    esquema.agregar_tabla(ti);
    esquema.agregar_tabla(td);

    std::vector<Fila> filas;
    filas.reserve(pares.size());
    for (auto& par : pares) {
        Fila f = std::move(par.first);
        f.insert(f.end(), std::make_move_iterator(par.second.begin()), std::make_move_iterator(par.second.end()));
        filas.push_back(std::move(f));
    }

    PasoPlan paso{"join", {{"tipo", j.tipo},
                           {"algoritmo", algoritmo},
                           {"condicion", ti.nombre + "." + col_i + " = " + td.nombre + "." + col_d},
                           {"filas_izquierda", texto(filas_i.size())},
                           {"filas_derecha", texto(filas_d_usadas)},
                           {"paginas_izquierda", texto(pag_i)},
                           {"paginas_derecha", texto(pag_d)},
                           {"indice_disponible", hay_indice ? "si" : "no"},
                           {"filas_resultado", texto(filas.size())}}};
    paso.detalles.push_back(algoritmo == "index_nested_loop_join"
                                ? std::make_pair(std::string("sondas"), texto(filas_i.size()))
                                : std::make_pair(std::string("entradas_tabla_hash"), texto(entradas_hash)));
    plan.push_back(paso);
    return filas;
}

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

}

Resultado Ejecutor::ejecutar(const std::string& sql) { return ejecutar(parsear(sql)); }

Resultado Ejecutor::ejecutar(const Sentencia& s) {
    const auto inicio = Reloj::now();
    Resultado r;
    switch (s.tipo) {
        case TipoSentencia::CREATE_TABLE: r = crear_tabla(s); break;
        case TipoSentencia::CREATE_TABLE_FROM_FILE: r = crear_tabla_desde_csv(s); break;
        case TipoSentencia::CREATE_INDEX: r = crear_indice(s); break;
        case TipoSentencia::DROP_TABLE: r = borrar_tabla(s); break;
        case TipoSentencia::INSERT: r = insertar(s); break;
        case TipoSentencia::DELETE_FROM: r = eliminar(s); break;
        case TipoSentencia::SELECT: r = seleccionar(s); break;
        case TipoSentencia::SHOW_TABLES: r = mostrar_tablas(); break;
        case TipoSentencia::DESCRIBE: r = describir(s); break;
    }
    r.tiempo_ms = std::chrono::duration<double, std::milli>(Reloj::now() - inicio).count();
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

Resultado Ejecutor::crear_tabla_desde_csv(const Sentencia& s) {
    if (catalogo_.existe(s.tabla)) throw std::runtime_error("la tabla " + s.tabla + " ya existe");
    const auto inicio_lectura = Reloj::now();
    std::vector<std::vector<std::string>> csv = leer_csv(s.archivo_csv);
    if (csv.size() < 2) throw std::runtime_error("el CSV no tiene filas de datos");
    const std::size_t ncol = csv[0].size();

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
                      {{"estructura", nombre_organizacion(tabla.organizacion)}, {"paginas_escritas", texto(escritas)}, {"tiempo_ms", std::to_string(t_carga)}}});
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

    const bool hash = s.indice_tipo == "HASH";
    Indice indice{s.indice_nombre, tabla.columnas[col].nombre, hash ? "HASH" : "BPLUS",
                  catalogo_.ruta_datos(tabla.nombre + "__" + minusculas(s.indice_nombre), hash ? ".hash" : ".bplus")};
    long entradas = 0;
    long disco = 0;
    std::string detalle;
    {
        Almacen almacen(catalogo_, tabla, false);
        IndiceAbierto arbol;
        arbol.meta = &indice;
        if (hash) arbol.hash = std::make_unique<HashExtensibleDisco>(indice.archivo, true);
        else arbol.bplus = std::make_unique<BPlusNoAgrupado>(indice.archivo, true);
        almacen.construir_indice(indice, arbol);
        entradas = arbol.num_entradas();
        disco = arbol.tamano_en_disco();
        if (hash) {
            detalle = "profundidad global " + std::to_string(arbol.hash->profundidad_global()) + ", " +
                      std::to_string(arbol.hash->paginas_bucket()) + " paginas de bucket";
        } else {
            detalle = "altura " + std::to_string(arbol.bplus->altura());
        }
    }
    tabla.indices.push_back(indice);
    catalogo_.guardar();

    Resultado r;
    r.tipo = "create_index";
    r.afectadas = static_cast<std::size_t>(entradas);
    r.mensaje = "indice " + indice.nombre + (hash ? " (hash extensible)" : " (B+ no agrupado)") + " creado sobre " +
                tabla.nombre + "." + indice.columna + " con " + std::to_string(entradas) + " entradas";
    r.plan.push_back({"construir_indice",
                      {{"estructura", hash ? "hash_extensible" : "bplus_no_agrupado"},
                       {"entradas", std::to_string(entradas)},
                       {"bytes", std::to_string(disco)},
                       {"detalle", detalle},
                       {"archivo", indice.archivo}}});
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
    r.plan.push_back({"insertar", {{"estructura", almacen.estructura()}, {"paginas_leidas", texto(almacen.paginas_leidas())},
                                   {"paginas_escritas", texto(almacen.paginas_escritas())}, {"indices_actualizados", texto(tabla.indices.size())}}});
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
    r.plan.push_back({"eliminar", {{"estructura", almacen.estructura()}, {"filas", texto(r.afectadas)},
                                   {"paginas_escritas", texto(almacen.paginas_escritas())}, {"modo", "lazy: se marcan tumbas"}}});
    return r;
}

Resultado Ejecutor::seleccionar(const Sentencia& s) {
    Resultado r;
    r.tipo = "select";
    PlanTrace traza;
    EsquemaResultado esquema;
    std::vector<Fila> filas;

    if (s.joins.empty()) {
        const Tabla& tabla = catalogo_.tabla(s.tabla);
        Almacen almacen(catalogo_, tabla, false);
        Acceso acceso = acceder(almacen, tabla, s.condiciones);
        r.plan.push_back(acceso.paso);
        filtrar(acceso, tabla, r.plan);
        filas.reserve(acceso.filas.size());
        for (FilaFisica& f : acceso.filas) filas.push_back(std::move(f.fila));
        esquema.agregar_tabla(tabla);
    } else {
        filas = ejecutar_join(catalogo_, s, esquema, r.plan, traza);
    }

    std::vector<ItemSelect> items = s.items;
    if (s.todas_las_columnas) {
        for (const ColumnaEsquema& c : esquema.columnas()) {
            items.push_back({c.nombre, "", esquema.multiple() ? c.fuente : std::string()});
        }
    }
    bool hay_agregados = false;
    for (const ItemSelect& item : items) {
        if (!item.agregado.empty()) hay_agregados = true;
        if (!item.columna.empty() && esquema.posicion(item.calificador, item.columna) < 0) {
            throw std::runtime_error("la columna " + calificar(item.calificador, item.columna) + " no existe");
        }
    }

    if (hay_agregados || !s.group_by.empty()) {
        const int gcol = s.group_by.empty() ? -1 : esquema.posicion(s.group_by_calificador, s.group_by);
        if (!s.group_by.empty() && gcol < 0) throw std::runtime_error("la columna " + calificar(s.group_by_calificador, s.group_by) + " no existe");
        for (const ItemSelect& item : items) {
            if (item.agregado.empty() && (gcol < 0 || esquema.posicion(item.calificador, item.columna) != gcol)) {
                throw std::runtime_error("la columna " + calificar(item.calificador, item.columna) + " debe estar en GROUP BY o dentro de un agregado");
            }
            if (!item.agregado.empty() && !item.columna.empty() && esquema.en(esquema.posicion(item.calificador, item.columna)).tipo != TipoColumna::INT) {
                throw std::runtime_error(item.agregado + " solo se aplica a columnas INT");
            }
        }
        auto clave_grupo = [&](const Fila& f) { return gcol < 0 ? std::string() : f[gcol].a_texto(); };
        std::map<std::string, Valor> valor_grupo;
        for (const Fila& f : filas) valor_grupo.emplace(clave_grupo(f), gcol < 0 ? Valor::de_texto("") : f[gcol]);

        std::vector<std::unordered_map<std::string, AggregateResult>> resultados;
        for (const ItemSelect& item : items) {
            if (item.agregado.empty()) { resultados.emplace_back(); continue; }
            const int col = item.columna.empty() ? -1 : esquema.posicion(item.calificador, item.columna);
            ExternalHashAggregate<Fila, std::string> agregador(10, 100, &traza);
            resultados.push_back(agregador.agrupar(filas, clave_grupo,
                                                   [&](const Fila& f) { return col < 0 ? 0.0 : static_cast<double>(f[col].entero); },
                                                   {AggregateOp::COUNT}));
        }
        for (const ItemSelect& item : items) r.columnas.push_back(esquema.multiple() ? item.etiqueta_calificada() : item.etiqueta());
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
                // grupos vivos a la vez: con external hashing es el de una particion,
                // no el total, y esa es justamente la propiedad que se quiere mostrar
                if (k == "peak_groups_in_memory") paso.detalles.push_back({"grupos_en_memoria", v});
            }
        }
        r.plan.push_back(paso);
    } else {
        for (const ItemSelect& item : items) {
            r.columnas.push_back(esquema.multiple()
                                     ? esquema.nombre_salida(static_cast<std::size_t>(esquema.posicion(item.calificador, item.columna)))
                                     : item.columna);
        }
    }

    if (!s.order_by.empty()) {
        int col = -1;
        std::vector<Fila>& objetivo = hay_agregados || !s.group_by.empty() ? r.filas : filas;
        if (hay_agregados || !s.group_by.empty()) {
            const std::string buscado = minusculas(calificar(s.order_by_calificador, s.order_by));
            for (std::size_t i = 0; i < r.columnas.size(); ++i) if (minusculas(r.columnas[i]) == buscado) col = static_cast<int>(i);
            if (col < 0 && s.order_by_calificador.empty()) {
                const std::string sufijo = "." + minusculas(s.order_by);
                for (std::size_t i = 0; i < r.columnas.size(); ++i) {
                    const std::string c = minusculas(r.columnas[i]);
                    if (c.size() > sufijo.size() && c.compare(c.size() - sufijo.size(), sufijo.size(), sufijo) == 0) col = static_cast<int>(i);
                }
            }
        } else {
            col = esquema.posicion(s.order_by_calificador, s.order_by);
        }
        if (col < 0) throw std::runtime_error("no se puede ordenar por " + calificar(s.order_by_calificador, s.order_by));
        ExternalMergeSort<Fila, Valor> ordenador(10, 100, 0, &traza);
        objetivo = ordenador.ordenar(std::move(objetivo), [col](const Fila& f) { return f[col]; }, s.descendente);
        PasoPlan paso{"ordenamiento", {{"algoritmo", "external_merge_sort"}, {"columna", calificar(s.order_by_calificador, s.order_by)}, {"orden", s.descendente ? "DESC" : "ASC"}}};
        for (const auto& [k, v] : traza.last("external_merge_sort").details) if (k == "initial_runs" || k == "merge_passes" || k == "k") paso.detalles.push_back({k, v});
        r.plan.push_back(paso);
    }

    if (!(hay_agregados || !s.group_by.empty())) {
        std::vector<int> posiciones;
        for (const ItemSelect& item : items) posiciones.push_back(esquema.posicion(item.calificador, item.columna));
        for (Fila& f : filas) {
            Fila salida;
            for (int p : posiciones) salida.push_back(f[p]);
            r.filas.push_back(std::move(salida));
        }
        if (!s.todas_las_columnas) r.plan.push_back({"proyeccion", {{"columnas", texto(posiciones.size())}}});
    }

    if (s.limite >= 0 && r.filas.size() > static_cast<std::size_t>(s.limite)) {
        r.filas.resize(static_cast<std::size_t>(s.limite));
        r.plan.push_back({"limite", {{"filas", texto(r.filas.size())}}});
    }
    r.afectadas = r.filas.size();
    r.mensaje = texto(r.filas.size()) + " filas";
    return r;
}

Resultado Ejecutor::mostrar_tablas() {
    Resultado r;
    r.tipo = "show_tables";
    r.columnas = {"tabla", "organizacion", "clave", "columnas", "registros", "paginas", "bytes", "indices", "detalle"};
    for (const auto& [clave, tabla] : catalogo_.tablas()) {
        Almacen almacen(catalogo_, tabla, false);
        std::string indices;
        for (const Indice& i : tabla.indices) {
            indices += (indices.empty() ? "" : ", ") + i.nombre + "(" + i.columna + ") " + (i.tipo == "HASH" ? "hash" : "B+");
        }
        r.filas.push_back({Valor::de_texto(tabla.nombre), Valor::de_texto(nombre_organizacion(tabla.organizacion)),
                           Valor::de_texto(tabla.columnas[tabla.pk].nombre), Valor::de_entero(static_cast<long long>(tabla.columnas.size())),
                           Valor::de_entero(static_cast<long long>(almacen.registros())), Valor::de_entero(static_cast<long long>(almacen.paginas())),
                           Valor::de_entero(static_cast<long long>(almacen.bytes())), Valor::de_texto(indices), Valor::de_texto(almacen.detalle())});
    }
    r.afectadas = r.filas.size();
    r.mensaje = texto(r.filas.size()) + " tablas";
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
                           Valor::de_texto(indice ? indice->nombre + (indice->tipo == "HASH" ? " (hash extensible)" : " (B+ no agrupado)") : "")});
    }
    r.afectadas = r.filas.size();
    r.mensaje = tabla.nombre + ": " + nombre_organizacion(tabla.organizacion) + " en " + tabla.archivo;
    return r;
}

}
}
