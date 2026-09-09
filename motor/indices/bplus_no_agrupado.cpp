#include <vector>

using namespace std;

const int ORDEN       = 4;
const int MAX_CLAVES  = ORDEN - 1;
const int MIN_HOJA    = ORDEN / 2;
const int MIN_INTERNO = (ORDEN + 1) / 2 - 1;

//comparo dos entradas 1 clave y si son iguales la posicion
inline bool menor(int c1, int p1, int c2, int p2) {
    if (c1 != c2) return c1 < c2;
    return p1 < p2;
}


struct Nodo {
    bool es_hoja;
    vector<int> claves;
    vector<int> punteros;  
    vector<Nodo*> hijos;   
    Nodo* siguiente;             

    Nodo(bool hoja) : es_hoja(hoja), siguiente(nullptr) {}
    int minimo() const { return es_hoja ? MIN_HOJA : MIN_INTERNO; }
};


//el nodo y el hijo que tome para poder subir despues
struct Rastro {
    Nodo* padre;
    int i;
};


class BPlusNoAgrupado {
public:
    Nodo* raiz;

    // crea el arbol vacio con una sola hoja hace de raiz
    BPlusNoAgrupado() { raiz = new Nodo(true); }

    ~BPlusNoAgrupado() { liberar(raiz); }
    BPlusNoAgrupado(const BPlusNoAgrupado&) = delete;
    BPlusNoAgrupado& operator=(const BPlusNoAgrupado&) = delete;

    //insetamos (clave, pos)
    void insertar(int clave, int pos) {
        vector<Rastro> camino;
        Nodo* hoja = buscar_hoja(clave, pos, camino);

        int i = 0;
        while (i < (int)hoja->claves.size() &&
               !menor(clave, pos, hoja->claves[i], hoja->punteros[i]))
            i = i + 1;
        hoja->claves.insert(hoja->claves.begin() + i, clave);
        hoja->punteros.insert(hoja->punteros.begin() + i, pos);

        if ((int)hoja->claves.size() > MAX_CLAVES)
            partir_hoja(hoja, camino);
    }

    // devuelve las posiciones de todos los registros que tienen esa clave
    vector<int> buscar(int clave) {
        return buscar_rango(clave, clave);
    }

    // recore las hojas y saca las posiciones
    vector<int> buscar_rango(int desde, int hasta) {
        vector<Rastro> camino;
        Nodo* hoja = buscar_hoja(desde, -1, camino);  
        vector<int> resultado;
        while (hoja != nullptr) {
            for (int i = 0; i < (int)hoja->claves.size(); i++) {
                if (hoja->claves[i] > hasta) return resultado;
                if (hoja->claves[i] >= desde) resultado.push_back(hoja->punteros[i]);
            }
            hoja = hoja->siguiente;
        }
        return resultado;
    }

    int altura() {
        int h = 1;
        Nodo* n = raiz;
        while (!n->es_hoja) {
            n = n->hijos[0];
            h = h + 1;
        }
        return h;
    }

    //borra todas las entradas con esa clave y cuantas borro
    int eliminar(int clave) {
        vector<int> posiciones = buscar(clave);
        int n = 0;
        for (int pos : posiciones)
            if (eliminar_entrada(clave, pos)) n = n + 1;
        return n;
    }

    // borrmos una sola entrada
    bool eliminar_entrada(int clave, int pos) {
        vector<Rastro> camino;
        Nodo* hoja = buscar_hoja(clave, pos, camino);

        int i = -1;
        for (int j = 0; j < (int)hoja->claves.size(); j++)
            if (hoja->claves[j] == clave && hoja->punteros[j] == pos) { i = j; break; }
        if (i == -1) return false;

        hoja->claves.erase(hoja->claves.begin() + i);
        hoja->punteros.erase(hoja->punteros.begin() + i);
        reparar(hoja, camino);
        return true;
    }

private:
    // baja de la raiz hasta la hoja y va guardando los padres en el camino
    Nodo* buscar_hoja(int clave, int pos, vector<Rastro>& camino) {
        Nodo* nodo = raiz;
        camino.clear();
        while (!nodo->es_hoja) {
            int i = 0;
            while (i < (int)nodo->claves.size() &&
                   !menor(clave, pos, nodo->claves[i], nodo->punteros[i]))
                i = i + 1;
            camino.push_back(Rastro{nodo, i});
            nodo = nodo->hijos[i];
        }
        return nodo;
    }

    void partir_hoja(Nodo* hoja, vector<Rastro>& camino) {
        int mitad = (int)hoja->claves.size() / 2;
        Nodo* nueva = new Nodo(true);
        nueva->claves.assign(hoja->claves.begin() + mitad, hoja->claves.end());
        nueva->punteros.assign(hoja->punteros.begin() + mitad, hoja->punteros.end());
        hoja->claves.resize(mitad);
        hoja->punteros.resize(mitad);

        nueva->siguiente = hoja->siguiente; 
        hoja->siguiente = nueva;  

        subir_clave(nueva->claves[0], nueva->punteros[0], nueva, camino);
    }

