#include "bplus_no_agrupado.h"
#include "buffer_pool.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using detalle_bplus_no_agrupado::BufferPool;
using detalle_bplus_no_agrupado::GestorPaginas;
using detalle_bplus_no_agrupado::PageId;
using detalle_bplus_no_agrupado::PAGINA_NULA;
using detalle_bplus_no_agrupado::TAM_PAGINA;

// espacio = 4096 - 8 de cabecera, hijo = 4 bytes, par (clave, pos) = 16 bytes
// orden = (espacio - hijo) / (hijo + par) + 1 = (4088 - 4) / 20 + 1 = 205
const int ORDEN = 205;
const int MAX_CLAVES = ORDEN - 1;
const int MAX_HIJOS = ORDEN;
const int MIN_HOJA = ORDEN / 2;
const int MIN_INTERNO = (ORDEN + 1) / 2 - 1;

const std::uint8_t NODO_INTERNO = 0;
const std::uint8_t NODO_HOJA = 1;
const std::uint32_t MAGICO = 0x42504E41u;
const std::uint16_t VERSION = 1;

struct Entrada {
    int clave;
    long long pos;
};

inline bool menor(const Entrada& a, const Entrada& b) {
    if (a.clave != b.clave) return a.clave < b.clave;
    return a.pos < b.pos;
}

inline bool iguales(const Entrada& a, const Entrada& b) {
    return a.clave == b.clave && a.pos == b.pos;
}

struct CabeceraPagina {
    std::uint8_t tipo;
    std::uint8_t reservado;
    std::uint16_t num;
    PageId siguiente;
};

struct CabeceraArchivo {
    PageId raiz;
    PageId libres;
    std::uint64_t num_entradas;
    std::uint32_t magico;
    std::uint16_t version;
    std::uint16_t reservado;
};

static_assert(sizeof(CabeceraPagina) == 8, "cabecera de pagina inesperada");
static_assert(std::is_trivially_copyable<Entrada>::value,
              "Entrada debe poder copiarse a la pagina");

struct VistaHoja {
    CabeceraPagina* cab;
    Entrada* entradas;

    explicit VistaHoja(char* pagina)
        : cab(reinterpret_cast<CabeceraPagina*>(pagina)),
          entradas(reinterpret_cast<Entrada*>(pagina + sizeof(CabeceraPagina))) {}
};

struct VistaInterna {
    CabeceraPagina* cab;
    Entrada* entradas;
    PageId* hijos;

    explicit VistaInterna(char* pagina)
        : cab(reinterpret_cast<CabeceraPagina*>(pagina)),
          entradas(reinterpret_cast<Entrada*>(pagina + sizeof(CabeceraPagina))),
          hijos(reinterpret_cast<PageId*>(pagina + sizeof(CabeceraPagina) +
                                          sizeof(Entrada) * MAX_CLAVES)) {}
};

static_assert(sizeof(CabeceraPagina) + sizeof(Entrada) * MAX_CLAVES <= TAM_PAGINA,
              "la hoja no cabe en una pagina");
static_assert(sizeof(CabeceraPagina) + sizeof(Entrada) * MAX_CLAVES +
                      sizeof(PageId) * MAX_HIJOS <= TAM_PAGINA,
              "el nodo interno no cabe en una pagina");

int posicion_en_hoja(const Entrada* entradas, int n, const Entrada& buscada) {
    int inicio = 0;
    int fin = n;
    while (inicio < fin) {
        int medio = (inicio + fin) / 2;
        if (menor(entradas[medio], buscada)) inicio = medio + 1;
        else fin = medio;
    }
    return inicio;
}

int hijo_para(const Entrada* entradas, int n, const Entrada& buscada) {
    int inicio = 0;
    int fin = n;
    while (inicio < fin) {
        int medio = (inicio + fin) / 2;
        if (!menor(buscada, entradas[medio])) inicio = medio + 1;
        else fin = medio;
    }
    return inicio;
}

} 

struct BPlusNoAgrupado::Impl {
    GestorPaginas gestor;
    BufferPool pool;
    CabeceraArchivo cab{};

