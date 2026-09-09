// ============================================================================
// Indice B+ AGRUPADO (clustered) sobre el campo "Index" del dataset.
//
// Lo que lo hace AGRUPADO esta en una frase: las hojas SON las paginas de
// datos. El registro completo vive dentro de la hoja, ordenado por la clave.
// No hay archivo de datos aparte, no hay RID, no hay salto extra.
//
// Y como las hojas son los datos, el arbol NO puede vivir en RAM: tenerlo en
// memoria seria tener la tabla entera en memoria. Por eso aqui:
//   - cada nodo es exactamente UNA pagina de disco de TAM_PAGINA bytes,
//   - los punteros son numeros de pagina (PageId), no direcciones de memoria,
//   - un BufferPool con reemplazo LRU garantiza un techo duro de NUM_MARCOS
//     paginas residentes, sin importar si el archivo pesa 25 MB o 25 GB.
//
// Compilar:  g++ -std=c++17 -O2 -o pruebas main.cpp
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>
#include <type_traits>

// ============================================================ parametros

using PageId = uint32_t;

const int    TAM_PAGINA   = 4096;      // igual al bloque del sistema de archivos
const PageId PAGINA_NULA  = 0;         // la pagina 0 es la cabecera del archivo
const int    NUM_MARCOS   = 64;        // techo duro de paginas en RAM (256 KB)

// ------------------------------------------------------------ modo demo
// Con la capacidad REAL (22 registros y 511 hijos en una pagina de 4096 B)
// hacen falta muchisimos datos para que el arbol gane altura, y en la GUI se
// ve una sola hoja. Estos dos topes la recortan A MANO para poder VER los
// splits, las fusiones y varios niveles con pocos registros.
//
//   0 = usar la capacidad real que sale del tamano de pagina.
//
// OJO al sustentar: en modo demo la pagina sigue ocupando TAM_PAGINA bytes en
// disco, o sea que se desperdicia espacio a proposito. Para medir costos de
// I/O reales hay que poner los dos en 0.
const int DEMO_REGS_HOJA = 3;          // registros por pagina hoja
const int DEMO_HIJOS     = 4;          // punteros por nodo interno -> 3 claves

const uint8_t NODO_INTERNO = 0;
const uint8_t NODO_HOJA    = 1;

// ============================================================ el registro

// Registro de tamano FIJO. Vive DENTRO de la hoja: eso es "agrupado".
// Los enteros van primero para que no aparezca relleno de alineacion.
struct Registro {
    int  index;              // <- CLAVE DE AGRUPAMIENTO (campo "Index" del CSV)
    int  fundada;            // Founded
    int  empleados;          // Number of employees
    char org_id[16];         // Organization Id  (max real: 15)
    char nombre[40];         // Name             (max real: 37)
    char pais[56];           // Country          (max real: 51)
    char industria[56];      // Industry         (max real: 51)
};

static_assert(std::is_trivially_copyable<Registro>::value,
              "el registro debe poder copiarse byte a byte a la pagina");

// ============================================================ layout de pagina

struct CabeceraPagina {
    uint8_t  tipo;           // NODO_INTERNO o NODO_HOJA
    uint16_t num;            // claves (interno) o registros (hoja)
    PageId   siguiente;      // solo hojas: siguiente hoja de la cadena
};

const int ESPACIO_UTIL = TAM_PAGINA - (int)sizeof(CabeceraPagina);

// Capacidad REAL, la que sale de la fisica de la pagina.
// Hoja  ("factor de bloqueo"): cuantos registros completos entran.
// Interno: h hijos y h-1 claves, o sea 4h + 4(h-1) <= ESPACIO_UTIL.
const int CAP_REGS_REAL  = ESPACIO_UTIL / (int)sizeof(Registro);
const int CAP_HIJOS_REAL = (ESPACIO_UTIL + (int)sizeof(int))
                           / ((int)sizeof(PageId) + (int)sizeof(int));

// Capacidad EFECTIVA: la real, salvo que el modo demo la recorte.
const int MAX_REGS_HOJA  = (DEMO_REGS_HOJA > 0 && DEMO_REGS_HOJA < CAP_REGS_REAL)
                           ? DEMO_REGS_HOJA : CAP_REGS_REAL;
const int MIN_REGS_HOJA  = MAX_REGS_HOJA / 2;

const int MAX_HIJOS      = (DEMO_HIJOS > 0 && DEMO_HIJOS < CAP_HIJOS_REAL)
                           ? DEMO_HIJOS : CAP_HIJOS_REAL;
const int MAX_CLAVES_INT = MAX_HIJOS - 1;
const int MIN_CLAVES_INT = MAX_CLAVES_INT / 2;

// Por debajo de estos topes el split deja nodos degenerados (sin claves).
static_assert(MAX_REGS_HOJA >= 2, "hacen falta al menos 2 registros por hoja");
static_assert(MAX_HIJOS >= 3, "hacen falta al menos 3 hijos por nodo interno");
static_assert((int)sizeof(CabeceraPagina) + (int)sizeof(Registro) * MAX_REGS_HOJA
              <= TAM_PAGINA, "la hoja no cabe en la pagina");
static_assert((int)sizeof(CabeceraPagina) + (int)sizeof(PageId) * MAX_HIJOS
              + (int)sizeof(int) * MAX_CLAVES_INT <= TAM_PAGINA,
              "el nodo interno no cabe en la pagina");

