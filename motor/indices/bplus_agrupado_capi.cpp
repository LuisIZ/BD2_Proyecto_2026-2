// API en C (extern "C") para exponer el indice B+ AGRUPADO a la GUI de Python
// via ctypes. Sigue siendo C++ estandar, solo cambia la bandera de compilacion.
//
//   g++ -std=c++17 -O2 -shared -fPIC -o libagrupado.so bplus_agrupado_capi.cpp

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <set>
#include <vector>
#include "bplus_agrupado.hpp"

using namespace std;

namespace {

// Separador de campos en los volcados. Es un byte de control, asi que no
// puede aparecer dentro de un nombre de empresa ni de un pais.
const char US = '\x1f';

char* copiar_a_c(const string& texto) {
    char* copia = static_cast<char*>(malloc(texto.size() + 1));
    memcpy(copia, texto.c_str(), texto.size() + 1);
    return copia;
}

void escribir_registro(ostringstream& out, const Registro& r) {
    out << r.index << US << r.nombre << US << r.pais << US
        << r.industria << US << r.fundada << US << r.empleados;
}

// Reune las paginas hoja que se van a dibujar: arranca en la hoja que
// contiene 'desde' (o en la primera si desde < 0) y sigue la cadena.
void hojas_a_mostrar(BPlusAgrupado* arbol, int desde, int max_hojas,
                     vector<PageId>& orden, set<PageId>& conjunto) {
    PageId pid = arbol->cab.raiz;
    while (true) {                                   // bajar hasta el nivel hoja
        char* p = arbol->pool.fijar(pid);
        CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
        if (c->tipo == NODO_HOJA) { arbol->pool.soltar(pid, false); break; }
        VistaInterna nodo(p);
        int i = 0;
        if (desde >= 0)
            while (i < nodo.cab->num && desde >= nodo.claves[i]) i++;
        PageId hijo = nodo.hijos[i];
        arbol->pool.soltar(pid, false);
        pid = hijo;
    }
    while (pid != PAGINA_NULA && (int)orden.size() < max_hojas) {
        orden.push_back(pid);
        conjunto.insert(pid);
        char* p = arbol->pool.fijar(pid);
        PageId sig = reinterpret_cast<CabeceraPagina*>(p)->siguiente;
        arbol->pool.soltar(pid, false);
        pid = sig;
    }
}

// Recorre el arbol emitiendo los nodos internos (siempre, son pocos) y solo
// las hojas seleccionadas. Devuelve cuantas hojas totales tiene el subarbol.
long emitir(BPlusAgrupado* arbol, PageId pid, PageId padre,
            const set<PageId>& seleccionadas, ostringstream& out, long& internas) {
    char* p = arbol->pool.fijar(pid);
    CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);

    if (c->tipo == NODO_HOJA) {
        long total = 1;
        if (seleccionadas.count(pid)) {
            VistaHoja h(p);
            out << "H" << US << pid << US << padre << US << h.cab->siguiente
                << US << h.cab->num << "\n";
            for (int i = 0; i < h.cab->num; i++) {
                out << "R" << US << pid << US;
                escribir_registro(out, h.regs[i]);
                out << "\n";
            }
        }
        arbol->pool.soltar(pid, false);
        return total;
    }

    internas++;
    VistaInterna nodo(p);
    int n = nodo.cab->num;
    vector<int>    claves(nodo.claves, nodo.claves + n);
    vector<PageId> hijos(nodo.hijos, nodo.hijos + n + 1);
    arbol->pool.soltar(pid, false);

    out << "I" << US << pid << US << padre << US;
    for (int i = 0; i < n; i++) { if (i) out << ","; out << claves[i]; }
    out << US;
    for (size_t i = 0; i < hijos.size(); i++) { if (i) out << ","; out << hijos[i]; }
    out << "\n";

    long total = 0;
    for (PageId h : hijos) total += emitir(arbol, h, pid, seleccionadas, out, internas);
    return total;
}

} // namespace

