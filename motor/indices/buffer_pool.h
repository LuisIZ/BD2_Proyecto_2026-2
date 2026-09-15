#pragma once

#include "gestor_paginas.h"

#include <list>
#include <unordered_map>

namespace detalle_bplus_no_agrupado {

constexpr int NUM_MARCOS = 64;

// cache
class BufferPool {
public:
    // empieza con todos los marcos libres
    explicit BufferPool(GestorPaginas& gestor_paginas);

    char* fijar(PageId pagina);
    void soltar(PageId pagina, bool modificada);
    void vaciar();
    void limpiar();
    int paginas_residentes() const;

    long aciertos = 0;
    long fallos = 0;
    long desalojos = 0;

private:
    // busca un marco libre o saca la página menos usada que no esté en uso
    int conseguir_marco();

    GestorPaginas& gestor_;
    alignas(8) char marcos_[NUM_MARCOS][TAM_PAGINA];
    PageId pagina_de_[NUM_MARCOS];
    int fijaciones_[NUM_MARCOS];
    bool sucio_[NUM_MARCOS];
    bool ocupado_[NUM_MARCOS];
    std::unordered_map<PageId, int> tabla_;
    std::list<int> lru_;
    std::list<int>::iterator pos_lru_[NUM_MARCOS];
};

} 