// Vistas: no copian nada, solo interpretan los bytes crudos de la pagina.
//
//   HOJA      [cabecera][registro 0][registro 1]...
//   INTERNO   [cabecera][hijo 0..MAX_HIJOS-1][clave 0..MAX_CLAVES_INT-1]
struct VistaHoja {
    CabeceraPagina* cab;
    Registro*       regs;
    explicit VistaHoja(char* p)
        : cab(reinterpret_cast<CabeceraPagina*>(p)),
          regs(reinterpret_cast<Registro*>(p + sizeof(CabeceraPagina))) {}
};

struct VistaInterna {
    CabeceraPagina* cab;
    PageId*         hijos;     // cab->num + 1 entradas
    int*            claves;    // cab->num entradas
    explicit VistaInterna(char* p)
        : cab(reinterpret_cast<CabeceraPagina*>(p)),
          hijos(reinterpret_cast<PageId*>(p + sizeof(CabeceraPagina))),
          claves(reinterpret_cast<int*>(p + sizeof(CabeceraPagina)
                                        + sizeof(PageId) * MAX_HIJOS)) {}
};

// Cabecera del archivo completo, guardada en la pagina 0.
struct CabeceraArchivo {
    PageId   raiz;
    PageId   libres;          // cabeza de la lista de paginas libres
    uint64_t num_registros;
    uint32_t magico;
};

// ============================================================ disco

// Lo unico que toca el archivo. Lee y escribe SIEMPRE paginas completas.
class GestorPaginas {
public:
    std::string  ruta;
    std::fstream archivo;
    PageId       num_paginas = 0;
    long         lecturas = 0;      // I/O real contado
    long         escrituras = 0;

    GestorPaginas(const std::string& r, bool truncar) : ruta(r) {
        if (truncar) {
            std::ofstream limpiar(ruta, std::ios::binary | std::ios::trunc);
        }
        archivo.open(ruta, std::ios::binary | std::ios::in | std::ios::out);
        if (!archivo.is_open()) {
            std::ofstream crear(ruta, std::ios::binary);   // no existia
            crear.close();
            archivo.open(ruta, std::ios::binary | std::ios::in | std::ios::out);
        }
        if (!archivo.is_open())
            throw std::runtime_error("no se pudo abrir el archivo: " + ruta);
        archivo.seekg(0, std::ios::end);
        num_paginas = (PageId)(archivo.tellg() / TAM_PAGINA);
    }

    ~GestorPaginas() { if (archivo.is_open()) archivo.close(); }

    void leer(PageId p, char* destino) {
        archivo.clear();
        archivo.seekg((std::streamoff)p * TAM_PAGINA);
        archivo.read(destino, TAM_PAGINA);
        std::streamsize leidos = archivo.gcount();
        if (leidos < TAM_PAGINA) {                 // pagina recien creada
            std::memset(destino + leidos, 0, TAM_PAGINA - leidos);
            archivo.clear();
        }
        lecturas++;
    }

    void escribir(PageId p, const char* origen) {
        archivo.clear();
        archivo.seekp((std::streamoff)p * TAM_PAGINA);
        archivo.write(origen, TAM_PAGINA);
        escrituras++;
    }

    // Hace crecer el archivo en una pagina y devuelve su numero.
    PageId asignar() {
        PageId p = num_paginas++;
        std::vector<char> vacia(TAM_PAGINA, 0);
        escribir(p, vacia.data());
        return p;
    }

    long tamano_en_disco() {
        archivo.clear();
        archivo.flush();
        archivo.seekg(0, std::ios::end);
        return (long)archivo.tellg();
    }
};

// ============================================================ buffer pool

// La pieza que hace real el "no cargar todo en RAM": nunca hay mas de
// NUM_MARCOS paginas residentes. Quien necesite una pagina la FIJA (pin),
// la usa, y la SUELTA (unpin). Una pagina fijada no puede ser desalojada.
class BufferPool {
public:
    GestorPaginas& gestor;

    alignas(8) char marcos[NUM_MARCOS][TAM_PAGINA];
    PageId pagina_de[NUM_MARCOS];
    int    fijaciones[NUM_MARCOS];
    bool   sucio[NUM_MARCOS];
    bool   ocupado[NUM_MARCOS];

    std::unordered_map<PageId, int> tabla;      // pagina -> marco
    std::list<int> lru;                         // frente = mas reciente
    std::list<int>::iterator pos_lru[NUM_MARCOS];

    long aciertos = 0;
    long fallos = 0;
    long desalojos = 0;

    explicit BufferPool(GestorPaginas& g) : gestor(g) {
        for (int m = 0; m < NUM_MARCOS; m++) {
            pagina_de[m] = PAGINA_NULA;
            fijaciones[m] = 0;
            sucio[m] = false;
            ocupado[m] = false;
        }
    }

    char* fijar(PageId p) {
        auto it = tabla.find(p);
        if (it != tabla.end()) {                 // ya esta en RAM
            int m = it->second;
            fijaciones[m]++;
            lru.erase(pos_lru[m]);
            lru.push_front(m);
            pos_lru[m] = lru.begin();
            aciertos++;
            return marcos[m];
        }
        fallos++;                                // hay que ir al disco
        int m = conseguir_marco();
        gestor.leer(p, marcos[m]);
        pagina_de[m] = p;
        ocupado[m] = true;
        sucio[m] = false;
        fijaciones[m] = 1;
        tabla[p] = m;
        lru.push_front(m);
        pos_lru[m] = lru.begin();
        return marcos[m];
    }