extern "C" {

// ---------------------------------------------------------------- ciclo de vida

void* bpa_crear(const char* ruta, int truncar) {
    return new BPlusAgrupado(ruta, truncar != 0);
}

void bpa_destruir(void* h) {
    delete static_cast<BPlusAgrupado*>(h);
}

// ---------------------------------------------------------------- carga

// masivo = 1 -> lee el CSV a memoria, ordena y construye de abajo hacia arriba.
// masivo = 0 -> streaming: una fila a la vez, sin cargar el archivo.
long bpa_cargar_csv(void* h, const char* ruta, int masivo) {
    auto* arbol = static_cast<BPlusAgrupado*>(h);
    if (masivo) {
        vector<Registro> filas = leer_csv(ruta);
        if (filas.empty()) return 0;
        arbol->cargar_masivo(filas);
        return (long)filas.size();
    }
    return cargar_csv_streaming(*arbol, ruta);
}

void bpa_insertar(void* h, int index, const char* nombre, const char* pais,
                  const char* industria, int fundada, int empleados) {
    Registro r;
    memset(&r, 0, sizeof(Registro));
    r.index = index;
    r.fundada = fundada;
    r.empleados = empleados;
    copiar_campo(r.org_id,    sizeof(r.org_id),    "GUI" + to_string(index));
    copiar_campo(r.nombre,    sizeof(r.nombre),    nombre ? nombre : "");
    copiar_campo(r.pais,      sizeof(r.pais),      pais ? pais : "");
    copiar_campo(r.industria, sizeof(r.industria), industria ? industria : "");
    static_cast<BPlusAgrupado*>(h)->insertar(r);
}

// ---------------------------------------------------------------- consultas

// Devuelve 1 si encontro. En buf deja los campos separados por 0x1F.
int bpa_buscar(void* h, int clave, char* buf, int bufsize) {
    Registro r;
    if (!static_cast<BPlusAgrupado*>(h)->buscar(clave, r)) return 0;
    ostringstream out;
    escribir_registro(out, r);
    snprintf(buf, bufsize, "%s", out.str().c_str());
    return 1;
}

// Una linea por registro, campos separados por 0x1F. Liberar con bpa_liberar_str.
char* bpa_buscar_rango(void* h, int desde, int hasta, int max_filas) {
    auto* arbol = static_cast<BPlusAgrupado*>(h);
    vector<Registro> rs = arbol->buscar_rango(desde, hasta);
    ostringstream out;
    int limite = (max_filas > 0 && (int)rs.size() > max_filas) ? max_filas : (int)rs.size();
    out << "N" << US << rs.size() << "\n";
    for (int i = 0; i < limite; i++) {
        escribir_registro(out, rs[i]);
        out << "\n";
    }
    return copiar_a_c(out.str());
}

int bpa_eliminar(void* h, int clave) {
    return static_cast<BPlusAgrupado*>(h)->eliminar(clave) ? 1 : 0;
}

// ---------------------------------------------------------------- estadisticas

long bpa_num_registros(void* h)    { return static_cast<BPlusAgrupado*>(h)->num_registros(); }
int  bpa_altura(void* h)           { return static_cast<BPlusAgrupado*>(h)->altura(); }
long bpa_lecturas(void* h)         { return static_cast<BPlusAgrupado*>(h)->gestor.lecturas; }
long bpa_escrituras(void* h)       { return static_cast<BPlusAgrupado*>(h)->gestor.escrituras; }
long bpa_aciertos(void* h)         { return static_cast<BPlusAgrupado*>(h)->pool.aciertos; }
long bpa_fallos(void* h)           { return static_cast<BPlusAgrupado*>(h)->pool.fallos; }
long bpa_desalojos(void* h)        { return static_cast<BPlusAgrupado*>(h)->pool.desalojos; }
long bpa_tamano_en_disco(void* h)  { return static_cast<BPlusAgrupado*>(h)->gestor.tamano_en_disco(); }
int  bpa_paginas_residentes(void* h) { return static_cast<BPlusAgrupado*>(h)->pool.paginas_residentes(); }
void bpa_enfriar_cache(void* h)    { static_cast<BPlusAgrupado*>(h)->enfriar_cache(); }
void bpa_sincronizar(void* h)      { static_cast<BPlusAgrupado*>(h)->sincronizar(); }

// constantes del layout, para que la GUI las muestre sin adivinarlas
int bpa_tam_pagina()      { return TAM_PAGINA; }
int bpa_tam_registro()    { return (int)sizeof(Registro); }
int bpa_max_regs_hoja()   { return MAX_REGS_HOJA; }
int bpa_max_hijos()       { return MAX_HIJOS; }
int bpa_num_marcos()      { return NUM_MARCOS; }

void bpa_contar_paginas(void* h, long* internas, long* hojas) {
    static_cast<BPlusAgrupado*>(h)->contar_paginas(*internas, *hojas);
}

// ---------------------------------------------------------------- volcado

// Serializa el arbol para dibujarlo. Los nodos se identifican por su NUMERO
// DE PAGINA, que ya es unico. Formato, una linea por elemento:
//
//   I <US> pagina <US> pagina_padre <US> k1,k2,... <US> hijo1,hijo2,...
//   H <US> pagina <US> pagina_padre <US> pagina_siguiente <US> num_regs
//   R <US> pagina <US> index <US> nombre <US> pais <US> industria <US> fundada <US> empleados
//   T <US> internas <US> hojas_totales <US> hojas_mostradas
//
// Los internos salen siempre (son pocos). De las hojas solo salen hasta
// max_hojas, empezando por la que contiene 'desde' (si desde < 0, la primera).
// Asi la GUI puede dibujar un arbol de 100 000 registros sin ahogarse.
char* bpa_dump(void* h, int max_hojas, int desde) {
    auto* arbol = static_cast<BPlusAgrupado*>(h);
    if (max_hojas <= 0) max_hojas = 40;

    vector<PageId> orden;
    set<PageId> seleccionadas;
    hojas_a_mostrar(arbol, desde, max_hojas, orden, seleccionadas);

    ostringstream out;
    long internas = 0;
    long hojas_totales = emitir(arbol, arbol->cab.raiz, PAGINA_NULA,
                                seleccionadas, out, internas);
    out << "T" << US << internas << US << hojas_totales << US << orden.size() << "\n";
    return copiar_a_c(out.str());
}

void bpa_liberar_str(char* s) { free(s); }

} // extern "C"
