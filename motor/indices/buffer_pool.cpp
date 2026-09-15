#include "buffer_pool.h"

#include <stdexcept>

namespace detalle_bplus_no_agrupado {

// empieza con todos los marcos libres
BufferPool::BufferPool(GestorPaginas& gestor_paginas) : gestor_(gestor_paginas) {
    for (int marco = 0; marco < NUM_MARCOS; marco++) {
        pagina_de_[marco] = PAGINA_NULA;
        fijaciones_[marco] = 0;
        sucio_[marco] = false;
        ocupado_[marco] = false;
    }
}

// trae la página a la caché y la marca en uso; si no estaba, la lee del disco
char* BufferPool::fijar(PageId pagina) {
    auto encontrado = tabla_.find(pagina);
    if (encontrado != tabla_.end()) {
        int marco = encontrado->second;
        fijaciones_[marco]++;
        lru_.erase(pos_lru_[marco]);
        lru_.push_front(marco);
        pos_lru_[marco] = lru_.begin();
        aciertos++;
        return marcos_[marco];
    }

    fallos++;
    int marco = conseguir_marco();
    gestor_.leer(pagina, marcos_[marco]);
    pagina_de_[marco] = pagina;
    fijaciones_[marco] = 1;
    sucio_[marco] = false;
    ocupado_[marco] = true;
    tabla_[pagina] = marco;
    lru_.push_front(marco);
    pos_lru_[marco] = lru_.begin();
    return marcos_[marco];
}

void BufferPool::soltar(PageId pagina, bool modificada) {
    auto encontrado = tabla_.find(pagina);
    if (encontrado == tabla_.end()) return;
    int marco = encontrado->second;
    if (modificada) sucio_[marco] = true;
    if (fijaciones_[marco] > 0) fijaciones_[marco]--;
}

// escribe al disco las páginas modificadas
void BufferPool::vaciar() {
    for (int marco = 0; marco < NUM_MARCOS; marco++) {
        if (ocupado_[marco] && sucio_[marco]) {
            gestor_.escribir(pagina_de_[marco], marcos_[marco]);
            sucio_[marco] = false;
        }
    }
    gestor_.archivo.flush();
}

void BufferPool::limpiar() {
    for (int marco = 0; marco < NUM_MARCOS; marco++) {
        if (ocupado_[marco] && fijaciones_[marco] > 0) {
            throw std::runtime_error("no se puede vaciar la cache: hay paginas fijadas");
        }
    }

    vaciar();
    tabla_.clear();
    lru_.clear();
    for (int marco = 0; marco < NUM_MARCOS; marco++) {
        pagina_de_[marco] = PAGINA_NULA;
        fijaciones_[marco] = 0;
        sucio_[marco] = false;
        ocupado_[marco] = false;
    }
}

// cantidad de páginas que hay en la caché
int BufferPool::paginas_residentes() const {
    return static_cast<int>(tabla_.size());
}

int BufferPool::conseguir_marco() {
    for (int marco = 0; marco < NUM_MARCOS; marco++) {
        if (!ocupado_[marco]) return marco;
    }

    for (auto it = lru_.end(); it != lru_.begin();) {
        --it;
        int marco = *it;
        if (fijaciones_[marco] == 0) {
            if (sucio_[marco]) {
                gestor_.escribir(pagina_de_[marco], marcos_[marco]);
            }
            tabla_.erase(pagina_de_[marco]);
            lru_.erase(it);
            ocupado_[marco] = false;
            sucio_[marco] = false;
            desalojos++;
            return marco;
        }
    }

    throw std::runtime_error("cache llena: todas las paginas estan fijadas");
}

}  // namespace detalle_bplus_no_agrupado
