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

using PageId = uint32_t;

const int    TAM_PAGINA   = 4096;
const PageId PAGINA_NULA  = 0;
const int    NUM_MARCOS   = 64;

const int DEMO_REGS_HOJA = 3;
const int DEMO_HIJOS     = 4;

const uint8_t NODO_INTERNO = 0;
const uint8_t NODO_HOJA    = 1;

//el registro completo: vive dentro de la hoja
struct Registro {
    int  index;
    int  fundada;
    int  empleados;
    char org_id[16];
    char nombre[40];
    char pais[56];
    char industria[56];
};

static_assert(std::is_trivially_copyable<Registro>::value,
              "el registro debe poder copiarse byte a byte a la pagina");

//cabecera de cada pagina, 8 bytes
struct CabeceraPagina {
    uint8_t  tipo;
    uint16_t num;
    PageId   siguiente;
};

const int ESPACIO_UTIL = TAM_PAGINA - (int)sizeof(CabeceraPagina);

const int CAP_REGS_REAL  = ESPACIO_UTIL / (int)sizeof(Registro);
const int CAP_HIJOS_REAL = (ESPACIO_UTIL + (int)sizeof(int))
                           / ((int)sizeof(PageId) + (int)sizeof(int));

const int MAX_REGS_HOJA  = (DEMO_REGS_HOJA > 0 && DEMO_REGS_HOJA < CAP_REGS_REAL)
                           ? DEMO_REGS_HOJA : CAP_REGS_REAL;
const int MIN_REGS_HOJA  = MAX_REGS_HOJA / 2;

const int MAX_HIJOS      = (DEMO_HIJOS > 0 && DEMO_HIJOS < CAP_HIJOS_REAL)
                           ? DEMO_HIJOS : CAP_HIJOS_REAL;
const int MAX_CLAVES_INT = MAX_HIJOS - 1;
const int MIN_CLAVES_INT = MAX_CLAVES_INT / 2;

static_assert(MAX_REGS_HOJA >= 2, "hacen falta al menos 2 registros por hoja");
static_assert(MAX_HIJOS >= 3, "hacen falta al menos 3 hijos por nodo interno");
static_assert((int)sizeof(CabeceraPagina) + (int)sizeof(Registro) * MAX_REGS_HOJA
              <= TAM_PAGINA, "la hoja no cabe en la pagina");
static_assert((int)sizeof(CabeceraPagina) + (int)sizeof(PageId) * MAX_HIJOS
              + (int)sizeof(int) * MAX_CLAVES_INT <= TAM_PAGINA,
              "el nodo interno no cabe en la pagina");

//vista sobre los bytes de una hoja, no copia nada
struct VistaHoja {
    CabeceraPagina* cab;
    Registro*       regs;
    explicit VistaHoja(char* p)
        : cab(reinterpret_cast<CabeceraPagina*>(p)),
          regs(reinterpret_cast<Registro*>(p + sizeof(CabeceraPagina))) {}
};

//vista sobre los bytes de un nodo interno
struct VistaInterna {
    CabeceraPagina* cab;
    PageId*         hijos;
    int*            claves;
    explicit VistaInterna(char* p)
        : cab(reinterpret_cast<CabeceraPagina*>(p)),
          hijos(reinterpret_cast<PageId*>(p + sizeof(CabeceraPagina))),
          claves(reinterpret_cast<int*>(p + sizeof(CabeceraPagina)
                                        + sizeof(PageId) * MAX_HIJOS)) {}
};

//cabecera del archivo, va en la pagina 0
struct CabeceraArchivo {
    PageId   raiz;
    PageId   libres;
    uint64_t num_registros;
    uint32_t magico;
};

//lee y escribe paginas en el archivo
class GestorPaginas {
public:
    std::string  ruta;
    std::fstream archivo;
    PageId       num_paginas = 0;
    long         lecturas = 0;
    long         escrituras = 0;

