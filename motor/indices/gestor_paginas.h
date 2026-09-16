#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace detalle_bplus_no_agrupado {

using PageId = std::uint32_t;

constexpr int TAM_PAGINA = 4096;
constexpr PageId PAGINA_NULA = 0;

// lee y escribe las páginas del archivo .bplus
class GestorPaginas {
public:
    // abre el archivo o lo crea
    GestorPaginas(const std::string& ruta_archivo, bool truncar);
    ~GestorPaginas();

    void leer(PageId pagina, char* destino);
    void escribir(PageId pagina, const char* origen);
    PageId asignar();
    long tamano_en_disco();

    std::fstream archivo;
    PageId num_paginas = 0;
    long lecturas = 0;
    long escrituras = 0;

private:
    std::string ruta_;
};

} 