    //mete el separador en el padre y parte tanbien al padre si se llena
    void subir_clave(int clave, int pos, Nodo* nodo_der, vector<Rastro>& camino) {
        if (camino.empty()) {
            Nodo* nueva_raiz = new Nodo(false);
            nueva_raiz->claves.push_back(clave);
            nueva_raiz->punteros.push_back(pos);
            nueva_raiz->hijos.push_back(raiz);
            nueva_raiz->hijos.push_back(nodo_der);
            raiz = nueva_raiz;//crece por arriba
            return;
        }

        Nodo* padre = camino.back().padre;
        int i = camino.back().i;
        camino.pop_back();
        padre->claves.insert(padre->claves.begin() + i, clave);
        padre->punteros.insert(padre->punteros.begin() + i, pos);
        padre->hijos.insert(padre->hijos.begin() + i + 1, nodo_der);

        if ((int)padre->claves.size() <= MAX_CLAVES) return;

        int mitad = (int)padre->claves.size() / 2;
        int sube_c = padre->claves[mitad];
        int sube_p = padre->punteros[mitad];
        Nodo* nuevo = new Nodo(false);
        nuevo->claves.assign(padre->claves.begin() + mitad + 1, padre->claves.end());
        nuevo->punteros.assign(padre->punteros.begin() + mitad + 1, padre->punteros.end());
        nuevo->hijos.assign(padre->hijos.begin() + mitad + 1, padre->hijos.end());
        padre->claves.resize(mitad);
        padre->punteros.resize(mitad);
        padre->hijos.resize(mitad + 1);

        subir_clave(sube_c, sube_p, nuevo, camino);
    }

    //arreglams un nodo que quedo por debajo del minimo de claves permitido
    void reparar(Nodo* nodo, vector<Rastro>& camino) {
        if (camino.empty()) {                 
            if (!nodo->es_hoja && nodo->claves.empty()) {
                Nodo* vieja = raiz;
                raiz = nodo->hijos[0];      
                delete vieja;
            }
            return;
        }

        if ((int)nodo->claves.size() >= nodo->minimo()) return;

        Nodo* padre = camino.back().padre;
        int i = camino.back().i;
        camino.pop_back();
        Nodo* izq = (i > 0) ? padre->hijos[i - 1] : nullptr;
        Nodo* der = (i + 1 < (int)padre->hijos.size()) ? padre->hijos[i + 1] : nullptr;

        if (izq != nullptr && (int)izq->claves.size() > izq->minimo()) {
            prestar_de_izq(nodo, izq, padre, i);
            return;
        }
        if (der != nullptr && (int)der->claves.size() > der->minimo()) {
            prestar_de_der(nodo, der, padre, i);
            return;
        }

        if (izq != nullptr) fusionar(izq, nodo, padre, i - 1);
        else                fusionar(nodo, der, padre, i);

        reparar(padre, camino);
    }

    //restamso del hermano de la izquierda
    void prestar_de_izq(Nodo* nodo, Nodo* izq, Nodo* padre, int i) {
        if (nodo->es_hoja) {
            nodo->claves.insert(nodo->claves.begin(), izq->claves.back());
            nodo->punteros.insert(nodo->punteros.begin(), izq->punteros.back());
            izq->claves.pop_back();
            izq->punteros.pop_back();
            padre->claves[i - 1] = nodo->claves[0]; 
            padre->punteros[i - 1] = nodo->punteros[0];
        } else {
            nodo->claves.insert(nodo->claves.begin(), padre->claves[i - 1]);
            nodo->punteros.insert(nodo->punteros.begin(), padre->punteros[i - 1]);
            nodo->hijos.insert(nodo->hijos.begin(), izq->hijos.back());
            izq->hijos.pop_back();
            padre->claves[i - 1] = izq->claves.back(); 
            padre->punteros[i - 1] = izq->punteros.back();
            izq->claves.pop_back();
            izq->punteros.pop_back();
        }
    }

    //prestamso al hermano de la derecha
    void prestar_de_der(Nodo* nodo, Nodo* der, Nodo* padre, int i) {
        if (nodo->es_hoja) {
            nodo->claves.push_back(der->claves.front());
            nodo->punteros.push_back(der->punteros.front());
            der->claves.erase(der->claves.begin());
            der->punteros.erase(der->punteros.begin());
            padre->claves[i] = der->claves[0];    
            padre->punteros[i] = der->punteros[0];
        } else {
            nodo->claves.push_back(padre->claves[i]);
            nodo->punteros.push_back(padre->punteros[i]);
            nodo->hijos.push_back(der->hijos.front());
            der->hijos.erase(der->hijos.begin());
            padre->claves[i] = der->claves.front();    
            padre->punteros[i] = der->punteros.front();
            der->claves.erase(der->claves.begin());
            der->punteros.erase(der->punteros.begin());
        }
    }

    //juntamso los dos nodos hermanos en uno solo y quitamos el separador padre
    void fusionar(Nodo* izq, Nodo* der, Nodo* padre, int s) {
        if (izq->es_hoja) {                            
            izq->claves.insert(izq->claves.end(), der->claves.begin(), der->claves.end());
            izq->punteros.insert(izq->punteros.end(), der->punteros.begin(), der->punteros.end());
            izq->siguiente = der->siguiente;
        } else {
            izq->claves.push_back(padre->claves[s]);   
            izq->punteros.push_back(padre->punteros[s]);
            izq->claves.insert(izq->claves.end(), der->claves.begin(), der->claves.end());
            izq->punteros.insert(izq->punteros.end(), der->punteros.begin(), der->punteros.end());
            izq->hijos.insert(izq->hijos.end(), der->hijos.begin(), der->hijos.end());
        }

        padre->claves.erase(padre->claves.begin() + s);
        padre->punteros.erase(padre->punteros.begin() + s);
        padre->hijos.erase(padre->hijos.begin() + s + 1);
        delete der;
    }

    //borramos el nodo y sus hijos
    void liberar(Nodo* n) {
        if (n == nullptr) return;
        if (!n->es_hoja)
            for (Nodo* h : n->hijos) liberar(h);
        delete n;
    }
};