    // si el archivo está vacío crea la cabecera y una raiz hoja; si no, lee la cabecera
    Impl(const std::string& ruta, bool truncar) : gestor(ruta, truncar), pool(gestor) {
        if (gestor.num_paginas == 0) {
            gestor.asignar();
            PageId raiz = gestor.asignar();
            char* pagina = pool.fijar(raiz);
            VistaHoja hoja(pagina);
            hoja.cab->tipo = NODO_HOJA;
            hoja.cab->reservado = 0;
            hoja.cab->num = 0;
            hoja.cab->siguiente = PAGINA_NULA;
            pool.soltar(raiz, true);

            cab.raiz = raiz;
            cab.libres = PAGINA_NULA;
            cab.num_entradas = 0;
            cab.magico = MAGICO;
            cab.version = VERSION;
            cab.reservado = 0;
            guardar_cabecera();
        } else {
            cargar_cabecera();
            if (cab.magico != MAGICO || cab.version != VERSION ||
                cab.raiz == PAGINA_NULA || cab.raiz >= gestor.num_paginas) {
                throw std::invalid_argument("archivo B+ no agrupado invalido");
            }
        }
    }

    ~Impl() {
        try {
            guardar_cabecera();
            pool.vaciar();
        } catch (...) {
        }
    }

    void guardar_cabecera() {
        char* pagina = pool.fijar(PAGINA_NULA);
        std::memset(pagina, 0, TAM_PAGINA);
        std::memcpy(pagina, &cab, sizeof(CabeceraArchivo));
        pool.soltar(PAGINA_NULA, true);
    }

    void cargar_cabecera() {
        char* pagina = pool.fijar(PAGINA_NULA);
        std::memcpy(&cab, pagina, sizeof(CabeceraArchivo));
        pool.soltar(PAGINA_NULA, false);
    }

    PageId asignar_pagina() {
        PageId pagina;
        if (cab.libres != PAGINA_NULA) {
            pagina = cab.libres;
            char* datos = pool.fijar(pagina);
            std::memcpy(&cab.libres, datos, sizeof(PageId));
            std::memset(datos, 0, TAM_PAGINA);
            pool.soltar(pagina, true);
        } else {
            pagina = gestor.asignar();
        }
        guardar_cabecera();
        return pagina;
    }

    void liberar_pagina(PageId pagina) {
        char* datos = pool.fijar(pagina);
        std::memset(datos, 0, TAM_PAGINA);
        std::memcpy(datos, &cab.libres, sizeof(PageId));
        pool.soltar(pagina, true);
        cab.libres = pagina;
        guardar_cabecera();
    }

    PageId buscar_hoja(const Entrada& buscada) {
        PageId pagina_id = cab.raiz;
        while (true) {
            char* pagina = pool.fijar(pagina_id);
            CabeceraPagina* cabecera = reinterpret_cast<CabeceraPagina*>(pagina);
            if (cabecera->tipo == NODO_HOJA) {
                pool.soltar(pagina_id, false);
                return pagina_id;
            }
            VistaInterna nodo(pagina);
            PageId hijo = nodo.hijos[hijo_para(nodo.entradas, nodo.cab->num, buscada)];
            pool.soltar(pagina_id, false);
            pagina_id = hijo;
        }
    }