    void soltar(PageId p, bool modificada) {
        auto it = tabla.find(p);
        if (it == tabla.end()) return;
        int m = it->second;
        if (modificada) sucio[m] = true;
        if (fijaciones[m] > 0) fijaciones[m]--;
    }

    // Baja a disco todo lo modificado. Se llama al cerrar.
    void vaciar() {
        for (int m = 0; m < NUM_MARCOS; m++) {
            if (ocupado[m] && sucio[m]) {
                gestor.escribir(pagina_de[m], marcos[m]);
                sucio[m] = false;
            }
        }
        gestor.archivo.flush();
    }

    int paginas_residentes() const {
        return (int)tabla.size();
    }

    // Baja lo sucio y suelta TODOS los marcos: deja el pool como recien
    // abierto. Sirve para medir el costo real de una consulta "en frio".
    void limpiar() {
        for (int m = 0; m < NUM_MARCOS; m++)
            if (ocupado[m] && fijaciones[m] > 0)
                throw std::runtime_error("no se puede limpiar: hay paginas fijadas");
        vaciar();
        for (int m = 0; m < NUM_MARCOS; m++) {
            ocupado[m] = false;
            sucio[m] = false;
            fijaciones[m] = 0;
            pagina_de[m] = PAGINA_NULA;
        }
        tabla.clear();
        lru.clear();
    }

private:
    int conseguir_marco() {
        for (int m = 0; m < NUM_MARCOS; m++)
            if (!ocupado[m]) return m;

        // desalojar el menos usado recientemente que no este fijado
        for (auto it = lru.end(); it != lru.begin(); ) {
            --it;
            int m = *it;
            if (fijaciones[m] == 0) {
                if (sucio[m]) gestor.escribir(pagina_de[m], marcos[m]);
                tabla.erase(pagina_de[m]);
                lru.erase(it);
                ocupado[m] = false;
                sucio[m] = false;
                desalojos++;
                return m;
            }
        }
        throw std::runtime_error("buffer pool lleno: todas las paginas fijadas");
    }
};

// ============================================================ el arbol

class BPlusAgrupado {
public:
    GestorPaginas   gestor;
    BufferPool      pool;
    CabeceraArchivo cab;

    explicit BPlusAgrupado(const std::string& ruta, bool truncar = true)
        : gestor(ruta, truncar), pool(gestor) {
        if (gestor.num_paginas == 0) {
            gestor.asignar();                        // pagina 0 = cabecera
            PageId r = gestor.asignar();             // pagina 1 = raiz (hoja vacia)
            char* p = pool.fijar(r);
            VistaHoja h(p);
            h.cab->tipo = NODO_HOJA;
            h.cab->num = 0;
            h.cab->siguiente = PAGINA_NULA;
            pool.soltar(r, true);
            cab.raiz = r;
            cab.libres = PAGINA_NULA;
            cab.num_registros = 0;
            cab.magico = 0x42504C53;                 // "BPLS"
            guardar_cabecera();
        } else {
            cargar_cabecera();
        }
    }

    ~BPlusAgrupado() {
        try { guardar_cabecera(); pool.vaciar(); } catch (...) {}
    }

    BPlusAgrupado(const BPlusAgrupado&) = delete;
    BPlusAgrupado& operator=(const BPlusAgrupado&) = delete;

    // -------------------------------------------------------- consultas

    bool buscar(int clave, Registro& salida) {
        PageId pid = bajar_hasta_hoja(clave);
        char* p = pool.fijar(pid);
        VistaHoja hoja(p);
        int i = pos_en_hoja(hoja.regs, hoja.cab->num, clave);
        bool ok = (i < hoja.cab->num && hoja.regs[i].index == clave);
        if (ok) salida = hoja.regs[i];
        pool.soltar(pid, false);
        return ok;
    }

    // La ventaja del agrupado: se baja UNA vez y despues se avanza por la
    // cadena de hojas, que son las paginas de datos, en orden y contiguas.
    std::vector<Registro> buscar_rango(int desde, int hasta) {
        std::vector<Registro> salida;
        PageId pid = bajar_hasta_hoja(desde);
        while (pid != PAGINA_NULA) {
            char* p = pool.fijar(pid);
            VistaHoja hoja(p);
            PageId sig = hoja.cab->siguiente;
            bool terminar = false;
            for (int i = 0; i < hoja.cab->num; i++) {
                if (hoja.regs[i].index > hasta) { terminar = true; break; }
                if (hoja.regs[i].index >= desde) salida.push_back(hoja.regs[i]);
            }
            pool.soltar(pid, false);
            if (terminar) break;
            pid = sig;
        }
        return salida;
    }

    int altura() {
        int h = 1;
        PageId pid = cab.raiz;
        while (true) {
            char* p = pool.fijar(pid);
            CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
            if (c->tipo == NODO_HOJA) { pool.soltar(pid, false); break; }
            PageId hijo = VistaInterna(p).hijos[0];
            pool.soltar(pid, false);
            pid = hijo;
            h++;
        }
        return h;
    }

