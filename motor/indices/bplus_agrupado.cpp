#include "bplus_agrupado.h"
#include "buffer_pool.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace motor {

namespace {

using detalle_bplus_no_agrupado::BufferPool;
using detalle_bplus_no_agrupado::GestorPaginas;
using detalle_bplus_no_agrupado::PageId;
using detalle_bplus_no_agrupado::PAGINA_NULA;
using detalle_bplus_no_agrupado::TAM_PAGINA;

const std::uint8_t NODO_INTERNO = 0;
const std::uint8_t NODO_HOJA = 1;
const std::uint32_t MAGICO = 0x42504C53u;  // "BPLS"
const std::uint16_t VERSION = 2;

// cabecera de cada página, 8 bytes
struct CabeceraPagina {
    std::uint8_t tipo;
    std::uint8_t reservado;
    std::uint16_t num;
    PageId siguiente;
};

// cabecera del archivo, va en la página 0
struct CabeceraArchivo {
    std::uint32_t magico;
    std::uint16_t version;
    std::uint16_t tam_registro;
    std::uint16_t max_regs_hoja;
    std::uint16_t max_hijos;
    PageId raiz;
    PageId libres;
    std::uint64_t num_registros;
};

static_assert(sizeof(CabeceraPagina) == 8, "cabecera de pagina inesperada");
static_assert(sizeof(CabeceraArchivo) <= TAM_PAGINA, "la cabecera no cabe en la pagina 0");

const int ESPACIO_UTIL = TAM_PAGINA - static_cast<int>(sizeof(CabeceraPagina));

int clave_en(const char* registro) {
    int clave;
    std::memcpy(&clave, registro, sizeof(int));
    return clave;
}

// vista sobre los bytes de una hoja: registros de tam bytes, uno tras otro
struct VistaHoja {
    CabeceraPagina* cab;
    char* regs;
    int tam;

    VistaHoja(char* pagina, int tam_registro)
        : cab(reinterpret_cast<CabeceraPagina*>(pagina)),
          regs(pagina + sizeof(CabeceraPagina)),
          tam(tam_registro) {}

    char* reg(int i) const { return regs + static_cast<std::size_t>(i) * tam; }
    int clave(int i) const { return clave_en(reg(i)); }
};

// vista sobre los bytes de un nodo interno: hijos[max_hijos] y luego claves[max_hijos - 1]
struct VistaInterna {
    CabeceraPagina* cab;
    PageId* hijos;
    int* claves;

    VistaInterna(char* pagina, int max_hijos)
        : cab(reinterpret_cast<CabeceraPagina*>(pagina)),
          hijos(reinterpret_cast<PageId*>(pagina + sizeof(CabeceraPagina))),
          claves(reinterpret_cast<int*>(pagina + sizeof(CabeceraPagina) +
                                        sizeof(PageId) * max_hijos)) {}
};

}  // namespace

struct BPlusAgrupado::Impl {
    GestorPaginas gestor;
    BufferPool pool;
    CabeceraArchivo cab{};

    // derivados de la cabecera
    int tam = 0;
    int max_regs = 0;
    int min_regs = 0;
    int max_hijos = 0;
    int max_claves = 0;
    int min_claves = 0;

    // si el archivo está vacío crea cabecera y raíz hoja; si no, lee y valida la cabecera
    Impl(const std::string& ruta, std::uint16_t tam_registro, bool truncar,
         std::uint16_t max_regs_hoja, std::uint16_t max_hijos_pedidos)
        : gestor(ruta, truncar), pool(gestor) {
        if (gestor.num_paginas == 0) {
            crear(tam_registro, max_regs_hoja, max_hijos_pedidos);
        } else {
            cargar_cabecera();
            validar(tam_registro);
            derivar();
        }
    }

    ~Impl() {
        try {
            guardar_cabecera();
            pool.vaciar();
        } catch (...) {
        }
    }