    // inserta en el subárbol; devuelve true si el nodo se partió y deja el separador que sube
    bool insertar_en(PageId pagina_id, const Entrada& nueva, Entrada& separador,
                     PageId& pagina_nueva, bool& insertada) {
        char* pagina = pool.fijar(pagina_id);
        std::uint8_t tipo = reinterpret_cast<CabeceraPagina*>(pagina)->tipo;

        if (tipo == NODO_HOJA) {
            VistaHoja hoja(pagina);
            int n = hoja.cab->num;
            int i = posicion_en_hoja(hoja.entradas, n, nueva);
            if (i < n && iguales(hoja.entradas[i], nueva)) {
                insertada = false;
                pool.soltar(pagina_id, false);
                return false;
            }
            insertada = true;

            if (n < MAX_CLAVES) {
                std::memmove(&hoja.entradas[i + 1], &hoja.entradas[i],
                             sizeof(Entrada) * static_cast<std::size_t>(n - i));
                hoja.entradas[i] = nueva;
                hoja.cab->num = static_cast<std::uint16_t>(n + 1);
                pool.soltar(pagina_id, true);
                return false;
            }

            std::vector<Entrada> temporal(static_cast<std::size_t>(n + 1));
            std::copy(hoja.entradas, hoja.entradas + i, temporal.begin());
            temporal[static_cast<std::size_t>(i)] = nueva;
            std::copy(hoja.entradas + i, hoja.entradas + n,
                      temporal.begin() + i + 1);

            int total = n + 1;
            int mitad = total / 2;
            PageId derecha_id = asignar_pagina();
            char* pagina_derecha = pool.fijar(derecha_id);
            VistaHoja derecha(pagina_derecha);
            derecha.cab->tipo = NODO_HOJA;
            derecha.cab->reservado = 0;
            derecha.cab->num = static_cast<std::uint16_t>(total - mitad);
            derecha.cab->siguiente = hoja.cab->siguiente;
            std::copy(temporal.begin() + mitad, temporal.end(), derecha.entradas);

            hoja.cab->num = static_cast<std::uint16_t>(mitad);
            hoja.cab->siguiente = derecha_id;
            std::copy(temporal.begin(), temporal.begin() + mitad, hoja.entradas);

            separador = derecha.entradas[0];
            pagina_nueva = derecha_id;
            pool.soltar(derecha_id, true);
            pool.soltar(pagina_id, true);
            return true;
        }

        VistaInterna nodo(pagina);
        int i = hijo_para(nodo.entradas, nodo.cab->num, nueva);
        PageId hijo = nodo.hijos[i];
        Entrada separador_hijo{};
        PageId pagina_hijo_nueva = PAGINA_NULA;
        bool hubo_split = insertar_en(hijo, nueva, separador_hijo,
                                      pagina_hijo_nueva, insertada);
        if (!hubo_split) {
            pool.soltar(pagina_id, false);
            return false;
        }

        int n = nodo.cab->num;
        if (n < MAX_CLAVES) {
            std::memmove(&nodo.entradas[i + 1], &nodo.entradas[i],
                         sizeof(Entrada) * static_cast<std::size_t>(n - i));
            std::memmove(&nodo.hijos[i + 2], &nodo.hijos[i + 1],
                         sizeof(PageId) * static_cast<std::size_t>(n - i));
            nodo.entradas[i] = separador_hijo;
            nodo.hijos[i + 1] = pagina_hijo_nueva;
            nodo.cab->num = static_cast<std::uint16_t>(n + 1);
            pool.soltar(pagina_id, true);
            return false;
        }

        std::vector<Entrada> entradas(static_cast<std::size_t>(n + 1));
        std::vector<PageId> hijos(static_cast<std::size_t>(n + 2));
        std::copy(nodo.entradas, nodo.entradas + i, entradas.begin());
        entradas[static_cast<std::size_t>(i)] = separador_hijo;
        std::copy(nodo.entradas + i, nodo.entradas + n, entradas.begin() + i + 1);
        std::copy(nodo.hijos, nodo.hijos + i + 1, hijos.begin());
        hijos[static_cast<std::size_t>(i + 1)] = pagina_hijo_nueva;
        std::copy(nodo.hijos + i + 1, nodo.hijos + n + 1, hijos.begin() + i + 2);

        int total = n + 1;
        int mitad = total / 2;
        Entrada sube = entradas[static_cast<std::size_t>(mitad)];
        PageId derecha_id = asignar_pagina();
        char* pagina_derecha = pool.fijar(derecha_id);
        VistaInterna derecha(pagina_derecha);
        derecha.cab->tipo = NODO_INTERNO;
        derecha.cab->reservado = 0;
        derecha.cab->num = static_cast<std::uint16_t>(total - mitad - 1);
        derecha.cab->siguiente = PAGINA_NULA;
        std::copy(entradas.begin() + mitad + 1, entradas.end(), derecha.entradas);
        std::copy(hijos.begin() + mitad + 1, hijos.end(), derecha.hijos);

        nodo.cab->num = static_cast<std::uint16_t>(mitad);
        std::copy(entradas.begin(), entradas.begin() + mitad, nodo.entradas);
        std::copy(hijos.begin(), hijos.begin() + mitad + 1, nodo.hijos);

        separador = sube;
        pagina_nueva = derecha_id;
        pool.soltar(derecha_id, true);
        pool.soltar(pagina_id, true);
        return true;
    }

    // inserta el par y, si la raiz se partió, crea una raiz nueva
    void insertar(int clave, long long pos) {
        Entrada separador{};
        PageId pagina_nueva = PAGINA_NULA;
        bool insertada = false;
        Entrada nueva{clave, pos};

        if (insertar_en(cab.raiz, nueva, separador, pagina_nueva, insertada)) {
            PageId raiz_nueva = asignar_pagina();
            char* pagina = pool.fijar(raiz_nueva);
            VistaInterna raiz(pagina);
            raiz.cab->tipo = NODO_INTERNO;
            raiz.cab->reservado = 0;
            raiz.cab->num = 1;
            raiz.cab->siguiente = PAGINA_NULA;
            raiz.entradas[0] = separador;
            raiz.hijos[0] = cab.raiz;
            raiz.hijos[1] = pagina_nueva;
            pool.soltar(raiz_nueva, true);
            cab.raiz = raiz_nueva;
        }

        if (insertada) {
            cab.num_entradas++;
            guardar_cabecera();
        }
    }