    long num_registros() const { return (long)cab.num_registros; }

    // -------------------------------------------------------- insercion

    void insertar(const Registro& r) {
        int    clave_sube = 0;
        PageId pagina_nueva = PAGINA_NULA;
        if (insertar_en(cab.raiz, r, clave_sube, pagina_nueva)) {
            // la raiz se partio: el arbol crece un nivel hacia arriba
            PageId nueva_raiz = asignar_pagina();
            char* p = pool.fijar(nueva_raiz);
            VistaInterna nodo(p);
            nodo.cab->tipo = NODO_INTERNO;
            nodo.cab->num = 1;
            nodo.cab->siguiente = PAGINA_NULA;
            nodo.hijos[0]  = cab.raiz;
            nodo.claves[0] = clave_sube;
            nodo.hijos[1]  = pagina_nueva;
            pool.soltar(nueva_raiz, true);
            cab.raiz = nueva_raiz;
        }
        cab.num_registros++;
        guardar_cabecera();
    }

    // -------------------------------------------------------- borrado

    bool eliminar(int clave) {
        bool encontrado = false;
        eliminar_en(cab.raiz, clave, encontrado);
        if (!encontrado) return false;

        cab.num_registros--;
        // si la raiz interna se quedo sin claves, el arbol encoge
        char* p = pool.fijar(cab.raiz);
        CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
        if (c->tipo == NODO_INTERNO && c->num == 0) {
            PageId unico = VistaInterna(p).hijos[0];
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

    // -------------------------------------------------------- carga masiva

    // Para volumenes grandes, insertar uno por uno es lento y deja el arbol a
    // media ocupacion. Aqui se ordena una vez y se construye de abajo hacia
    // arriba con las paginas llenas al FACTOR_LLENADO indicado.
    void cargar_masivo(std::vector<Registro>& regs, double factor_llenado = 0.9) {
        if (regs.empty()) return;
        std::sort(regs.begin(), regs.end(),
                  [](const Registro& a, const Registro& b) { return a.index < b.index; });

        // el arbol venia vacio: su hoja raiz queda huerfana, se recicla
        if (cab.num_registros == 0) liberar_pagina(cab.raiz);

        int por_hoja = std::max(1, (int)(MAX_REGS_HOJA * factor_llenado));
        std::vector<PageId> nivel;      // paginas del nivel que se acaba de crear
        std::vector<int>    claves;     // claves[j] = primera clave de nivel[j+1]

        PageId anterior = PAGINA_NULA;
        for (size_t i = 0; i < regs.size(); i += por_hoja) {
            int n = (int)std::min((size_t)por_hoja, regs.size() - i);
            PageId pid = asignar_pagina();
            char* p = pool.fijar(pid);
            VistaHoja h(p);
            h.cab->tipo = NODO_HOJA;
            h.cab->num = (uint16_t)n;
            h.cab->siguiente = PAGINA_NULA;
            std::memcpy(h.regs, &regs[i], sizeof(Registro) * n);
            pool.soltar(pid, true);

            if (!nivel.empty()) claves.push_back(regs[i].index);
            nivel.push_back(pid);

            if (anterior != PAGINA_NULA) {          // encadenar hojas
                char* pa = pool.fijar(anterior);
                reinterpret_cast<CabeceraPagina*>(pa)->siguiente = pid;
                pool.soltar(anterior, true);
            }
            anterior = pid;
        }

        // niveles internos, de abajo hacia arriba
        int por_nodo = std::max(2, (int)(MAX_HIJOS * factor_llenado));
        while (nivel.size() > 1) {
            std::vector<PageId> arriba;
            std::vector<int>    claves_arriba;
            size_t s = 0;
            while (s < nivel.size()) {
                size_t n = std::min((size_t)por_nodo, nivel.size() - s);
                if (nivel.size() - (s + n) == 1 && n > 2) n--;   // no dejar grupos de 1

                PageId pid = asignar_pagina();
                char* p = pool.fijar(pid);
                VistaInterna nodo(p);
                nodo.cab->tipo = NODO_INTERNO;
                nodo.cab->num = (uint16_t)(n - 1);
                nodo.cab->siguiente = PAGINA_NULA;
                for (size_t k = 0; k < n; k++) nodo.hijos[k] = nivel[s + k];
                for (size_t k = 0; k + 1 < n; k++) nodo.claves[k] = claves[s + k];
                pool.soltar(pid, true);

                if (!arriba.empty()) claves_arriba.push_back(claves[s - 1]);
                arriba.push_back(pid);
                s += n;
            }
            nivel = arriba;
            claves = claves_arriba;
        }

        cab.raiz = nivel[0];
        cab.num_registros = regs.size();
        guardar_cabecera();
    }

    // -------------------------------------------------------- utilidades

    void sincronizar() { guardar_cabecera(); pool.vaciar(); }

    // Deja el buffer pool vacio, como si se acabara de abrir el archivo.
    void enfriar_cache() { guardar_cabecera(); pool.limpiar(); cargar_cabecera(); }

    // Recorre la cadena de hojas de punta a punta. Sirve para verificar que
    // los datos quedaron fisicamente ORDENADOS, que es lo que define al
    // indice agrupado.
    std::vector<int> recorrer_cadena() {
        std::vector<int> claves;
        PageId pid = cab.raiz;
        while (true) {                                // bajar por la izquierda
            char* p = pool.fijar(pid);
            CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
            if (c->tipo == NODO_HOJA) { pool.soltar(pid, false); break; }
            PageId hijo = VistaInterna(p).hijos[0];
            pool.soltar(pid, false);
            pid = hijo;
        }
        while (pid != PAGINA_NULA) {
            char* p = pool.fijar(pid);
            VistaHoja h(p);
            for (int i = 0; i < h.cab->num; i++) claves.push_back(h.regs[i].index);
            PageId sig = h.cab->siguiente;
            pool.soltar(pid, false);
            pid = sig;
        }
        return claves;
    }

    // Comprueba que todas las hojas cuelgan a la misma profundidad.
    bool hojas_a_la_misma_altura() {
        std::vector<int> prof;
        profundidades(cab.raiz, 0, prof);
        if (prof.empty()) return true;
        return *std::min_element(prof.begin(), prof.end())
            == *std::max_element(prof.begin(), prof.end());
    }

    void contar_paginas(long& internas, long& hojas) {
        internas = 0; hojas = 0;
        contar(cab.raiz, internas, hojas);
    }

private:
    // ------------------------------------------------ cabecera y paginas

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

    // Reutiliza una pagina liberada antes de hacer crecer el archivo.
    PageId asignar_pagina() {
        PageId p;
        if (cab.libres != PAGINA_NULA) {
            p = cab.libres;
            char* buf = pool.fijar(p);
            std::memcpy(&cab.libres, buf, sizeof(PageId));   // siguiente libre
            std::memset(buf, 0, TAM_PAGINA);
            pool.soltar(p, true);
        } else {
            p = gestor.asignar();
        }
        guardar_cabecera();
        return p;
    }

    void liberar_pagina(PageId p) {
        char* buf = pool.fijar(p);
        std::memset(buf, 0, TAM_PAGINA);
        std::memcpy(buf, &cab.libres, sizeof(PageId));       // encadenar
        pool.soltar(p, true);
        cab.libres = p;
        guardar_cabecera();
    }

    // ------------------------------------------------ busquedas binarias

    // primera posicion con index >= clave
    static int pos_en_hoja(const Registro* regs, int n, int clave) {
        int lo = 0, hi = n;
        while (lo < hi) {
            int med = (lo + hi) / 2;
            if (regs[med].index < clave) lo = med + 1; else hi = med;
        }
        return lo;
    }

    // indice del hijo por el que hay que bajar
    static int hijo_para(const int* claves, int n, int clave) {
        int lo = 0, hi = n;
        while (lo < hi) {
            int med = (lo + hi) / 2;
            if (clave >= claves[med]) lo = med + 1; else hi = med;
        }
        return lo;
    }

    PageId bajar_hasta_hoja(int clave) {
        PageId pid = cab.raiz;
        while (true) {
            char* p = pool.fijar(pid);
            CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
            if (c->tipo == NODO_HOJA) { pool.soltar(pid, false); return pid; }
            VistaInterna nodo(p);
            PageId hijo = nodo.hijos[hijo_para(nodo.claves, nodo.cab->num, clave)];
            pool.soltar(pid, false);
            pid = hijo;
        }
    }

    // ------------------------------------------------ insercion recursiva

    // Devuelve true si el nodo se partio; entonces clave_sube y pagina_nueva
    // traen lo que el padre debe insertar.
    bool insertar_en(PageId pid, const Registro& r, int& clave_sube, PageId& pagina_nueva) {
        char* p = pool.fijar(pid);
        uint8_t tipo = reinterpret_cast<CabeceraPagina*>(p)->tipo;

        // ---------------------------------------------------- caso hoja
        if (tipo == NODO_HOJA) {
            VistaHoja hoja(p);
            int n = hoja.cab->num;
            int i = pos_en_hoja(hoja.regs, n, r.index);

            if (n < MAX_REGS_HOJA) {                    // cabe: insercion directa
                std::memmove(&hoja.regs[i + 1], &hoja.regs[i],
                             sizeof(Registro) * (n - i));
                hoja.regs[i] = r;
                hoja.cab->num = (uint16_t)(n + 1);
                pool.soltar(pid, true);
                return false;
            }

            // no cabe: se arma la secuencia completa aparte y se parte en dos
            std::vector<Registro> temp(n + 1);
            std::memcpy(temp.data(), hoja.regs, sizeof(Registro) * i);
            temp[i] = r;
            std::memcpy(temp.data() + i + 1, hoja.regs + i, sizeof(Registro) * (n - i));

            int total = n + 1;
            int mitad = total / 2;

            PageId nueva = asignar_pagina();
            char* pn = pool.fijar(nueva);
            VistaHoja hn(pn);
            hn.cab->tipo = NODO_HOJA;
            hn.cab->num = (uint16_t)(total - mitad);
            hn.cab->siguiente = hoja.cab->siguiente;
            std::memcpy(hn.regs, temp.data() + mitad, sizeof(Registro) * (total - mitad));

            hoja.cab->num = (uint16_t)mitad;
            hoja.cab->siguiente = nueva;
            std::memcpy(hoja.regs, temp.data(), sizeof(Registro) * mitad);

            clave_sube   = hn.regs[0].index;    // COPIA: la clave sigue abajo
            pagina_nueva = nueva;
            pool.soltar(nueva, true);
            pool.soltar(pid, true);
            return true;
        }

        // ------------------------------------------------- caso interno
        VistaInterna nodo(p);
        int i = hijo_para(nodo.claves, nodo.cab->num, r.index);
        PageId hijo = nodo.hijos[i];

        int    csube = 0;
        PageId pnueva = PAGINA_NULA;
        bool hubo_split = insertar_en(hijo, r, csube, pnueva);   // p sigue fijada
        if (!hubo_split) { pool.soltar(pid, false); return false; }

        int n = nodo.cab->num;
        if (n < MAX_CLAVES_INT) {                       // cabe el separador
            std::memmove(&nodo.claves[i + 1], &nodo.claves[i], sizeof(int) * (n - i));
            std::memmove(&nodo.hijos[i + 2], &nodo.hijos[i + 1], sizeof(PageId) * (n - i));
            nodo.claves[i]    = csube;
            nodo.hijos[i + 1] = pnueva;
            nodo.cab->num = (uint16_t)(n + 1);
            pool.soltar(pid, true);
            return false;
        }

        // el interno tambien se desborda: se parte
        std::vector<int>    tclaves(n + 1);
        std::vector<PageId> thijos(n + 2);
        std::memcpy(tclaves.data(), nodo.claves, sizeof(int) * i);
        tclaves[i] = csube;
        std::memcpy(tclaves.data() + i + 1, nodo.claves + i, sizeof(int) * (n - i));
        std::memcpy(thijos.data(), nodo.hijos, sizeof(PageId) * (i + 1));
        thijos[i + 1] = pnueva;
        std::memcpy(thijos.data() + i + 2, nodo.hijos + i + 1, sizeof(PageId) * (n - i));

        int total = n + 1;
        int mitad = total / 2;
        int sube  = tclaves[mitad];        // MUEVE: desaparece de abajo

        PageId nuevo = asignar_pagina();
        char* pn = pool.fijar(nuevo);
        VistaInterna nn(pn);
        nn.cab->tipo = NODO_INTERNO;
        nn.cab->siguiente = PAGINA_NULA;
        nn.cab->num = (uint16_t)(total - mitad - 1);
        std::memcpy(nn.claves, tclaves.data() + mitad + 1, sizeof(int) * nn.cab->num);
        std::memcpy(nn.hijos,  thijos.data()  + mitad + 1, sizeof(PageId) * (nn.cab->num + 1));

        nodo.cab->num = (uint16_t)mitad;
        std::memcpy(nodo.claves, tclaves.data(), sizeof(int) * mitad);
        std::memcpy(nodo.hijos,  thijos.data(),  sizeof(PageId) * (mitad + 1));

        clave_sube   = sube;
        pagina_nueva = nuevo;
        pool.soltar(nuevo, true);
        pool.soltar(pid, true);
        return true;
    }

    // ------------------------------------------------ borrado recursivo

    // Devuelve true si pid quedo por debajo de su minimo de ocupacion.
    bool eliminar_en(PageId pid, int clave, bool& encontrado) {
        char* p = pool.fijar(pid);
        uint8_t tipo = reinterpret_cast<CabeceraPagina*>(p)->tipo;

        if (tipo == NODO_HOJA) {
            VistaHoja hoja(p);
            int n = hoja.cab->num;
            int i = pos_en_hoja(hoja.regs, n, clave);
            if (i < n && hoja.regs[i].index == clave) {
                std::memmove(&hoja.regs[i], &hoja.regs[i + 1],
                             sizeof(Registro) * (n - i - 1));
                hoja.cab->num = (uint16_t)(n - 1);
                encontrado = true;
                bool poco = hoja.cab->num < MIN_REGS_HOJA;
                pool.soltar(pid, true);
                return poco;
            }
            pool.soltar(pid, false);
            return false;
        }

        VistaInterna nodo(p);
        int i = hijo_para(nodo.claves, nodo.cab->num, clave);
        PageId hijo = nodo.hijos[i];
        bool hijo_bajo = eliminar_en(hijo, clave, encontrado);
        if (!hijo_bajo) { pool.soltar(pid, false); return false; }

        reparar_hijo(p, i);
        bool poco = nodo.cab->num < MIN_CLAVES_INT;
        pool.soltar(pid, true);
        return poco;
    }

    // El hijo i del padre quedo corto: primero se intenta que un hermano le
    // preste, y solo si nadie puede, se fusiona.
    void reparar_hijo(char* p_padre, int i) {
        VistaInterna padre(p_padre);
        PageId pid_hijo = padre.hijos[i];
        char* ph = pool.fijar(pid_hijo);
        bool es_hoja = reinterpret_cast<CabeceraPagina*>(ph)->tipo == NODO_HOJA;

        if (i > 0) {                                   // hermano izquierdo
            PageId pid_izq = padre.hijos[i - 1];
            char* pi = pool.fijar(pid_izq);
            int n_izq = reinterpret_cast<CabeceraPagina*>(pi)->num;
            int minimo = es_hoja ? MIN_REGS_HOJA : MIN_CLAVES_INT;
            if (n_izq > minimo) {
                prestar_de_izquierda(padre, i, pi, ph, es_hoja);
                pool.soltar(pid_izq, true);
                pool.soltar(pid_hijo, true);
                return;
            }
            pool.soltar(pid_izq, false);
        }

        if (i < padre.cab->num) {                      // hermano derecho
            PageId pid_der = padre.hijos[i + 1];
            char* pd = pool.fijar(pid_der);
            int n_der = reinterpret_cast<CabeceraPagina*>(pd)->num;
            int minimo = es_hoja ? MIN_REGS_HOJA : MIN_CLAVES_INT;
            if (n_der > minimo) {
                prestar_de_derecha(padre, i, pd, ph, es_hoja);
                pool.soltar(pid_der, true);
                pool.soltar(pid_hijo, true);
                return;
            }
            pool.soltar(pid_der, false);
        }

        pool.soltar(pid_hijo, false);
        if (i > 0) fusionar(padre, i - 1, es_hoja);    // fusionar con el izquierdo
        else       fusionar(padre, i, es_hoja);        // fusionar con el derecho
    }

    void prestar_de_izquierda(VistaInterna& padre, int i, char* pi, char* ph, bool es_hoja) {
        if (es_hoja) {
            VistaHoja izq(pi), hijo(ph);
            int ni = izq.cab->num, nh = hijo.cab->num;
            std::memmove(&hijo.regs[1], &hijo.regs[0], sizeof(Registro) * nh);
            hijo.regs[0] = izq.regs[ni - 1];
            hijo.cab->num = (uint16_t)(nh + 1);
            izq.cab->num  = (uint16_t)(ni - 1);
            padre.claves[i - 1] = hijo.regs[0].index;      // separador recalculado
        } else {
            VistaInterna izq(pi), hijo(ph);
            int ni = izq.cab->num, nh = hijo.cab->num;
            std::memmove(&hijo.claves[1], &hijo.claves[0], sizeof(int) * nh);
            std::memmove(&hijo.hijos[1],  &hijo.hijos[0],  sizeof(PageId) * (nh + 1));
            hijo.claves[0] = padre.claves[i - 1];          // rotacion a la derecha
            hijo.hijos[0]  = izq.hijos[ni];
            padre.claves[i - 1] = izq.claves[ni - 1];
            hijo.cab->num = (uint16_t)(nh + 1);
            izq.cab->num  = (uint16_t)(ni - 1);
        }
    }

    void prestar_de_derecha(VistaInterna& padre, int i, char* pd, char* ph, bool es_hoja) {
        if (es_hoja) {
            VistaHoja der(pd), hijo(ph);
            int nd = der.cab->num, nh = hijo.cab->num;
            hijo.regs[nh] = der.regs[0];
            std::memmove(&der.regs[0], &der.regs[1], sizeof(Registro) * (nd - 1));
            hijo.cab->num = (uint16_t)(nh + 1);
            der.cab->num  = (uint16_t)(nd - 1);
            padre.claves[i] = der.regs[0].index;           // separador recalculado
        } else {
            VistaInterna der(pd), hijo(ph);
            int nd = der.cab->num, nh = hijo.cab->num;
            hijo.claves[nh]    = padre.claves[i];          // rotacion a la izquierda
            hijo.hijos[nh + 1] = der.hijos[0];
            padre.claves[i] = der.claves[0];
            std::memmove(&der.claves[0], &der.claves[1], sizeof(int) * (nd - 1));
            std::memmove(&der.hijos[0],  &der.hijos[1],  sizeof(PageId) * nd);
            hijo.cab->num = (uint16_t)(nh + 1);
            der.cab->num  = (uint16_t)(nd - 1);
        }
    }

    // Junta los hijos s y s+1 del padre en uno solo y quita el separador s.
    void fusionar(VistaInterna& padre, int s, bool es_hoja) {
        PageId pid_izq = padre.hijos[s];
        PageId pid_der = padre.hijos[s + 1];
        char* pi = pool.fijar(pid_izq);
        char* pd = pool.fijar(pid_der);

        if (es_hoja) {
            VistaHoja izq(pi), der(pd);
            std::memcpy(&izq.regs[izq.cab->num], der.regs,
                        sizeof(Registro) * der.cab->num);
            izq.cab->num = (uint16_t)(izq.cab->num + der.cab->num);
            izq.cab->siguiente = der.cab->siguiente;       // recoser la cadena
        } else {
            VistaInterna izq(pi), der(pd);
            izq.claves[izq.cab->num] = padre.claves[s];    // el separador BAJA
            std::memcpy(&izq.claves[izq.cab->num + 1], der.claves,
                        sizeof(int) * der.cab->num);
            std::memcpy(&izq.hijos[izq.cab->num + 1], der.hijos,
                        sizeof(PageId) * (der.cab->num + 1));
            izq.cab->num = (uint16_t)(izq.cab->num + der.cab->num + 1);
        }

        pool.soltar(pid_izq, true);
        pool.soltar(pid_der, false);
        liberar_pagina(pid_der);                           // la pagina se recicla

        int n = padre.cab->num;
        std::memmove(&padre.claves[s], &padre.claves[s + 1], sizeof(int) * (n - s - 1));
        std::memmove(&padre.hijos[s + 1], &padre.hijos[s + 2], sizeof(PageId) * (n - s - 1));
        padre.cab->num = (uint16_t)(n - 1);
    }

    // ------------------------------------------------ recorridos auxiliares

    void profundidades(PageId pid, int d, std::vector<int>& prof) {
        char* p = pool.fijar(pid);
        CabeceraPagina* c = reinterpret_cast<CabeceraPagina*>(p);
        if (c->tipo == NODO_HOJA) {
            prof.push_back(d);
            pool.soltar(pid, false);
            return;
        }
        VistaInterna nodo(p);
        std::vector<PageId> hijos(nodo.hijos, nodo.hijos + nodo.cab->num + 1);
        pool.soltar(pid, false);
        for (PageId h : hijos) profundidades(h, d + 1, prof);
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
        VistaInterna nodo(p);
        std::vector<PageId> hijos(nodo.hijos, nodo.hijos + nodo.cab->num + 1);
        pool.soltar(pid, false);
        for (PageId h : hijos) contar(h, internas, hojas);
    }
};

// ============================================================ CSV

inline void copiar_campo(char* destino, size_t tam, const std::string& origen) {
    std::memset(destino, 0, tam);
    std::memcpy(destino, origen.data(), std::min(origen.size(), tam - 1));
}

// El CSV viene con finales de linea de Windows (CRLF). getline corta en \n y
// deja el \r pegado al ultimo campo, asi que hay que quitarlo o el nombre de
// la ultima columna nunca coincide.
inline void quitar_cr(std::string& linea) {
    if (!linea.empty() && linea.back() == '\r') linea.pop_back();
}

// Separa respetando comillas dobles (hay nombres con comas adentro).
inline std::vector<std::string> partir_linea_csv(const std::string& linea) {
    std::vector<std::string> campos;
    std::string actual;
    bool entre_comillas = false;
    for (char c : linea) {
        if (c == '"') entre_comillas = !entre_comillas;
        else if (c == ',' && !entre_comillas) { campos.push_back(actual); actual.clear(); }
        else actual += c;
    }
    campos.push_back(actual);
    return campos;
}

struct ColumnasCsv {
    int index = -1, org = -1, nombre = -1, pais = -1;
    int fundada = -1, industria = -1, empleados = -1;
    bool validas() const { return index >= 0; }
};

inline ColumnasCsv columnas_de(const std::vector<std::string>& encabezado) {
    ColumnasCsv col;
    for (int i = 0; i < (int)encabezado.size(); i++) {
        const std::string& h = encabezado[i];
        if      (h == "Index")               col.index = i;
        else if (h == "Organization Id")     col.org = i;
        else if (h == "Name")                col.nombre = i;
        else if (h == "Country")             col.pais = i;
        else if (h == "Founded")             col.fundada = i;
        else if (h == "Industry")            col.industria = i;
        else if (h == "Number of employees") col.empleados = i;
    }
    return col;
}

inline bool fila_a_registro(const std::vector<std::string>& c, const ColumnasCsv& col, Registro& r) {
    if ((int)c.size() <= col.index) return false;
    auto campo = [&c](int i) -> std::string {
        return (i >= 0 && i < (int)c.size()) ? c[i] : std::string();
    };
    std::memset(&r, 0, sizeof(Registro));
    try { r.index = std::stoi(c[col.index]); } catch (...) { return false; }
    try { r.fundada = std::stoi(campo(col.fundada)); } catch (...) { r.fundada = 0; }
    try { r.empleados = std::stoi(campo(col.empleados)); } catch (...) { r.empleados = 0; }
    copiar_campo(r.org_id,    sizeof(r.org_id),    campo(col.org));
    copiar_campo(r.nombre,    sizeof(r.nombre),    campo(col.nombre));
    copiar_campo(r.pais,      sizeof(r.pais),      campo(col.pais));
    copiar_campo(r.industria, sizeof(r.industria), campo(col.industria));
    return true;
}

// Lee el CSV completo a memoria. Solo hace falta para la carga masiva, que
// necesita ordenar antes de construir.
inline std::vector<Registro> leer_csv(const std::string& ruta) {
    std::vector<Registro> salida;
    std::ifstream f(ruta);
    if (!f.is_open()) return salida;

    std::string linea;
    if (!std::getline(f, linea)) return salida;
    quitar_cr(linea);
    ColumnasCsv col = columnas_de(partir_linea_csv(linea));
    if (!col.validas()) return salida;

    Registro r;
    while (std::getline(f, linea)) {
        quitar_cr(linea);
        if (linea.empty()) continue;
        if (fila_a_registro(partir_linea_csv(linea), col, r)) salida.push_back(r);
    }
    return salida;
}

// Carga en STREAMING: lee y mete una fila a la vez. Nunca hay mas de un
// registro en memoria, asi que sirve para archivos de cualquier tamano.
inline long cargar_csv_streaming(BPlusAgrupado& arbol, const std::string& ruta) {
    std::ifstream f(ruta);
    if (!f.is_open()) return 0;

    std::string linea;
    if (!std::getline(f, linea)) return 0;
    quitar_cr(linea);
    ColumnasCsv col = columnas_de(partir_linea_csv(linea));
    if (!col.validas()) return 0;

    long total = 0;
    Registro r;
    while (std::getline(f, linea)) {
        quitar_cr(linea);
        if (linea.empty()) continue;
        if (fila_a_registro(partir_linea_csv(linea), col, r)) {
            arbol.insertar(r);
            total++;
        }
    }
    return total;
}