    void crear(std::uint16_t tam_registro, std::uint16_t max_regs_hoja,
               std::uint16_t max_hijos_pedidos) {
        if (tam_registro < sizeof(int)) {
            throw std::invalid_argument("tam_registro debe ser al menos 4 bytes");
        }
        const int cap_regs = ESPACIO_UTIL / tam_registro;
        const int cap_hijos = (ESPACIO_UTIL + static_cast<int>(sizeof(int))) /
                              static_cast<int>(sizeof(PageId) + sizeof(int));
        if (cap_regs < 2) {
            throw std::invalid_argument("el registro es demasiado grande: no caben 2 por pagina");
        }
        const int regs = (max_regs_hoja > 0 && max_regs_hoja < cap_regs) ? max_regs_hoja : cap_regs;
        const int hijos = (max_hijos_pedidos > 0 && max_hijos_pedidos < cap_hijos)
                              ? max_hijos_pedidos : cap_hijos;
        if (regs < 2) throw std::invalid_argument("max_regs_hoja debe ser al menos 2");
        if (hijos < 3) throw std::invalid_argument("max_hijos debe ser al menos 3");

        cab.magico = MAGICO;
        cab.version = VERSION;
        cab.tam_registro = tam_registro;
        cab.max_regs_hoja = static_cast<std::uint16_t>(regs);
        cab.max_hijos = static_cast<std::uint16_t>(hijos);
        cab.libres = PAGINA_NULA;
        cab.num_registros = 0;
        derivar();

        gestor.asignar();  // página 0: cabecera
        cab.raiz = gestor.asignar();
        char* p = pool.fijar(cab.raiz);
        VistaHoja hoja(p, tam);
        hoja.cab->tipo = NODO_HOJA;
        hoja.cab->reservado = 0;
        hoja.cab->num = 0;
        hoja.cab->siguiente = PAGINA_NULA;
        pool.soltar(cab.raiz, true);
        guardar_cabecera();
    }

    void validar(std::uint16_t tam_registro) const {
        if (cab.magico != MAGICO) throw std::invalid_argument("el archivo no es un B+ agrupado");
        if (cab.version != VERSION) throw std::invalid_argument("version de B+ agrupado no soportada");
        if (cab.tam_registro != tam_registro) {
            throw std::invalid_argument("tam_registro no coincide con el archivo: esperaba " +
                                        std::to_string(cab.tam_registro));
        }
    }

    void derivar() {
        tam = cab.tam_registro;
        max_regs = cab.max_regs_hoja;
        min_regs = max_regs / 2;
        max_hijos = cab.max_hijos;
        max_claves = max_hijos - 1;
        min_claves = max_claves / 2;
    }

    // --- cabecera y páginas libres ---

    void guardar_cabecera() {
        char* p = pool.fijar(PAGINA_NULA);
        std::memcpy(p, &cab, sizeof(CabeceraArchivo));
        pool.soltar(PAGINA_NULA, true);
    }

    void cargar_cabecera() {
        char* p = pool.fijar(PAGINA_NULA);
        std::memcpy(&cab, p, sizeof(CabeceraArchivo));
        pool.soltar(PAGINA_NULA, false);
    }

    // reutiliza una libre o crece el archivo
    PageId asignar_pagina() {
        PageId p;
        if (cab.libres != PAGINA_NULA) {
            p = cab.libres;
            char* buf = pool.fijar(p);
            std::memcpy(&cab.libres, buf, sizeof(PageId));
            std::memset(buf, 0, TAM_PAGINA);
            pool.soltar(p, true);
        } else {
            p = gestor.asignar();
        }
        guardar_cabecera();
        return p;
    }

    // devuelve la página a la lista de libres
    void liberar_pagina(PageId p) {
        char* buf = pool.fijar(p);
        std::memset(buf, 0, TAM_PAGINA);
        std::memcpy(buf, &cab.libres, sizeof(PageId));
        pool.soltar(p, true);
        cab.libres = p;
        guardar_cabecera();
    }

    // --- búsqueda ---

    // primera posición con clave >= buscada
    static int pos_en_hoja(const VistaHoja& hoja, int n, int clave) {
        int inicio = 0;
        int fin = n;
        while (inicio < fin) {
            int medio = (inicio + fin) / 2;
            if (hoja.clave(medio) < clave) inicio = medio + 1;
            else fin = medio;
        }
        return inicio;
    }

    // por qué hijo bajar: clave >= claves[i] va a la derecha
    static int hijo_para(const int* claves, int n, int clave) {
        int inicio = 0;
        int fin = n;
        while (inicio < fin) {
            int medio = (inicio + fin) / 2;
            if (clave >= claves[medio]) inicio = medio + 1;
            else fin = medio;
        }
        return inicio;
    }

    PageId bajar_hasta_hoja(int clave) {
        PageId pid = cab.raiz;
        while (true) {
            char* p = pool.fijar(pid);
            CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
            if (c->tipo == NODO_HOJA) {
                pool.soltar(pid, false);
                return pid;
            }
            VistaInterna nodo(p, max_hijos);
            PageId hijo = nodo.hijos[hijo_para(nodo.claves, nodo.cab->num, clave)];
            pool.soltar(pid, false);
            pid = hijo;
        }
    }