    //abre o crea el archivo
    GestorPaginas(const std::string& r, bool truncar) : ruta(r) {
        if (truncar) {
            std::ofstream limpiar(ruta, std::ios::binary | std::ios::trunc);
        }
        archivo.open(ruta, std::ios::binary | std::ios::in | std::ios::out);
        if (!archivo.is_open()) {
            std::ofstream crear(ruta, std::ios::binary);
            crear.close();
            archivo.open(ruta, std::ios::binary | std::ios::in | std::ios::out);
        }
        if (!archivo.is_open())
            throw std::runtime_error("no se pudo abrir el archivo: " + ruta);
        archivo.seekg(0, std::ios::end);
        num_paginas = (PageId)(archivo.tellg() / TAM_PAGINA);
    }

    ~GestorPaginas() { if (archivo.is_open()) archivo.close(); }

    //lee la pagina p del disco
    void leer(PageId p, char* destino) {
        archivo.clear();
        archivo.seekg((std::streamoff)p * TAM_PAGINA);
        archivo.read(destino, TAM_PAGINA);
        std::streamsize leidos = archivo.gcount();
        if (leidos < TAM_PAGINA) {
            std::memset(destino + leidos, 0, TAM_PAGINA - leidos);
            archivo.clear();
        }
        lecturas++;
    }

    //escribe la pagina p al disco
    void escribir(PageId p, const char* origen) {
        archivo.clear();
        archivo.seekp((std::streamoff)p * TAM_PAGINA);
        archivo.write(origen, TAM_PAGINA);
        escrituras++;
    }

    //crece el archivo una pagina
    PageId asignar() {
        PageId p = num_paginas++;
        std::vector<char> vacia(TAM_PAGINA, 0);
        escribir(p, vacia.data());
        return p;
    }

    //bytes que ocupa el archivo
    long tamano_en_disco() {
        archivo.clear();
        archivo.flush();
        archivo.seekg(0, std::ios::end);
        return (long)archivo.tellg();
    }
};

//los 64 marcos en RAM con reemplazo LRU
class BufferPool {
public:
    GestorPaginas& gestor;

    alignas(8) char marcos[NUM_MARCOS][TAM_PAGINA];
    PageId pagina_de[NUM_MARCOS];
    int    fijaciones[NUM_MARCOS];
    bool   sucio[NUM_MARCOS];
    bool   ocupado[NUM_MARCOS];

    std::unordered_map<PageId, int> tabla;
    std::list<int> lru;
    std::list<int>::iterator pos_lru[NUM_MARCOS];

    long aciertos = 0;
    long fallos = 0;
    long desalojos = 0;

    //arranca con todos los marcos vacios
    explicit BufferPool(GestorPaginas& g) : gestor(g) {
        for (int m = 0; m < NUM_MARCOS; m++) {
            pagina_de[m] = PAGINA_NULA;
            fijaciones[m] = 0;
            sucio[m] = false;
            ocupado[m] = false;
        }
    }

