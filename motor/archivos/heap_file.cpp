#include "heap_file.h"

#include "serializacion.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <utility>

#ifdef HEAP_DEBUG
#include <cassert>
#define HEAP_VERIFICAR(pagina) assert((pagina).verificar_invariantes())
#else
#define HEAP_VERIFICAR(pagina) ((void)0)
#endif

namespace motor {

std::ostream& operator<<(std::ostream& salida, const EstadisticasHeap& estadisticas) {
    salida << "paginas=" << estadisticas.num_paginas
           << ", vivos=" << estadisticas.registros_vivos
           << ", tumbas=" << estadisticas.tumbas
           << ", bytes_desperdiciados=" << estadisticas.bytes_desperdiciados
           << ", bytes_libres=" << estadisticas.bytes_libres
           << ", archivo=" << estadisticas.tamano_archivo_bytes << " B";
    return salida;
}

HeapFile::HeapFile(const std::string& ruta, bool truncar) : buffer_(PAGE_SIZE) {
    abrir_o_crear(ruta, truncar);
}

HeapFile::~HeapFile() {
    if (archivo_.is_open()) {
        escribir_cabecera_archivo();
        archivo_.flush();
        archivo_.close();
    }
}

std::streamoff HeapFile::offset_de(std::uint32_t page_id) noexcept {
    return static_cast<std::streamoff>(page_id + 1) * static_cast<std::streamoff>(PAGE_SIZE);
}

void HeapFile::abrir_o_crear(const std::string& ruta, bool truncar) {
    bool existe = false;
    {
        std::ifstream sonda(ruta, std::ios::binary);
        existe = sonda.good();
    }

    if (!existe || truncar) {
        std::ofstream nuevo(ruta, std::ios::binary | std::ios::trunc);
        if (!nuevo) {
            throw std::runtime_error("no se pudo crear el archivo: " + ruta);
        }
        std::array<std::byte, PAGE_SIZE> cabecera{};
        escribir_campo<std::uint32_t>(cabecera.data(), DESPL_MAGIC, MAGIC_HEAP);
        escribir_campo<std::uint16_t>(cabecera.data(), DESPL_VERSION, VERSION_HEAP);
        escribir_campo<std::uint16_t>(cabecera.data(), DESPL_RESERVADO, 0);
        escribir_campo<std::uint32_t>(cabecera.data(), DESPL_PAGE_SIZE,
                                      static_cast<std::uint32_t>(PAGE_SIZE));
        escribir_campo<std::uint32_t>(cabecera.data(), DESPL_NUM_PAGES, 0);
        escribir_campo<std::uint32_t>(cabecera.data(), DESPL_FIRST_PAGE, PAGINA_INVALIDA);
        escribir_campo<std::uint32_t>(cabecera.data(), DESPL_LAST_PAGE, PAGINA_INVALIDA);
        nuevo.write(reinterpret_cast<const char*>(cabecera.data()), PAGE_SIZE);
        nuevo.close();
    }

    archivo_.open(ruta, std::ios::in | std::ios::out | std::ios::binary);
    if (!archivo_) {
        throw std::runtime_error("no se pudo abrir el archivo: " + ruta);
    }
    leer_cabecera_archivo();
    reconstruir_mapa();
}

void HeapFile::leer_cabecera_archivo() {
    std::array<std::byte, PAGE_SIZE> cabecera{};
    archivo_.seekg(0);
    archivo_.read(reinterpret_cast<char*>(cabecera.data()), PAGE_SIZE);
    if (archivo_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        archivo_.clear();
        throw std::invalid_argument("cabecera de archivo incompleta");
    }

    const std::uint32_t magic = leer_campo<std::uint32_t>(cabecera.data(), DESPL_MAGIC);
    if (magic != MAGIC_HEAP) {
        throw std::invalid_argument("el archivo no es un heap file (magic invalido)");
    }
    const std::uint16_t version = leer_campo<std::uint16_t>(cabecera.data(), DESPL_VERSION);
    if (version != VERSION_HEAP) {
        throw std::invalid_argument("version de heap file no soportada");
    }
    const std::uint32_t page_size = leer_campo<std::uint32_t>(cabecera.data(), DESPL_PAGE_SIZE);
    if (page_size != static_cast<std::uint32_t>(PAGE_SIZE)) {
        throw std::invalid_argument("page_size del archivo incompatible con el compilado");
    }

    num_paginas_ = leer_campo<std::uint32_t>(cabecera.data(), DESPL_NUM_PAGES);
    primera_pagina_ = leer_campo<std::uint32_t>(cabecera.data(), DESPL_FIRST_PAGE);
    ultima_pagina_ = leer_campo<std::uint32_t>(cabecera.data(), DESPL_LAST_PAGE);
}

void HeapFile::escribir_cabecera_archivo() {
    std::array<std::byte, PAGE_SIZE> cabecera{};
    escribir_campo<std::uint32_t>(cabecera.data(), DESPL_MAGIC, MAGIC_HEAP);
    escribir_campo<std::uint16_t>(cabecera.data(), DESPL_VERSION, VERSION_HEAP);
    escribir_campo<std::uint16_t>(cabecera.data(), DESPL_RESERVADO, 0);
    escribir_campo<std::uint32_t>(cabecera.data(), DESPL_PAGE_SIZE,
                                  static_cast<std::uint32_t>(PAGE_SIZE));
    escribir_campo<std::uint32_t>(cabecera.data(), DESPL_NUM_PAGES, num_paginas_);
    escribir_campo<std::uint32_t>(cabecera.data(), DESPL_FIRST_PAGE, primera_pagina_);
    escribir_campo<std::uint32_t>(cabecera.data(), DESPL_LAST_PAGE, ultima_pagina_);

    archivo_.seekp(0);
    archivo_.write(reinterpret_cast<const char*>(cabecera.data()), PAGE_SIZE);
    archivo_.flush();
}

void HeapFile::leer_pagina(std::uint32_t page_id, std::byte* destino) const {
    archivo_.seekg(offset_de(page_id));
    archivo_.read(reinterpret_cast<char*>(destino), PAGE_SIZE);
    if (archivo_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        archivo_.clear();
        throw std::runtime_error("lectura de pagina incompleta");
    }
    ++paginas_leidas_;
}

void HeapFile::escribir_pagina(std::uint32_t page_id, const std::byte* origen) {
    archivo_.seekp(offset_de(page_id));
    archivo_.write(reinterpret_cast<const char*>(origen), PAGE_SIZE);
    if (!archivo_) {
        throw std::runtime_error("escritura de pagina fallida");
    }
    ++paginas_escritas_;
}

void HeapFile::escanear_cabeceras(std::vector<std::uint16_t>& espacio_contiguo,
                                  std::vector<std::uint16_t>& bytes_muertos,
                                  std::size_t& registros_vivos, std::size_t& tumbas,
                                  std::size_t& bytes_muertos_totales) const {
    espacio_contiguo.assign(num_paginas_, 0);
    bytes_muertos.assign(num_paginas_, 0);
    registros_vivos = 0;
    tumbas = 0;
    bytes_muertos_totales = 0;

    std::array<std::byte, HEADER_SIZE> cabecera{};
    for (std::uint32_t page_id = 0; page_id < num_paginas_; ++page_id) {
        archivo_.seekg(offset_de(page_id));
        archivo_.read(reinterpret_cast<char*>(cabecera.data()), HEADER_SIZE);
        if (archivo_.gcount() != static_cast<std::streamsize>(HEADER_SIZE)) {
            archivo_.clear();
            throw std::runtime_error("lectura de cabecera de pagina incompleta");
        }
        ++paginas_leidas_;

        const std::uint16_t slot_count = leer_campo<std::uint16_t>(cabecera.data(), 0);
        const std::uint16_t free_ptr = leer_campo<std::uint16_t>(cabecera.data(), 2);
        const std::uint16_t live_count = leer_campo<std::uint16_t>(cabecera.data(), 8);
        const std::uint16_t dead_bytes = leer_campo<std::uint16_t>(cabecera.data(), 10);

        const std::size_t fin_directorio =
            HEADER_SIZE + static_cast<std::size_t>(slot_count) * SLOT_SIZE;
        espacio_contiguo[page_id] =
            free_ptr > fin_directorio ? static_cast<std::uint16_t>(free_ptr - fin_directorio) : 0;
        bytes_muertos[page_id] = dead_bytes;

        registros_vivos += live_count;
        tumbas += static_cast<std::size_t>(slot_count) - live_count;
        bytes_muertos_totales += dead_bytes;
    }
}

void HeapFile::reconstruir_mapa() {
    escanear_cabeceras(espacio_contiguo_, bytes_muertos_, registros_vivos_, tumbas_,
                       bytes_muertos_totales_);
    cursor_ = 0;
}

bool HeapFile::verificar_mapa() const {
    std::vector<std::uint16_t> espacio_contiguo;
    std::vector<std::uint16_t> bytes_muertos;
    std::size_t registros_vivos = 0;
    std::size_t tumbas = 0;
    std::size_t bytes_muertos_totales = 0;

    escanear_cabeceras(espacio_contiguo, bytes_muertos, registros_vivos, tumbas,
                       bytes_muertos_totales);

    return espacio_contiguo == espacio_contiguo_ && bytes_muertos == bytes_muertos_ &&
           registros_vivos == registros_vivos_ && tumbas == tumbas_ &&
           bytes_muertos_totales == bytes_muertos_totales_;
}

std::uint32_t HeapFile::crear_pagina() {
    const std::uint32_t nueva = num_paginas_;

    PaginaSlotted pagina(buffer_.data());
    pagina.inicializar();
    HEAP_VERIFICAR(pagina);
    escribir_pagina(nueva, buffer_.data());

    if (ultima_pagina_ != PAGINA_INVALIDA) {
        leer_pagina(ultima_pagina_, buffer_.data());
        pagina.set_next_page(nueva);
        escribir_pagina(ultima_pagina_, buffer_.data());
    } else {
        primera_pagina_ = nueva;
    }

    ultima_pagina_ = nueva;
    num_paginas_ += 1;
    espacio_contiguo_.push_back(ESPACIO_UTIL_PAGINA);
    bytes_muertos_.push_back(0);
    cursor_ = nueva;

    escribir_cabecera_archivo();
    return nueva;
}

std::uint32_t HeapFile::seleccionar_pagina(std::uint16_t necesario) {
    std::uint32_t mejor_compactable = PAGINA_INVALIDA;
    std::uint16_t mejor_muertos = 0;

    for (std::uint32_t paso = 0; paso < num_paginas_; ++paso) {
        const std::uint32_t page_id = (cursor_ + paso) % num_paginas_;
        if (espacio_contiguo_[page_id] >= necesario) {
            cursor_ = page_id;
            return page_id;
        }
        const std::uint32_t recuperable =
            static_cast<std::uint32_t>(espacio_contiguo_[page_id]) + bytes_muertos_[page_id];
        if (recuperable >= necesario && bytes_muertos_[page_id] > mejor_muertos) {
            mejor_muertos = bytes_muertos_[page_id];
            mejor_compactable = page_id;
        }
    }

    if (mejor_compactable != PAGINA_INVALIDA) {
        compactar_pagina(mejor_compactable);
        cursor_ = mejor_compactable;
        return mejor_compactable;
    }
    return PAGINA_INVALIDA;
}

bool HeapFile::compactar_pagina(std::uint32_t page_id) {
    if (page_id >= num_paginas_ || bytes_muertos_[page_id] == 0) {
        return false;
    }

    leer_pagina(page_id, buffer_.data());
    PaginaSlotted pagina(buffer_.data());
    pagina.compactar();
    HEAP_VERIFICAR(pagina);
    escribir_pagina(page_id, buffer_.data());

    bytes_muertos_totales_ -= bytes_muertos_[page_id];
    bytes_muertos_[page_id] = 0;
    espacio_contiguo_[page_id] = pagina.espacio_contiguo();
    cursor_ = std::min(cursor_, page_id);
    return true;
}

RecordId HeapFile::insertar_bytes(const std::byte* datos, std::uint16_t largo) {
    if (largo > CAPACIDAD_MAXIMA_REGISTRO) {
        throw std::invalid_argument("el registro no cabe en una pagina");
    }
    const std::uint16_t necesario = static_cast<std::uint16_t>(largo + SLOT_SIZE);

    std::uint32_t destino = seleccionar_pagina(necesario);
    if (destino == PAGINA_INVALIDA) {
        destino = crear_pagina();
    }

    leer_pagina(destino, buffer_.data());
    PaginaSlotted pagina(buffer_.data());
    const std::uint16_t slot_id = pagina.insertar(datos, largo);
    if (slot_id == SLOT_INVALIDO) {
        throw std::runtime_error("mapa de espacio libre desincronizado con el disco");
    }
    HEAP_VERIFICAR(pagina);
    escribir_pagina(destino, buffer_.data());

    espacio_contiguo_[destino] = pagina.espacio_contiguo();
    ++registros_vivos_;

    RecordId rid;
    rid.page_id = destino;
    rid.slot_id = slot_id;
    return rid;
}

std::optional<std::vector<std::byte>> HeapFile::obtener(RecordId rid) const {
    if (rid.page_id >= num_paginas_) {
        return std::nullopt;
    }
    leer_pagina(rid.page_id, buffer_.data());
    PaginaSlotted pagina(buffer_.data());

    const std::byte* datos = nullptr;
    std::uint16_t largo = 0;
    if (!pagina.obtener(rid.slot_id, datos, largo)) {
        return std::nullopt;
    }
    return std::vector<std::byte>(datos, datos + largo);
}

bool HeapFile::eliminar_rid(RecordId rid) {
    if (rid.page_id >= num_paginas_) {
        return false;
    }
    leer_pagina(rid.page_id, buffer_.data());
    PaginaSlotted pagina(buffer_.data());

    const std::uint16_t muertos_antes = pagina.dead_bytes();
    if (!pagina.eliminar(rid.slot_id)) {
        return false;
    }
    const std::uint16_t liberados =
        static_cast<std::uint16_t>(pagina.dead_bytes() - muertos_antes);
    HEAP_VERIFICAR(pagina);
    escribir_pagina(rid.page_id, buffer_.data());

    bytes_muertos_[rid.page_id] = pagina.dead_bytes();
    bytes_muertos_totales_ += liberados;
    ++tumbas_;
    --registros_vivos_;
    cursor_ = std::min(cursor_, rid.page_id);
    return true;
}

EstadisticasHeap HeapFile::stats_heap() const {
    EstadisticasHeap estadisticas;
    estadisticas.num_paginas = num_paginas_;
    estadisticas.registros_vivos = registros_vivos_;
    estadisticas.tumbas = tumbas_;
    estadisticas.bytes_desperdiciados = bytes_muertos_totales_;
    estadisticas.tamano_archivo_bytes = tamano_en_disco();

    std::size_t libres = 0;
    for (const std::uint16_t espacio : espacio_contiguo_) {
        libres += espacio;
    }
    estadisticas.bytes_libres = libres;
    return estadisticas;
}

std::uint32_t HeapFile::num_paginas() const noexcept {
    return num_paginas_;
}

std::uint32_t HeapFile::primera_pagina() const noexcept {
    return primera_pagina_;
}

std::uint32_t HeapFile::ultima_pagina() const noexcept {
    return ultima_pagina_;
}

std::size_t HeapFile::tamano_en_disco() const {
    archivo_.flush();
    archivo_.seekg(0, std::ios::end);
    return static_cast<std::size_t>(archivo_.tellg());
}

std::size_t HeapFile::paginas_leidas() const noexcept {
    return paginas_leidas_;
}

std::size_t HeapFile::paginas_escritas() const noexcept {
    return paginas_escritas_;
}

void HeapFile::reiniciar_contadores() noexcept {
    paginas_leidas_ = 0;
    paginas_escritas_ = 0;
}

bool HeapFile::verificar_cadena() const {
    if (num_paginas_ == 0) {
        return primera_pagina_ == PAGINA_INVALIDA && ultima_pagina_ == PAGINA_INVALIDA;
    }
    if (primera_pagina_ != 0 || ultima_pagina_ != num_paginas_ - 1) {
        return false;
    }

    PaginaSlotted pagina(buffer_.data());
    std::uint32_t actual = primera_pagina_;
    for (std::uint32_t visitadas = 0; visitadas < num_paginas_; ++visitadas) {
        if (actual != visitadas) {
            return false;
        }
        leer_pagina(actual, buffer_.data());
        const std::uint32_t siguiente = pagina.next_page();
        if (visitadas + 1 == num_paginas_) {
            return siguiente == PAGINA_INVALIDA;
        }
        actual = siguiente;
    }
    return false;
}


std::vector<std::byte> codificar_registro(const Registro& registro) {
    const std::size_t largo_valor = registro.valor.size();
    std::vector<std::byte> bytes(CABECERA_REGISTRO + largo_valor);
    escribir_campo<std::int32_t>(bytes.data(), 0, static_cast<std::int32_t>(registro.clave));
    escribir_campo<std::uint32_t>(bytes.data(), 4, static_cast<std::uint32_t>(largo_valor));
    if (largo_valor > 0) {
        std::memcpy(bytes.data() + CABECERA_REGISTRO, registro.valor.data(), largo_valor);
    }
    return bytes;
}

bool leer_clave(const std::byte* datos, std::uint16_t largo, int& clave) {
    if (largo < CABECERA_REGISTRO) {
        return false;
    }
    clave = static_cast<int>(leer_campo<std::int32_t>(datos, 0));
    return true;
}

bool decodificar_registro(const std::byte* datos, std::uint16_t largo, Registro& salida) {
    if (largo < CABECERA_REGISTRO) {
        return false;
    }
    const std::uint32_t largo_valor = leer_campo<std::uint32_t>(datos, 4);
    if (CABECERA_REGISTRO + static_cast<std::size_t>(largo_valor) != largo) {
        return false;
    }
    salida.clave = static_cast<int>(leer_campo<std::int32_t>(datos, 0));
    salida.valor.assign(reinterpret_cast<const char*>(datos + CABECERA_REGISTRO), largo_valor);
    return true;
}

void HeapFile::recorrer(const VisitanteRegistro& visitante) const {
    PaginaSlotted pagina(buffer_.data());
    for (std::uint32_t page_id = 0; page_id < num_paginas_; ++page_id) {
        leer_pagina(page_id, buffer_.data());
        const std::uint16_t total_slots = pagina.slot_count();
        for (std::uint16_t slot_id = 0; slot_id < total_slots; ++slot_id) {
            const std::byte* datos = nullptr;
            std::uint16_t largo = 0;
            if (!pagina.obtener(slot_id, datos, largo)) {
                continue;
            }
            RecordId rid;
            rid.page_id = page_id;
            rid.slot_id = slot_id;
            if (!visitante(rid, datos, largo)) {
                return;
            }
        }
    }
}

bool HeapFile::insertar(const Registro& registro) {
    if (registro.valor.size() > CAPACIDAD_MAXIMA_VALOR) {
        return false;
    }
    const std::vector<std::byte> bytes = codificar_registro(registro);
    try {
        insertar_bytes(bytes.data(), static_cast<std::uint16_t>(bytes.size()));
    } catch (const std::invalid_argument&) {
        return false;
    }
    return true;
}

bool HeapFile::eliminar(int clave) {
    RecordId encontrado;
    bool hallado = false;
    recorrer([&](const RecordId& rid, const std::byte* datos, std::uint16_t largo) {
        int clave_actual = 0;
        if (leer_clave(datos, largo, clave_actual) && clave_actual == clave) {
            encontrado = rid;
            hallado = true;
            return false;
        }
        return true;
    });
    if (!hallado) {
        return false;
    }
    return eliminar_rid(encontrado);
}

std::vector<Registro> HeapFile::scan() const {
    std::vector<Registro> resultado;
    resultado.reserve(registros_vivos_);
    recorrer([&](const RecordId&, const std::byte* datos, std::uint16_t largo) {
        Registro registro;
        if (decodificar_registro(datos, largo, registro)) {
            resultado.push_back(std::move(registro));
        }
        return true;
    });
    return resultado;
}

std::optional<Registro> HeapFile::buscar(int clave) const {
    Registro encontrado;
    bool hallado = false;
    recorrer([&](const RecordId&, const std::byte* datos, std::uint16_t largo) {
        int clave_actual = 0;
        if (!leer_clave(datos, largo, clave_actual) || clave_actual != clave) {
            return true;
        }
        hallado = decodificar_registro(datos, largo, encontrado);
        return false;
    });
    if (!hallado) {
        return std::nullopt;
    }
    return encontrado;
}

EstadisticasArchivo HeapFile::stats() const {
    EstadisticasArchivo estadisticas;
    estadisticas.registros = registros_vivos_;
    estadisticas.tumbas = tumbas_;
    estadisticas.paginas = num_paginas_;
    estadisticas.registros_auxiliares = 0;

    const std::size_t total_slots = registros_vivos_ + tumbas_;
    estadisticas.porcentaje_desperdicio =
        total_slots == 0 ? 0.0 : static_cast<double>(tumbas_) / static_cast<double>(total_slots);
    estadisticas.umbral_reorganizacion = 0.0;
    estadisticas.ultima_reorganizacion_us = ultima_reorganizacion_us_;
    return estadisticas;
}

void HeapFile::reorganizar() {
    const auto inicio = std::chrono::steady_clock::now();
    for (std::uint32_t page_id = 0; page_id < num_paginas_; ++page_id) {
        if (bytes_muertos_[page_id] > 0) {
            compactar_pagina(page_id);
        }
    }
    cursor_ = 0;
    const auto fin = std::chrono::steady_clock::now();
    ultima_reorganizacion_us_ =
        std::chrono::duration_cast<std::chrono::microseconds>(fin - inicio).count();
}

}  // namespace motor
