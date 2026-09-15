#include "gestor_paginas.h"

#include <stdexcept>
#include <vector>

namespace detalle_bplus_no_agrupado {

// abre el archivo o lo crea
GestorPaginas::GestorPaginas(const std::string& ruta_archivo, bool truncar)
    : ruta_(ruta_archivo) {
    if (truncar) {
        std::ofstream limpiar(ruta_, std::ios::binary | std::ios::trunc);
    }

    archivo.open(ruta_, std::ios::binary | std::ios::in | std::ios::out);
    if (!archivo.is_open()) {
        std::ofstream crear(ruta_, std::ios::binary);
        crear.close();
        archivo.open(ruta_, std::ios::binary | std::ios::in | std::ios::out);
    }
    if (!archivo.is_open()) {
        throw std::runtime_error("no se pudo abrir el indice: " + ruta_);
    }

    archivo.seekg(0, std::ios::end);
    num_paginas = static_cast<PageId>(archivo.tellg() / TAM_PAGINA);
}

GestorPaginas::~GestorPaginas() {
    if (archivo.is_open()) archivo.close();
}

// copia una página del disco en destino
void GestorPaginas::leer(PageId pagina, char* destino) {
    if (pagina >= num_paginas) throw std::out_of_range("page_id invalido");
    archivo.clear();
    archivo.seekg(static_cast<std::streamoff>(pagina) * TAM_PAGINA);
    archivo.read(destino, TAM_PAGINA);
    if (archivo.gcount() != TAM_PAGINA) {
        archivo.clear();
        throw std::runtime_error("lectura incompleta del indice");
    }
    lecturas++;
}

// escribe origen en una página del disco
void GestorPaginas::escribir(PageId pagina, const char* origen) {
    archivo.clear();
    archivo.seekp(static_cast<std::streamoff>(pagina) * TAM_PAGINA);
    archivo.write(origen, TAM_PAGINA);
    if (!archivo) throw std::runtime_error("fallo al escribir el indice");
    escrituras++;
}

// agrega una página vacía al final del archivo
PageId GestorPaginas::asignar() {
    PageId pagina = num_paginas++;
    std::vector<char> vacia(TAM_PAGINA, 0);
    escribir(pagina, vacia.data());
    return pagina;
}

// tamaño del archivo en bytes
long GestorPaginas::tamano_en_disco() {
    archivo.clear();
    archivo.flush();
    archivo.seekg(0, std::ios::end);
    return static_cast<long>(archivo.tellg());
}

}  // namespace detalle_bplus_no_agrupado