    PageId primera_hoja() {
        PageId pid = cab.raiz;
        while (true) {
            char* p = pool.fijar(pid);
            CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
            if (c->tipo == NODO_HOJA) {
                pool.soltar(pid, false);
                return pid;
            }
            PageId hijo = VistaInterna(p, max_hijos).hijos[0];
            pool.soltar(pid, false);
            pid = hijo;
        }
    }

    bool buscar(int clave, void* salida) {
        PageId pid = bajar_hasta_hoja(clave);
        char* p = pool.fijar(pid);
        VistaHoja hoja(p, tam);
        int i = pos_en_hoja(hoja, hoja.cab->num, clave);
        bool ok = i < hoja.cab->num && hoja.clave(i) == clave;
        if (ok) std::memcpy(salida, hoja.reg(i), tam);
        pool.soltar(pid, false);
        return ok;
    }

    // recorre las hojas desde 'desde' hasta pasarse de 'hasta'
    void buscar_rango(int desde, int hasta, const Visitante& visitante) {
        PageId pid = bajar_hasta_hoja(desde);
        while (pid != PAGINA_NULA) {
            char* p = pool.fijar(pid);
            VistaHoja hoja(p, tam);
            PageId siguiente = hoja.cab->siguiente;
            bool terminar = false;
            for (int i = 0; i < hoja.cab->num; i++) {
                int clave = hoja.clave(i);
                if (clave > hasta) { terminar = true; break; }
                if (clave >= desde && !visitante(hoja.reg(i))) { terminar = true; break; }
            }
            pool.soltar(pid, false);
            if (terminar) break;
            pid = siguiente;
        }
    }

    // --- inserción ---

    // false si la clave ya existía; si se parte la raíz el árbol crece
    bool insertar(const char* registro) {
        bool insertado = false;
        int clave_sube = 0;
        PageId pagina_nueva = PAGINA_NULA;
        bool split = insertar_en(cab.raiz, registro, insertado, clave_sube, pagina_nueva);
        if (!insertado) return false;

        if (split) {
            PageId nueva_raiz = asignar_pagina();
            char* p = pool.fijar(nueva_raiz);
            VistaInterna nodo(p, max_hijos);
            nodo.cab->tipo = NODO_INTERNO;
            nodo.cab->reservado = 0;
            nodo.cab->num = 1;
            nodo.cab->siguiente = PAGINA_NULA;
            nodo.hijos[0] = cab.raiz;
            nodo.claves[0] = clave_sube;
            nodo.hijos[1] = pagina_nueva;
            pool.soltar(nueva_raiz, true);
            cab.raiz = nueva_raiz;
        }
        cab.num_registros++;
        guardar_cabecera();
        return true;
    }