    //trae la pagina a un marco, del disco si no estaba
    char* fijar(PageId p) {
        auto it = tabla.find(p);
        if (it != tabla.end()) {
            int m = it->second;
            fijaciones[m]++;
            lru.erase(pos_lru[m]);
            lru.push_front(m);
            pos_lru[m] = lru.begin();
            aciertos++;
            return marcos[m];
        }
        fallos++;
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

    //libera el marco y marca si se ensucio
    void soltar(PageId p, bool modificada) {
        auto it = tabla.find(p);
        if (it == tabla.end()) return;
        int m = it->second;
        if (modificada) sucio[m] = true;
        if (fijaciones[m] > 0) fijaciones[m]--;
    }

    //escribe las paginas sucias al disco
    void vaciar() {
        for (int m = 0; m < NUM_MARCOS; m++) {
            if (ocupado[m] && sucio[m]) {
                gestor.escribir(pagina_de[m], marcos[m]);
                sucio[m] = false;
            }
        }
        gestor.archivo.flush();
    }

    //cuantos marcos estan ocupados
    int paginas_residentes() const {
        return (int)tabla.size();
    }

    //vacia todo el pool
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
    //un marco libre, o desaloja el menos usado
    int conseguir_marco() {
        for (int m = 0; m < NUM_MARCOS; m++)
            if (!ocupado[m]) return m;

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

//el arbol
class BPlusAgrupado {
public:
    GestorPaginas   gestor;
    BufferPool      pool;
    CabeceraArchivo cab;

    //abre el archivo; si esta vacio crea cabecera y raiz
    explicit BPlusAgrupado(const std::string& ruta, bool truncar = true)
        : gestor(ruta, truncar), pool(gestor) {
        if (gestor.num_paginas == 0) {
            gestor.asignar();
            PageId r = gestor.asignar();
            char* p = pool.fijar(r);
            VistaHoja h(p);
            h.cab->tipo = NODO_HOJA;
            h.cab->num = 0;
            h.cab->siguiente = PAGINA_NULA;
            pool.soltar(r, true);
            cab.raiz = r;
            cab.libres = PAGINA_NULA;
            cab.num_registros = 0;
            cab.magico = 0x42504C53;
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

    //busca la clave y devuelve el registro completo
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

    //recorre las hojas desde 'desde' hasta pasarse de 'hasta'
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

    //niveles desde la raiz hasta la hoja
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

    //total de registros
    long num_registros() const { return (long)cab.num_registros; }

    //inserta el registro; si se parte la raiz el arbol crece
    void insertar(const Registro& r) {
        int    clave_sube = 0;
        PageId pagina_nueva = PAGINA_NULA;
        if (insertar_en(cab.raiz, r, clave_sube, pagina_nueva)) {
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

    //borra la clave; si la raiz queda vacia el arbol baja un nivel
    bool eliminar(int clave) {
        bool encontrado = false;
        eliminar_en(cab.raiz, clave, encontrado);
        if (!encontrado) return false;

        cab.num_registros--;
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

    //construye de abajo hacia arriba con los registros ordenados
    void cargar_masivo(std::vector<Registro>& regs, double factor_llenado = 0.9) {
        if (regs.empty()) return;
        std::sort(regs.begin(), regs.end(),
                  [](const Registro& a, const Registro& b) { return a.index < b.index; });

        if (cab.num_registros == 0) liberar_pagina(cab.raiz);

        int por_hoja = std::max(1, (int)(MAX_REGS_HOJA * factor_llenado));
        std::vector<PageId> nivel;
        std::vector<int>    claves;

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

            if (anterior != PAGINA_NULA) {
                char* pa = pool.fijar(anterior);
                reinterpret_cast<CabeceraPagina*>(pa)->siguiente = pid;
                pool.soltar(anterior, true);
            }
            anterior = pid;
        }

        int por_nodo = std::max(2, (int)(MAX_HIJOS * factor_llenado));
        while (nivel.size() > 1) {
            std::vector<PageId> arriba;
            std::vector<int>    claves_arriba;
            size_t s = 0;
            while (s < nivel.size()) {
                size_t n = std::min((size_t)por_nodo, nivel.size() - s);
                if (nivel.size() - (s + n) == 1 && n > 2) n--;

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

    //vuelca cabecera y paginas sucias
    void sincronizar() { guardar_cabecera(); pool.vaciar(); }

    //vacia el pool para medir sin cache
    void enfriar_cache() { guardar_cabecera(); pool.limpiar(); cargar_cabecera(); }

    //todas las claves siguiendo la cadena de hojas
    std::vector<int> recorrer_cadena() {
        std::vector<int> claves;
        PageId pid = cab.raiz;
        while (true) {
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

    //verifica el invariante de profundidad
    bool hojas_a_la_misma_altura() {
        std::vector<int> prof;
        profundidades(cab.raiz, 0, prof);
        if (prof.empty()) return true;
        return *std::min_element(prof.begin(), prof.end())
            == *std::max_element(prof.begin(), prof.end());
    }

    //cuenta paginas internas y hojas
    void contar_paginas(long& internas, long& hojas) {
        internas = 0; hojas = 0;
        contar(cab.raiz, internas, hojas);
    }

private:

    //escribe la pagina 0
    void guardar_cabecera() {
        char* p = pool.fijar(PAGINA_NULA);
        std::memcpy(p, &cab, sizeof(CabeceraArchivo));
        pool.soltar(PAGINA_NULA, true);
    }

    //lee la pagina 0
    void cargar_cabecera() {
        char* p = pool.fijar(PAGINA_NULA);
        std::memcpy(&cab, p, sizeof(CabeceraArchivo));
        pool.soltar(PAGINA_NULA, false);
    }

    //reutiliza una libre o crece el archivo
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

    //devuelve la pagina a la lista de libres
    void liberar_pagina(PageId p) {
        char* buf = pool.fijar(p);
        std::memset(buf, 0, TAM_PAGINA);
        std::memcpy(buf, &cab.libres, sizeof(PageId));
        pool.soltar(p, true);
        cab.libres = p;
        guardar_cabecera();
    }

    //busqueda binaria: primera posicion con index >= clave
    static int pos_en_hoja(const Registro* regs, int n, int clave) {
        int lo = 0, hi = n;
        while (lo < hi) {
            int med = (lo + hi) / 2;
            if (regs[med].index < clave) lo = med + 1; else hi = med;
        }
        return lo;
    }

    //busqueda binaria: por que hijo bajar
    static int hijo_para(const int* claves, int n, int clave) {
        int lo = 0, hi = n;
        while (lo < hi) {
            int med = (lo + hi) / 2;
            if (clave >= claves[med]) lo = med + 1; else hi = med;
        }
        return lo;
    }

    //baja de la raiz a la hoja fijando y soltando
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

    //inserta recursivo; devuelve si hubo split y que sube
    bool insertar_en(PageId pid, const Registro& r, int& clave_sube, PageId& pagina_nueva) {
        char* p = pool.fijar(pid);
        uint8_t tipo = reinterpret_cast<CabeceraPagina*>(p)->tipo;

        if (tipo == NODO_HOJA) {
            VistaHoja hoja(p);
            int n = hoja.cab->num;
            int i = pos_en_hoja(hoja.regs, n, r.index);

            if (n < MAX_REGS_HOJA) {
                std::memmove(&hoja.regs[i + 1], &hoja.regs[i],
                             sizeof(Registro) * (n - i));
                hoja.regs[i] = r;
                hoja.cab->num = (uint16_t)(n + 1);
                pool.soltar(pid, true);
                return false;
            }

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

            clave_sube   = hn.regs[0].index;
            pagina_nueva = nueva;
            pool.soltar(nueva, true);
            pool.soltar(pid, true);
            return true;
        }

        VistaInterna nodo(p);
        int i = hijo_para(nodo.claves, nodo.cab->num, r.index);
        PageId hijo = nodo.hijos[i];

        int    csube = 0;
        PageId pnueva = PAGINA_NULA;
        bool hubo_split = insertar_en(hijo, r, csube, pnueva);
        if (!hubo_split) { pool.soltar(pid, false); return false; }

        int n = nodo.cab->num;
        if (n < MAX_CLAVES_INT) {
            std::memmove(&nodo.claves[i + 1], &nodo.claves[i], sizeof(int) * (n - i));
            std::memmove(&nodo.hijos[i + 2], &nodo.hijos[i + 1], sizeof(PageId) * (n - i));
            nodo.claves[i]    = csube;
            nodo.hijos[i + 1] = pnueva;
            nodo.cab->num = (uint16_t)(n + 1);
            pool.soltar(pid, true);
            return false;
        }

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
        int sube  = tclaves[mitad];

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

    //borra recursivo; devuelve si el nodo quedo bajo el minimo
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

    //prestar de un hermano o fusionar
    void reparar_hijo(char* p_padre, int i) {
        VistaInterna padre(p_padre);
        PageId pid_hijo = padre.hijos[i];
        char* ph = pool.fijar(pid_hijo);
        bool es_hoja = reinterpret_cast<CabeceraPagina*>(ph)->tipo == NODO_HOJA;

        if (i > 0) {
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

        if (i < padre.cab->num) {
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
        if (i > 0) fusionar(padre, i - 1, es_hoja);
        else       fusionar(padre, i, es_hoja);
    }

    //toma una entrada del hermano izquierdo
    void prestar_de_izquierda(VistaInterna& padre, int i, char* pi, char* ph, bool es_hoja) {
        if (es_hoja) {
            VistaHoja izq(pi), hijo(ph);
            int ni = izq.cab->num, nh = hijo.cab->num;
            std::memmove(&hijo.regs[1], &hijo.regs[0], sizeof(Registro) * nh);
            hijo.regs[0] = izq.regs[ni - 1];
            hijo.cab->num = (uint16_t)(nh + 1);
            izq.cab->num  = (uint16_t)(ni - 1);
            padre.claves[i - 1] = hijo.regs[0].index;
        } else {
            VistaInterna izq(pi), hijo(ph);
            int ni = izq.cab->num, nh = hijo.cab->num;
            std::memmove(&hijo.claves[1], &hijo.claves[0], sizeof(int) * nh);
            std::memmove(&hijo.hijos[1],  &hijo.hijos[0],  sizeof(PageId) * (nh + 1));
            hijo.claves[0] = padre.claves[i - 1];
            hijo.hijos[0]  = izq.hijos[ni];
            padre.claves[i - 1] = izq.claves[ni - 1];
            hijo.cab->num = (uint16_t)(nh + 1);
            izq.cab->num  = (uint16_t)(ni - 1);
        }
    }

    //toma una entrada del hermano derecho
    void prestar_de_derecha(VistaInterna& padre, int i, char* pd, char* ph, bool es_hoja) {
        if (es_hoja) {
            VistaHoja der(pd), hijo(ph);
            int nd = der.cab->num, nh = hijo.cab->num;
            hijo.regs[nh] = der.regs[0];
            std::memmove(&der.regs[0], &der.regs[1], sizeof(Registro) * (nd - 1));
            hijo.cab->num = (uint16_t)(nh + 1);
            der.cab->num  = (uint16_t)(nd - 1);
            padre.claves[i] = der.regs[0].index;
        } else {
            VistaInterna der(pd), hijo(ph);
            int nd = der.cab->num, nh = hijo.cab->num;
            hijo.claves[nh]    = padre.claves[i];
            hijo.hijos[nh + 1] = der.hijos[0];
            padre.claves[i] = der.claves[0];
            std::memmove(&der.claves[0], &der.claves[1], sizeof(int) * (nd - 1));
            std::memmove(&der.hijos[0],  &der.hijos[1],  sizeof(PageId) * nd);
            hijo.cab->num = (uint16_t)(nh + 1);
            der.cab->num  = (uint16_t)(nd - 1);
        }
    }

    //junta dos hermanos y quita el separador
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
            izq.cab->siguiente = der.cab->siguiente;
        } else {
            VistaInterna izq(pi), der(pd);
            izq.claves[izq.cab->num] = padre.claves[s];
            std::memcpy(&izq.claves[izq.cab->num + 1], der.claves,
                        sizeof(int) * der.cab->num);
            std::memcpy(&izq.hijos[izq.cab->num + 1], der.hijos,
                        sizeof(PageId) * (der.cab->num + 1));
            izq.cab->num = (uint16_t)(izq.cab->num + der.cab->num + 1);
        }

        pool.soltar(pid_izq, true);
        pool.soltar(pid_der, false);
        liberar_pagina(pid_der);

        int n = padre.cab->num;
        std::memmove(&padre.claves[s], &padre.claves[s + 1], sizeof(int) * (n - s - 1));
        std::memmove(&padre.hijos[s + 1], &padre.hijos[s + 2], sizeof(PageId) * (n - s - 1));
        padre.cab->num = (uint16_t)(n - 1);
    }

    //profundidad de cada hoja
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

    //cuenta paginas recursivo
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