    // baja a la primera hoja posible y recorre las hojas juntando las pos
    std::vector<long long> buscar_rango(int desde, int hasta) {
        std::vector<long long> resultado;
        if (desde > hasta) return resultado;

        Entrada inicio{desde, std::numeric_limits<long long>::min()};
        PageId pagina_id = buscar_hoja(inicio);
        while (pagina_id != PAGINA_NULA) {
            char* pagina = pool.fijar(pagina_id);
            VistaHoja hoja(pagina);
            PageId siguiente = hoja.cab->siguiente;
            bool terminar = false;
            for (int i = 0; i < hoja.cab->num; i++) {
                if (hoja.entradas[i].clave > hasta) {
                    terminar = true;
                    break;
                }
                if (hoja.entradas[i].clave >= desde) {
                    resultado.push_back(hoja.entradas[i].pos);
                }
            }
            pool.soltar(pagina_id, false);
            if (terminar) break;
            pagina_id = siguiente;
        }
        return resultado;
    }

    // borra en el subárbol; devuelve true si el nodo quedó bajo el mínimo
    bool eliminar_en(PageId pagina_id, const Entrada& buscada, bool& encontrada) {
        char* pagina = pool.fijar(pagina_id);
        std::uint8_t tipo = reinterpret_cast<CabeceraPagina*>(pagina)->tipo;

        if (tipo == NODO_HOJA) {
            VistaHoja hoja(pagina);
            int n = hoja.cab->num;
            int i = posicion_en_hoja(hoja.entradas, n, buscada);
            if (i >= n || !iguales(hoja.entradas[i], buscada)) {
                pool.soltar(pagina_id, false);
                return false;
            }
            std::memmove(&hoja.entradas[i], &hoja.entradas[i + 1],
                         sizeof(Entrada) * static_cast<std::size_t>(n - i - 1));
            hoja.cab->num = static_cast<std::uint16_t>(n - 1);
            encontrada = true;
            bool bajo = hoja.cab->num < MIN_HOJA;
            pool.soltar(pagina_id, true);
            return bajo;
        }

        VistaInterna nodo(pagina);
        int i = hijo_para(nodo.entradas, nodo.cab->num, buscada);
        PageId hijo = nodo.hijos[i];
        bool hijo_bajo = eliminar_en(hijo, buscada, encontrada);
        if (!hijo_bajo) {
            pool.soltar(pagina_id, false);
            return false;
        }

        reparar_hijo(pagina, i);
        bool bajo = nodo.cab->num < MIN_INTERNO;
        pool.soltar(pagina_id, true);
        return bajo;
    }

    // pasa una entrada del hermano izquierdo al hijo
    void prestar_izquierda(VistaInterna& padre, int i, char* pagina_izq,
                           char* pagina_hijo, bool es_hoja) {
        if (es_hoja) {
            VistaHoja izquierda(pagina_izq), hijo(pagina_hijo);
            int ni = izquierda.cab->num;
            int nh = hijo.cab->num;
            std::memmove(&hijo.entradas[1], &hijo.entradas[0],
                         sizeof(Entrada) * static_cast<std::size_t>(nh));
            hijo.entradas[0] = izquierda.entradas[ni - 1];
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            izquierda.cab->num = static_cast<std::uint16_t>(ni - 1);
            padre.entradas[i - 1] = hijo.entradas[0];
        } else {
            VistaInterna izquierda(pagina_izq), hijo(pagina_hijo);
            int ni = izquierda.cab->num;
            int nh = hijo.cab->num;
            std::memmove(&hijo.entradas[1], &hijo.entradas[0],
                         sizeof(Entrada) * static_cast<std::size_t>(nh));
            std::memmove(&hijo.hijos[1], &hijo.hijos[0],
                         sizeof(PageId) * static_cast<std::size_t>(nh + 1));
            hijo.entradas[0] = padre.entradas[i - 1];
            hijo.hijos[0] = izquierda.hijos[ni];
            padre.entradas[i - 1] = izquierda.entradas[ni - 1];
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            izquierda.cab->num = static_cast<std::uint16_t>(ni - 1);
        }
    }