    // recursivo; devuelve si hubo split y qué sube al padre
    bool insertar_en(PageId pid, const char* registro, bool& insertado,
                     int& clave_sube, PageId& pagina_nueva) {
        const int clave = clave_en(registro);
        char* p = pool.fijar(pid);
        std::uint8_t tipo = reinterpret_cast<CabeceraPagina*>(p)->tipo;

        if (tipo == NODO_HOJA) {
            VistaHoja hoja(p, tam);
            int n = hoja.cab->num;
            int i = pos_en_hoja(hoja, n, clave);
            if (i < n && hoja.clave(i) == clave) {
                pool.soltar(pid, false);
                return false;
            }
            insertado = true;

            if (n < max_regs) {
                std::memmove(hoja.reg(i + 1), hoja.reg(i), static_cast<std::size_t>(tam) * (n - i));
                std::memcpy(hoja.reg(i), registro, tam);
                hoja.cab->num = static_cast<std::uint16_t>(n + 1);
                pool.soltar(pid, true);
                return false;
            }

            // hoja llena: se ordena en un temporal y se reparte en dos
            std::vector<char> temp(static_cast<std::size_t>(tam) * (n + 1));
            std::memcpy(temp.data(), hoja.reg(0), static_cast<std::size_t>(tam) * i);
            std::memcpy(temp.data() + static_cast<std::size_t>(tam) * i, registro, tam);
            std::memcpy(temp.data() + static_cast<std::size_t>(tam) * (i + 1), hoja.reg(i),
                        static_cast<std::size_t>(tam) * (n - i));

            int total = n + 1;
            int mitad = total / 2;

            PageId nueva = asignar_pagina();
            char* pn = pool.fijar(nueva);
            VistaHoja hn(pn, tam);
            hn.cab->tipo = NODO_HOJA;
            hn.cab->reservado = 0;
            hn.cab->num = static_cast<std::uint16_t>(total - mitad);
            hn.cab->siguiente = hoja.cab->siguiente;
            std::memcpy(hn.reg(0), temp.data() + static_cast<std::size_t>(tam) * mitad,
                        static_cast<std::size_t>(tam) * (total - mitad));

            hoja.cab->num = static_cast<std::uint16_t>(mitad);
            hoja.cab->siguiente = nueva;
            std::memcpy(hoja.reg(0), temp.data(), static_cast<std::size_t>(tam) * mitad);

            clave_sube = hn.clave(0);
            pagina_nueva = nueva;
            pool.soltar(nueva, true);
            pool.soltar(pid, true);
            return true;
        }

        VistaInterna nodo(p, max_hijos);
        int i = hijo_para(nodo.claves, nodo.cab->num, clave);
        PageId hijo = nodo.hijos[i];

        int csube = 0;
        PageId pnueva = PAGINA_NULA;
        bool hubo_split = insertar_en(hijo, registro, insertado, csube, pnueva);
        if (!hubo_split) {
            pool.soltar(pid, false);
            return false;
        }

        int n = nodo.cab->num;
        if (n < max_claves) {
            std::memmove(&nodo.claves[i + 1], &nodo.claves[i], sizeof(int) * (n - i));
            std::memmove(&nodo.hijos[i + 2], &nodo.hijos[i + 1], sizeof(PageId) * (n - i));
            nodo.claves[i] = csube;
            nodo.hijos[i + 1] = pnueva;
            nodo.cab->num = static_cast<std::uint16_t>(n + 1);
            pool.soltar(pid, true);
            return false;
        }

        // nodo lleno: la clave del medio sube y desaparece de abajo
        std::vector<int> tclaves(n + 1);
        std::vector<PageId> thijos(n + 2);
        std::memcpy(tclaves.data(), nodo.claves, sizeof(int) * i);
        tclaves[i] = csube;
        std::memcpy(tclaves.data() + i + 1, nodo.claves + i, sizeof(int) * (n - i));
        std::memcpy(thijos.data(), nodo.hijos, sizeof(PageId) * (i + 1));
        thijos[i + 1] = pnueva;
        std::memcpy(thijos.data() + i + 2, nodo.hijos + i + 1, sizeof(PageId) * (n - i));

        int total = n + 1;
        int mitad = total / 2;
        int sube = tclaves[mitad];

        PageId nuevo = asignar_pagina();
        char* pn = pool.fijar(nuevo);
        VistaInterna nn(pn, max_hijos);
        nn.cab->tipo = NODO_INTERNO;
        nn.cab->reservado = 0;
        nn.cab->siguiente = PAGINA_NULA;
        nn.cab->num = static_cast<std::uint16_t>(total - mitad - 1);
        std::memcpy(nn.claves, tclaves.data() + mitad + 1, sizeof(int) * nn.cab->num);
        std::memcpy(nn.hijos, thijos.data() + mitad + 1, sizeof(PageId) * (nn.cab->num + 1));

        nodo.cab->num = static_cast<std::uint16_t>(mitad);
        std::memcpy(nodo.claves, tclaves.data(), sizeof(int) * mitad);
        std::memcpy(nodo.hijos, thijos.data(), sizeof(PageId) * (mitad + 1));

        clave_sube = sube;
        pagina_nueva = nuevo;
        pool.soltar(nuevo, true);
        pool.soltar(pid, true);
        return true;
    }

    // --- eliminación ---

    // si la raíz queda vacía el árbol baja un nivel
    bool eliminar(int clave) {
        bool encontrado = false;
        eliminar_en(cab.raiz, clave, encontrado);
        if (!encontrado) return false;

        cab.num_registros--;
        char* p = pool.fijar(cab.raiz);
        CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
        if (c->tipo == NODO_INTERNO && c->num == 0) {
            PageId unico = VistaInterna(p, max_hijos).hijos[0];
            pool.soltar(cab.raiz, false);
            PageId vieja = cab.raiz;
            cab.raiz = unico;
            liberar_pagina(vieja);
        } else {
            pool.soltar(cab.raiz, false);
        }
        guardar_cabecera();
        return true;
    }

    // recursivo; devuelve si el nodo quedó bajo el mínimo
    bool eliminar_en(PageId pid, int clave, bool& encontrado) {
        char* p = pool.fijar(pid);
        std::uint8_t tipo = reinterpret_cast<CabeceraPagina*>(p)->tipo;

        if (tipo == NODO_HOJA) {
            VistaHoja hoja(p, tam);
            int n = hoja.cab->num;
            int i = pos_en_hoja(hoja, n, clave);
            if (i < n && hoja.clave(i) == clave) {
                std::memmove(hoja.reg(i), hoja.reg(i + 1),
                             static_cast<std::size_t>(tam) * (n - i - 1));
                hoja.cab->num = static_cast<std::uint16_t>(n - 1);
                encontrado = true;
                bool poco = hoja.cab->num < min_regs;
                pool.soltar(pid, true);
                return poco;
            }
            pool.soltar(pid, false);
            return false;
        }

        VistaInterna nodo(p, max_hijos);
        int i = hijo_para(nodo.claves, nodo.cab->num, clave);
        PageId hijo = nodo.hijos[i];
        bool hijo_bajo = eliminar_en(hijo, clave, encontrado);
        if (!hijo_bajo) {
            pool.soltar(pid, false);
            return false;
        }

        reparar_hijo(p, i);
        bool poco = nodo.cab->num < min_claves;
        pool.soltar(pid, true);
        return poco;
    }

    // el hijo i quedó bajo el mínimo: pide prestado a un hermano o fusiona
    void reparar_hijo(char* p_padre, int i) {
        VistaInterna padre(p_padre, max_hijos);
        PageId pid_hijo = padre.hijos[i];
        char* ph = pool.fijar(pid_hijo);
        bool es_hoja = reinterpret_cast<CabeceraPagina*>(ph)->tipo == NODO_HOJA;
        int minimo = es_hoja ? min_regs : min_claves;

        if (i > 0) {
            PageId pid_izq = padre.hijos[i - 1];
            char* pi = pool.fijar(pid_izq);
            int n_izq = reinterpret_cast<CabeceraPagina*>(pi)->num;
            if (n_izq > minimo) {
                prestar_de_izquierda(padre, i, pi, ph, es_hoja);
                pool.soltar(pid_izq, true);
                pool.soltar(pid_hijo, true);
                return;
            }
            pool.soltar(pid_izq, false);
        }

        if (i < padre.cab->num) {
            PageId pid_der = padre.hijos[i + 1];
            char* pd = pool.fijar(pid_der);
            int n_der = reinterpret_cast<CabeceraPagina*>(pd)->num;
            if (n_der > minimo) {
                prestar_de_derecha(padre, i, pd, ph, es_hoja);
                pool.soltar(pid_der, true);
                pool.soltar(pid_hijo, true);
                return;
            }
            pool.soltar(pid_der, false);
        }

        pool.soltar(pid_hijo, false);
        if (i > 0) fusionar(padre, i - 1, es_hoja);
        else fusionar(padre, i, es_hoja);
    }

    // toma la última entrada del hermano izquierdo
    void prestar_de_izquierda(VistaInterna& padre, int i, char* pi, char* ph, bool es_hoja) {
        if (es_hoja) {
            VistaHoja izq(pi, tam), hijo(ph, tam);
            int ni = izq.cab->num, nh = hijo.cab->num;
            std::memmove(hijo.reg(1), hijo.reg(0), static_cast<std::size_t>(tam) * nh);
            std::memcpy(hijo.reg(0), izq.reg(ni - 1), tam);
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            izq.cab->num = static_cast<std::uint16_t>(ni - 1);
            padre.claves[i - 1] = hijo.clave(0);
        } else {
            VistaInterna izq(pi, max_hijos), hijo(ph, max_hijos);
            int ni = izq.cab->num, nh = hijo.cab->num;
            std::memmove(&hijo.claves[1], &hijo.claves[0], sizeof(int) * nh);
            std::memmove(&hijo.hijos[1], &hijo.hijos[0], sizeof(PageId) * (nh + 1));
            hijo.claves[0] = padre.claves[i - 1];
            hijo.hijos[0] = izq.hijos[ni];
            padre.claves[i - 1] = izq.claves[ni - 1];
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            izq.cab->num = static_cast<std::uint16_t>(ni - 1);
        }
    }

    // toma la primera entrada del hermano derecho
    void prestar_de_derecha(VistaInterna& padre, int i, char* pd, char* ph, bool es_hoja) {
        if (es_hoja) {
            VistaHoja der(pd, tam), hijo(ph, tam);
            int nd = der.cab->num, nh = hijo.cab->num;
            std::memcpy(hijo.reg(nh), der.reg(0), tam);
            std::memmove(der.reg(0), der.reg(1), static_cast<std::size_t>(tam) * (nd - 1));
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            der.cab->num = static_cast<std::uint16_t>(nd - 1);
            padre.claves[i] = der.clave(0);
        } else {
            VistaInterna der(pd, max_hijos), hijo(ph, max_hijos);
            int nd = der.cab->num, nh = hijo.cab->num;
            hijo.claves[nh] = padre.claves[i];
            hijo.hijos[nh + 1] = der.hijos[0];
            padre.claves[i] = der.claves[0];
            std::memmove(&der.claves[0], &der.claves[1], sizeof(int) * (nd - 1));
            std::memmove(&der.hijos[0], &der.hijos[1], sizeof(PageId) * nd);
            hijo.cab->num = static_cast<std::uint16_t>(nh + 1);
            der.cab->num = static_cast<std::uint16_t>(nd - 1);
        }
    }