    // pasa una entrada del hermano derecho al hijo
    void prestar_derecha(VistaInterna& padre, int i, char* pagina_der,
                         char* pagina_hijo, bool es_hoja) {
        if (es_hoja) {
            VistaHoja derecha(pagina_der), hijo(pagina_hijo);
            int nd = derecha.cab->num;
            int nh = hijo.cab->num;
            hijo.entradas[nh] = derecha.entradas[0];
            std::memmove(&derecha.entradas[0], &derecha.entradas[1],
                         sizeof(Entrada) * static_cast<std::size_t>(nd - 1));
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            derecha.cab->num = static_cast<std::uint16_t>(nd - 1);
            padre.entradas[i] = derecha.entradas[0];
        } else {
            VistaInterna derecha(pagina_der), hijo(pagina_hijo);
            int nd = derecha.cab->num;
            int nh = hijo.cab->num;
            hijo.entradas[nh] = padre.entradas[i];
            hijo.hijos[nh + 1] = derecha.hijos[0];
            padre.entradas[i] = derecha.entradas[0];
            std::memmove(&derecha.entradas[0], &derecha.entradas[1],
                         sizeof(Entrada) * static_cast<std::size_t>(nd - 1));
            std::memmove(&derecha.hijos[0], &derecha.hijos[1],
                         sizeof(PageId) * static_cast<std::size_t>(nd));
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            derecha.cab->num = static_cast<std::uint16_t>(nd - 1);
        }
    }

    // junta dos hermanos en el izquierdo y libera la página del derecho
    void fusionar(VistaInterna& padre, int separador, bool es_hoja) {
        PageId izquierda_id = padre.hijos[separador];
        PageId derecha_id = padre.hijos[separador + 1];
        char* pagina_izq = pool.fijar(izquierda_id);
        char* pagina_der = pool.fijar(derecha_id);

        if (es_hoja) {
            VistaHoja izquierda(pagina_izq), derecha(pagina_der);
            std::copy(derecha.entradas, derecha.entradas + derecha.cab->num,
                      izquierda.entradas + izquierda.cab->num);
            izquierda.cab->num = static_cast<std::uint16_t>(
                izquierda.cab->num + derecha.cab->num);
            izquierda.cab->siguiente = derecha.cab->siguiente;
        } else {
            VistaInterna izquierda(pagina_izq), derecha(pagina_der);
            int inicio = izquierda.cab->num;
            izquierda.entradas[inicio] = padre.entradas[separador];
            std::copy(derecha.entradas, derecha.entradas + derecha.cab->num,
                      izquierda.entradas + inicio + 1);
            std::copy(derecha.hijos, derecha.hijos + derecha.cab->num + 1,
                      izquierda.hijos + inicio + 1);
            izquierda.cab->num = static_cast<std::uint16_t>(
                izquierda.cab->num + derecha.cab->num + 1);
        }

        pool.soltar(izquierda_id, true);
        pool.soltar(derecha_id, false);
        liberar_pagina(derecha_id);

        int n = padre.cab->num;
        std::memmove(&padre.entradas[separador], &padre.entradas[separador + 1],
                     sizeof(Entrada) * static_cast<std::size_t>(n - separador - 1));
        std::memmove(&padre.hijos[separador + 1], &padre.hijos[separador + 2],
                     sizeof(PageId) * static_cast<std::size_t>(n - separador - 1));
        padre.cab->num = static_cast<std::uint16_t>(n - 1);
    }

    // arregla un hijo bajo el mínimo: pide prestado a un hermano o fusiona
    void reparar_hijo(char* pagina_padre, int i) {
        VistaInterna padre(pagina_padre);
        PageId hijo_id = padre.hijos[i];
        char* pagina_hijo = pool.fijar(hijo_id);
        bool es_hoja = reinterpret_cast<CabeceraPagina*>(pagina_hijo)->tipo == NODO_HOJA;
        int minimo = es_hoja ? MIN_HOJA : MIN_INTERNO;

        if (i > 0) {
            PageId izquierda_id = padre.hijos[i - 1];
            char* pagina_izq = pool.fijar(izquierda_id);
            int cantidad = reinterpret_cast<CabeceraPagina*>(pagina_izq)->num;
            if (cantidad > minimo) {
                prestar_izquierda(padre, i, pagina_izq, pagina_hijo, es_hoja);
                pool.soltar(izquierda_id, true);
                pool.soltar(hijo_id, true);
                return;
            }
            pool.soltar(izquierda_id, false);
        }

        if (i < padre.cab->num) {
            PageId derecha_id = padre.hijos[i + 1];
            char* pagina_der = pool.fijar(derecha_id);
            int cantidad = reinterpret_cast<CabeceraPagina*>(pagina_der)->num;
            if (cantidad > minimo) {
                prestar_derecha(padre, i, pagina_der, pagina_hijo, es_hoja);
                pool.soltar(derecha_id, true);
                pool.soltar(hijo_id, true);
                return;
            }
            pool.soltar(derecha_id, false);
        }

        pool.soltar(hijo_id, false);
        if (i > 0) fusionar(padre, i - 1, es_hoja);
        else fusionar(padre, i, es_hoja);
    }