    // junta hijos[s] y hijos[s+1] en el izquierdo y quita el separador
    void fusionar(VistaInterna& padre, int s, bool es_hoja) {
        PageId pid_izq = padre.hijos[s];
        PageId pid_der = padre.hijos[s + 1];
        char* pi = pool.fijar(pid_izq);
        char* pd = pool.fijar(pid_der);

        if (es_hoja) {
            VistaHoja izq(pi, tam), der(pd, tam);
            std::memcpy(izq.reg(izq.cab->num), der.reg(0),
                        static_cast<std::size_t>(tam) * der.cab->num);
            izq.cab->num = static_cast<std::uint16_t>(izq.cab->num + der.cab->num);
            izq.cab->siguiente = der.cab->siguiente;
        } else {
            VistaInterna izq(pi, max_hijos), der(pd, max_hijos);
            izq.claves[izq.cab->num] = padre.claves[s];
            std::memcpy(&izq.claves[izq.cab->num + 1], der.claves, sizeof(int) * der.cab->num);
            std::memcpy(&izq.hijos[izq.cab->num + 1], der.hijos,
                        sizeof(PageId) * (der.cab->num + 1));
            izq.cab->num = static_cast<std::uint16_t>(izq.cab->num + der.cab->num + 1);
        }

        pool.soltar(pid_izq, true);
        pool.soltar(pid_der, false);
        liberar_pagina(pid_der);

        int n = padre.cab->num;
        std::memmove(&padre.claves[s], &padre.claves[s + 1], sizeof(int) * (n - s - 1));
        std::memmove(&padre.hijos[s + 1], &padre.hijos[s + 2], sizeof(PageId) * (n - s - 1));
        padre.cab->num = static_cast<std::uint16_t>(n - 1);
    }

    // --- carga masiva ---

    // cuántos tomar del nivel actual para que el resto no quede bajo el mínimo
    static std::size_t tomar(std::size_t restante, std::size_t por_nodo, std::size_t minimo,
                             std::size_t maximo) {
        std::size_t n = std::min(por_nodo, restante);
        std::size_t sobran = restante - n;
        if (sobran > 0 && sobran < minimo) {
            n = restante <= maximo ? restante : restante - minimo;
        }
        return n;
    }

    void cargar_masivo(const char* registros, std::size_t n, double factor_llenado) {
        if (cab.num_registros != 0) {
            throw std::logic_error("cargar_masivo requiere un arbol vacio");
        }
        if (n == 0) return;
        for (std::size_t i = 1; i < n; i++) {
            if (clave_en(registros + tam * i) <= clave_en(registros + tam * (i - 1))) {
                throw std::invalid_argument("cargar_masivo requiere claves ordenadas y sin repetir");
            }
        }
        factor_llenado = std::min(1.0, std::max(0.1, factor_llenado));

        liberar_pagina(cab.raiz);

        // hojas enlazadas
        const std::size_t por_hoja = std::max<std::size_t>(1, static_cast<std::size_t>(max_regs * factor_llenado));
        std::vector<PageId> nivel;
        std::vector<int> claves;
        PageId anterior = PAGINA_NULA;
        for (std::size_t i = 0; i < n;) {
            const std::size_t cuantos = tomar(n - i, por_hoja, min_regs, max_regs);
            PageId pid = asignar_pagina();
            char* p = pool.fijar(pid);
            VistaHoja hoja(p, tam);
            hoja.cab->tipo = NODO_HOJA;
            hoja.cab->reservado = 0;
            hoja.cab->num = static_cast<std::uint16_t>(cuantos);
            hoja.cab->siguiente = PAGINA_NULA;
            std::memcpy(hoja.reg(0), registros + tam * i, static_cast<std::size_t>(tam) * cuantos);
            pool.soltar(pid, true);

            if (!nivel.empty()) claves.push_back(clave_en(registros + tam * i));
            nivel.push_back(pid);

            if (anterior != PAGINA_NULA) {
                char* pa = pool.fijar(anterior);
                reinterpret_cast<CabeceraPagina*>(pa)->siguiente = pid;
                pool.soltar(anterior, true);
            }
            anterior = pid;
            i += cuantos;
        }

        // niveles internos hasta quedar con una raíz
        const std::size_t por_nodo = std::max<std::size_t>(2, static_cast<std::size_t>(max_hijos * factor_llenado));
        const std::size_t min_hijos = static_cast<std::size_t>(min_claves) + 1;
        while (nivel.size() > 1) {
            std::vector<PageId> arriba;
            std::vector<int> claves_arriba;
            for (std::size_t s = 0; s < nivel.size();) {
                const std::size_t cuantos = tomar(nivel.size() - s, por_nodo, min_hijos, max_hijos);
                PageId pid = asignar_pagina();
                char* p = pool.fijar(pid);
                VistaInterna nodo(p, max_hijos);
                nodo.cab->tipo = NODO_INTERNO;
                nodo.cab->reservado = 0;
                nodo.cab->num = static_cast<std::uint16_t>(cuantos - 1);
                nodo.cab->siguiente = PAGINA_NULA;
                for (std::size_t k = 0; k < cuantos; k++) nodo.hijos[k] = nivel[s + k];
                for (std::size_t k = 0; k + 1 < cuantos; k++) nodo.claves[k] = claves[s + k];
                pool.soltar(pid, true);

                if (!arriba.empty()) claves_arriba.push_back(claves[s - 1]);
                arriba.push_back(pid);
                s += cuantos;
            }
            nivel.swap(arriba);
            claves.swap(claves_arriba);
        }

        cab.raiz = nivel[0];
        cab.num_registros = n;
        guardar_cabecera();
    }

    // --- estado y verificación ---

    int altura() {
        int h = 1;
        PageId pid = cab.raiz;
        while (true) {
            char* p = pool.fijar(pid);
            CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
            if (c->tipo == NODO_HOJA) {
                pool.soltar(pid, false);
                return h;
            }
            PageId hijo = VistaInterna(p, max_hijos).hijos[0];
            pool.soltar(pid, false);
            pid = hijo;
            h++;
        }
    }

    void contar(PageId pid, long& internas, long& hojas) {
        char* p = pool.fijar(pid);
        CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
        if (c->tipo == NODO_HOJA) {
            hojas++;
            pool.soltar(pid, false);
            return;
        }
        internas++;
        VistaInterna nodo(p, max_hijos);
        std::vector<PageId> hijos(nodo.hijos, nodo.hijos + nodo.cab->num + 1);
        pool.soltar(pid, false);
        for (PageId h : hijos) contar(h, internas, hojas);
    }

    std::vector<int> claves_en_orden() {
        std::vector<int> claves;
        PageId pid = primera_hoja();
        while (pid != PAGINA_NULA) {
            char* p = pool.fijar(pid);
            VistaHoja hoja(p, tam);
            for (int i = 0; i < hoja.cab->num; i++) claves.push_back(hoja.clave(i));
            PageId siguiente = hoja.cab->siguiente;
            pool.soltar(pid, false);
            pid = siguiente;
        }
        return claves;
    }

    // comprueba que las claves del subárbol estén en [lo, hi), la ocupación y la profundidad
    bool verificar_nodo(PageId pid, int prof, long long lo, long long hi, bool es_raiz,
                        int& prof_hojas, long long& vistos) {
        char* p = pool.fijar(pid);
        CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
        bool ok = true;

        if (c->tipo == NODO_HOJA) {
            VistaHoja hoja(p, tam);
            int n = c->num;
            if (n > max_regs) ok = false;
            if (!es_raiz && n < min_regs) ok = false;
            for (int i = 0; i < n; i++) {
                long long clave = hoja.clave(i);
                if (clave < lo || clave >= hi) ok = false;
                if (i > 0 && clave <= hoja.clave(i - 1)) ok = false;
            }
            vistos += n;
            if (prof_hojas < 0) prof_hojas = prof;
            else if (prof_hojas != prof) ok = false;
            pool.soltar(pid, false);
            return ok;
        }

        VistaInterna nodo(p, max_hijos);
        int n = c->num;
        if (n > max_claves) ok = false;
        if (es_raiz ? n < 1 : n < min_claves) ok = false;
        std::vector<int> claves(nodo.claves, nodo.claves + n);
        std::vector<PageId> hijos(nodo.hijos, nodo.hijos + n + 1);
        pool.soltar(pid, false);

        for (int i = 0; i < n; i++) {
            long long clave = claves[i];
            if (clave < lo || clave >= hi) ok = false;
            if (i > 0 && clave <= claves[i - 1]) ok = false;
        }
        for (int i = 0; i <= n; i++) {
            long long sub_lo = i == 0 ? lo : claves[i - 1];
            long long sub_hi = i == n ? hi : claves[i];
            if (!verificar_nodo(hijos[i], prof + 1, sub_lo, sub_hi, false, prof_hojas, vistos)) {
                ok = false;
            }
        }
        return ok;
    }