    // borra el par y, si la raiz quedó sin separadores, baja un nivel
    bool eliminar_entrada(int clave, long long pos) {
        bool encontrada = false;
        Entrada buscada{clave, pos};
        eliminar_en(cab.raiz, buscada, encontrada);
        if (!encontrada) return false;

        cab.num_entradas--;
        char* pagina = pool.fijar(cab.raiz);
        CabeceraPagina* cabecera = reinterpret_cast<CabeceraPagina*>(pagina);
        if (cabecera->tipo == NODO_INTERNO && cabecera->num == 0) {
            PageId nueva_raiz = VistaInterna(pagina).hijos[0];
            pool.soltar(cab.raiz, false);
            PageId raiz_anterior = cab.raiz;
            cab.raiz = nueva_raiz;
            liberar_pagina(raiz_anterior);
        } else {
            pool.soltar(cab.raiz, false);
        }
        guardar_cabecera();
        return true;
    }

    int altura() {
        int resultado = 1;
        PageId pagina_id = cab.raiz;
        while (true) {
            char* pagina = pool.fijar(pagina_id);
            CabeceraPagina* cabecera = reinterpret_cast<CabeceraPagina*>(pagina);
            if (cabecera->tipo == NODO_HOJA) {
                pool.soltar(pagina_id, false);
                return resultado;
            }
            PageId hijo = VistaInterna(pagina).hijos[0];
            pool.soltar(pagina_id, false);
            pagina_id = hijo;
            resultado++;
        }
    }
};

BPlusNoAgrupado::BPlusNoAgrupado(const std::string& ruta, bool truncar)
    : impl_(new Impl(ruta, truncar)) {}

BPlusNoAgrupado::~BPlusNoAgrupado() { delete impl_; }

void BPlusNoAgrupado::insertar(int clave, long long pos) {
    impl_->insertar(clave, pos);
}

std::vector<long long> BPlusNoAgrupado::buscar(int clave) {
    return impl_->buscar_rango(clave, clave);
}

std::vector<long long> BPlusNoAgrupado::buscar_rango(int desde, int hasta) {
    return impl_->buscar_rango(desde, hasta);
}

int BPlusNoAgrupado::eliminar(int clave) {
    std::vector<long long> posiciones = buscar(clave);
    int eliminadas = 0;
    for (long long pos : posiciones) {
        if (impl_->eliminar_entrada(clave, pos)) eliminadas++;
    }
    return eliminadas;
}

bool BPlusNoAgrupado::eliminar_entrada(int clave, long long pos) {
    return impl_->eliminar_entrada(clave, pos);
}

int BPlusNoAgrupado::altura() { return impl_->altura(); }

long BPlusNoAgrupado::num_entradas() const {
    return static_cast<long>(impl_->cab.num_entradas);
}

long BPlusNoAgrupado::tamano_en_disco() {
    sincronizar();
    return impl_->gestor.tamano_en_disco();
}

void BPlusNoAgrupado::sincronizar() {
    impl_->guardar_cabecera();
    impl_->pool.vaciar();
}


void BPlusNoAgrupado::enfriar_cache() {
    impl_->guardar_cabecera();
    impl_->pool.limpiar();
    impl_->cargar_cabecera();
}

long BPlusNoAgrupado::paginas_leidas() const { return impl_->gestor.lecturas; }
long BPlusNoAgrupado::paginas_escritas() const { return impl_->gestor.escrituras; }
long BPlusNoAgrupado::aciertos_cache() const { return impl_->pool.aciertos; }
long BPlusNoAgrupado::fallos_cache() const { return impl_->pool.fallos; }
long BPlusNoAgrupado::desalojos_cache() const { return impl_->pool.desalojos; }

int BPlusNoAgrupado::paginas_en_cache() const {
    return impl_->pool.paginas_residentes();
}