    bool verificar_invariantes() {
        int prof_hojas = -1;
        long long vistos = 0;
        bool ok = verificar_nodo(cab.raiz, 0, LLONG_MIN, LLONG_MAX, true, prof_hojas, vistos);
        if (vistos != static_cast<long long>(cab.num_registros)) ok = false;

        const std::vector<int> cadena = claves_en_orden();
        if (cadena.size() != cab.num_registros) ok = false;
        for (std::size_t i = 1; i < cadena.size(); i++) {
            if (cadena[i] <= cadena[i - 1]) ok = false;
        }
        return ok;
    }
};

// --- fachada ---

BPlusAgrupado::BPlusAgrupado(const std::string& ruta, std::uint16_t tam_registro, bool truncar,
                             std::uint16_t max_regs_hoja, std::uint16_t max_hijos)
    : impl_(new Impl(ruta, tam_registro, truncar, max_regs_hoja, max_hijos)) {}

BPlusAgrupado::~BPlusAgrupado() { delete impl_; }

bool BPlusAgrupado::insertar_bytes(const void* registro) {
    if (registro == nullptr) throw std::invalid_argument("registro nulo");
    return impl_->insertar(static_cast<const char*>(registro));
}

bool BPlusAgrupado::buscar_bytes(int clave, void* salida) {
    if (salida == nullptr) throw std::invalid_argument("salida nula");
    return impl_->buscar(clave, salida);
}

void BPlusAgrupado::buscar_rango_bytes(int desde, int hasta, const Visitante& visitante) {
    if (desde > hasta) return;
    impl_->buscar_rango(desde, hasta, visitante);
}

bool BPlusAgrupado::eliminar(int clave) { return impl_->eliminar(clave); }

void BPlusAgrupado::cargar_masivo_bytes(const void* registros, std::size_t n, double factor_llenado) {
    if (n > 0 && registros == nullptr) throw std::invalid_argument("registros nulos");
    impl_->cargar_masivo(static_cast<const char*>(registros), n, factor_llenado);
}

void BPlusAgrupado::comprobar_tam(std::size_t tam) const {
    if (tam != impl_->cab.tam_registro) {
        throw std::invalid_argument("sizeof del registro (" + std::to_string(tam) +
                                    ") no coincide con tam_registro (" +
                                    std::to_string(impl_->cab.tam_registro) + ")");
    }
}

std::uint16_t BPlusAgrupado::tam_registro() const { return impl_->cab.tam_registro; }
std::uint16_t BPlusAgrupado::max_regs_hoja() const { return impl_->cab.max_regs_hoja; }
std::uint16_t BPlusAgrupado::max_hijos() const { return impl_->cab.max_hijos; }
int BPlusAgrupado::altura() { return impl_->altura(); }
long BPlusAgrupado::num_registros() const { return static_cast<long>(impl_->cab.num_registros); }
long BPlusAgrupado::tamano_en_disco() { return impl_->gestor.tamano_en_disco(); }

void BPlusAgrupado::contar_paginas(long& internas, long& hojas) {
    internas = 0;
    hojas = 0;
    impl_->contar(impl_->cab.raiz, internas, hojas);
}

void BPlusAgrupado::sincronizar() {
    impl_->guardar_cabecera();
    impl_->pool.vaciar();
}

void BPlusAgrupado::enfriar_cache() {
    impl_->guardar_cabecera();
    impl_->pool.limpiar();
    impl_->cargar_cabecera();
}

long BPlusAgrupado::paginas_leidas() const { return impl_->gestor.lecturas; }
long BPlusAgrupado::paginas_escritas() const { return impl_->gestor.escrituras; }
long BPlusAgrupado::aciertos_cache() const { return impl_->pool.aciertos; }
long BPlusAgrupado::fallos_cache() const { return impl_->pool.fallos; }
long BPlusAgrupado::desalojos_cache() const { return impl_->pool.desalojos; }
int BPlusAgrupado::paginas_en_cache() const { return impl_->pool.paginas_residentes(); }

std::vector<int> BPlusAgrupado::claves_en_orden() { return impl_->claves_en_orden(); }
bool BPlusAgrupado::verificar_invariantes() { return impl_->verificar_invariantes(); }

}  // namespace motor
