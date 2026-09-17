#include "sequential_file.h"

#include "pagina_slotted.h"
#include "serializacion.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace motor {

namespace {

// --- registro: mismo layout que codificar_registro del heap ---

constexpr std::uint16_t CABECERA_REG = 8;

std::vector<std::byte> codificar(const Registro& registro) {
    std::vector<std::byte> bytes(CABECERA_REG + registro.valor.size());
    escribir_campo<std::int32_t>(bytes.data(), 0, static_cast<std::int32_t>(registro.clave));
    escribir_campo<std::uint32_t>(bytes.data(), 4, static_cast<std::uint32_t>(registro.valor.size()));
    if (!registro.valor.empty()) {
        std::memcpy(bytes.data() + CABECERA_REG, registro.valor.data(), registro.valor.size());
    }
    return bytes;
}

bool decodificar(const std::byte* datos, std::uint16_t largo, Registro& salida) {
    if (largo < CABECERA_REG) return false;
    const std::uint32_t largo_valor = leer_campo<std::uint32_t>(datos, 4);
    if (CABECERA_REG + static_cast<std::size_t>(largo_valor) != largo) return false;
    salida.clave = static_cast<int>(leer_campo<std::int32_t>(datos, 0));
    salida.valor.assign(reinterpret_cast<const char*>(datos + CABECERA_REG), largo_valor);
    return true;
}

// --- página ordenada ---
//
//  0  u16 slot_count     4  u16 live_count    8  u32 reservado
//  2  u16 free_ptr       6  u16 dead_bytes    12 directorio: (offset u16, largo u16) por slot
//
// El directorio va en orden de clave; los datos crecen desde el final. Una tumba
// conserva offset y largo (con el bit alto encendido) para que su clave siga
// sirviendo de frontera y su espacio cuente como desperdiciado hasta reorganizar.

constexpr std::uint16_t CAB_PAGINA = 12;
constexpr std::uint16_t TAM_SLOT = 4;
constexpr std::uint16_t BIT_TUMBA = 0x8000;
constexpr std::uint16_t ESPACIO_UTIL = static_cast<std::uint16_t>(PAGE_SIZE - CAB_PAGINA);
constexpr std::uint16_t REGISTRO_MAXIMO = static_cast<std::uint16_t>(ESPACIO_UTIL - TAM_SLOT);
constexpr std::uint16_t VALOR_MAXIMO = static_cast<std::uint16_t>(REGISTRO_MAXIMO - CABECERA_REG);

class PaginaOrdenada {
public:
    explicit PaginaOrdenada(std::byte* buffer) noexcept : b_(buffer) {}

    void inicializar() {
        std::memset(b_, 0, PAGE_SIZE);
        set_free_ptr(static_cast<std::uint16_t>(PAGE_SIZE));
    }

    std::uint16_t slot_count() const { return leer_campo<std::uint16_t>(b_, 0); }
    std::uint16_t free_ptr() const { return leer_campo<std::uint16_t>(b_, 2); }
    std::uint16_t live_count() const { return leer_campo<std::uint16_t>(b_, 4); }
    std::uint16_t dead_bytes() const { return leer_campo<std::uint16_t>(b_, 6); }

    std::uint16_t bytes_usados() const {
        return static_cast<std::uint16_t>(PAGE_SIZE - free_ptr() + slot_count() * TAM_SLOT);
    }
    std::uint16_t espacio_contiguo() const {
        const std::size_t fin_directorio = CAB_PAGINA + static_cast<std::size_t>(slot_count()) * TAM_SLOT;
        return free_ptr() > fin_directorio ? static_cast<std::uint16_t>(free_ptr() - fin_directorio) : 0;
    }
    bool cabe(std::uint16_t largo) const {
        return espacio_contiguo() >= static_cast<std::uint32_t>(largo) + TAM_SLOT;
    }

    bool es_tumba(std::uint16_t slot) const { return (largo_crudo(slot) & BIT_TUMBA) != 0; }
    std::uint16_t largo(std::uint16_t slot) const {
        return static_cast<std::uint16_t>(largo_crudo(slot) & ~BIT_TUMBA);
    }
    const std::byte* datos(std::uint16_t slot) const { return b_ + offset(slot); }
    int clave(std::uint16_t slot) const {
        return static_cast<int>(leer_campo<std::int32_t>(b_, offset(slot)));
    }

    // primer slot con clave >= buscada (tumbas incluidas: también están ordenadas)
    std::uint16_t posicion(int buscada) const {
        std::uint16_t inicio = 0;
        std::uint16_t fin = slot_count();
        while (inicio < fin) {
            const std::uint16_t medio = static_cast<std::uint16_t>((inicio + fin) / 2);
            if (clave(medio) < buscada) inicio = static_cast<std::uint16_t>(medio + 1);
            else fin = medio;
        }
        return inicio;
    }

    // primer slot con clave > buscada: los repetidos se insertan después, en orden de llegada
    std::uint16_t posicion_despues(int buscada) const {
        std::uint16_t inicio = 0;
        std::uint16_t fin = slot_count();
        while (inicio < fin) {
            const std::uint16_t medio = static_cast<std::uint16_t>((inicio + fin) / 2);
            if (clave(medio) <= buscada) inicio = static_cast<std::uint16_t>(medio + 1);
            else fin = medio;
        }
        return inicio;
    }

    // abre hueco en el directorio en la posición dada y copia los datos
    void insertar_en(std::uint16_t pos, const std::byte* registro, std::uint16_t largo) {
        const std::uint16_t n = slot_count();
        const std::uint16_t nuevo_offset = static_cast<std::uint16_t>(free_ptr() - largo);
        std::memcpy(b_ + nuevo_offset, registro, largo);
        std::memmove(b_ + despl_slot(static_cast<std::uint16_t>(pos + 1)), b_ + despl_slot(pos),
                     static_cast<std::size_t>(n - pos) * TAM_SLOT);
        escribir_slot(pos, nuevo_offset, largo);
        set_free_ptr(nuevo_offset);
        set_slot_count(static_cast<std::uint16_t>(n + 1));
        set_live_count(static_cast<std::uint16_t>(live_count() + 1));
    }

    void eliminar(std::uint16_t slot) {
        const std::uint16_t largo_vivo = largo(slot);
        escribir_slot(slot, offset(slot), static_cast<std::uint16_t>(largo_vivo | BIT_TUMBA));
        set_live_count(static_cast<std::uint16_t>(live_count() - 1));
        set_dead_bytes(static_cast<std::uint16_t>(dead_bytes() + largo_vivo));
    }

    bool verificar() const {
        const std::uint16_t n = slot_count();
        if (CAB_PAGINA + static_cast<std::size_t>(n) * TAM_SLOT > free_ptr() || free_ptr() > PAGE_SIZE) return false;
        std::size_t vivos = 0, bytes_vivos = 0, bytes_muertos = 0;
        for (std::uint16_t i = 0; i < n; ++i) {
            if (offset(i) < free_ptr() || static_cast<std::size_t>(offset(i)) + largo(i) > PAGE_SIZE) return false;
            if (largo(i) < CABECERA_REG) return false;
            if (i > 0 && clave(i) < clave(static_cast<std::uint16_t>(i - 1))) return false;
            if (es_tumba(i)) bytes_muertos += largo(i);
            else { ++vivos; bytes_vivos += largo(i); }
        }
        return vivos == live_count() && bytes_muertos == dead_bytes() &&
               PAGE_SIZE - free_ptr() == bytes_vivos + bytes_muertos;
    }

private:
    static std::size_t despl_slot(std::uint16_t slot) { return CAB_PAGINA + static_cast<std::size_t>(slot) * TAM_SLOT; }
    std::uint16_t offset(std::uint16_t slot) const { return leer_campo<std::uint16_t>(b_, despl_slot(slot)); }
    std::uint16_t largo_crudo(std::uint16_t slot) const { return leer_campo<std::uint16_t>(b_, despl_slot(slot) + 2); }
    void escribir_slot(std::uint16_t slot, std::uint16_t off, std::uint16_t largo) {
        escribir_campo<std::uint16_t>(b_, despl_slot(slot), off);
        escribir_campo<std::uint16_t>(b_, despl_slot(slot) + 2, largo);
    }
    void set_slot_count(std::uint16_t v) { escribir_campo<std::uint16_t>(b_, 0, v); }
    void set_free_ptr(std::uint16_t v) { escribir_campo<std::uint16_t>(b_, 2, v); }
    void set_live_count(std::uint16_t v) { escribir_campo<std::uint16_t>(b_, 4, v); }
    void set_dead_bytes(std::uint16_t v) { escribir_campo<std::uint16_t>(b_, 6, v); }

    std::byte* b_;
};

// --- cabecera del archivo (página 0) ---

constexpr std::size_t D_MAGIC = 0, D_VERSION = 4, D_PAGE_SIZE = 8, D_NUM_PAGINAS = 12,
                      D_PRINCIPAL = 16, D_UMBRAL = 20, D_FACTOR = 28, D_ULTIMA_REORG = 36,
                      D_TOTAL_REORG = 44, D_REORGS = 52;

bool menor_clave(const Registro& a, const Registro& b) { return a.clave < b.clave; }

}  // namespace

std::ostream& operator<<(std::ostream& salida, const EstadisticasSecuencial& e) {
    salida << "principal=" << e.paginas_principal << " pag, auxiliar=" << e.paginas_auxiliares
           << " pag, vivos=" << e.registros_vivos << " (aux " << e.registros_auxiliares
           << "), tumbas=" << e.tumbas << ", bytes_desperdiciados=" << e.bytes_desperdiciados
           << ", desperdicio=" << e.porcentaje_desperdicio * 100.0 << "%, archivo="
           << e.tamano_archivo_bytes << " B, reorganizaciones=" << e.reorganizaciones;
    return salida;
}

// --- apertura y cabecera ---

SequentialFile::SequentialFile(const std::string& ruta, bool truncar, double umbral_reorganizacion,
                               double factor_llenado)
    : ruta_(ruta), buffer_(PAGE_SIZE), buffer_aux_(PAGE_SIZE) {
    set_umbral_reorganizacion(umbral_reorganizacion);
    if (!std::isfinite(factor_llenado) || factor_llenado <= 0.0 || factor_llenado > 1.0) {
        throw std::invalid_argument("El factor de llenado debe estar en (0, 1]");
    }
    factor_llenado_ = factor_llenado;
    abrir_o_crear(ruta, truncar);
}

SequentialFile::~SequentialFile() {
    if (archivo_.is_open()) {
        escribir_cabecera_archivo();
        archivo_.flush();
        archivo_.close();
    }
}

std::streamoff SequentialFile::offset_de(std::uint32_t page_id) noexcept {
    return static_cast<std::streamoff>(page_id + 1) * static_cast<std::streamoff>(PAGE_SIZE);
}

void SequentialFile::abrir_o_crear(const std::string& ruta, bool truncar) {
    bool existe = false;
    {
        std::ifstream sonda(ruta, std::ios::binary);
        existe = sonda.good();
    }
    if (!existe || truncar) {
        std::ofstream nuevo(ruta, std::ios::binary | std::ios::trunc);
        if (!nuevo) throw std::runtime_error("no se pudo crear el archivo: " + ruta);
        std::array<std::byte, PAGE_SIZE> cabecera{};
        nuevo.write(reinterpret_cast<const char*>(cabecera.data()), PAGE_SIZE);
        nuevo.close();
        num_paginas_ = 0;
        paginas_principal_ = 0;
        abrir(ruta);
        escribir_cabecera_archivo();
        return;
    }
    abrir(ruta);
    leer_cabecera_archivo();
    recontar();
}

void SequentialFile::abrir(const std::string& ruta) {
    archivo_.open(ruta, std::ios::in | std::ios::out | std::ios::binary);
    if (!archivo_) throw std::runtime_error("no se pudo abrir el archivo: " + ruta);
}

void SequentialFile::escribir_cabecera_archivo() {
    std::array<std::byte, PAGE_SIZE> c{};
    escribir_campo<std::uint32_t>(c.data(), D_MAGIC, MAGIC_SEQ);
    escribir_campo<std::uint16_t>(c.data(), D_VERSION, VERSION_SEQ);
    escribir_campo<std::uint32_t>(c.data(), D_PAGE_SIZE, static_cast<std::uint32_t>(PAGE_SIZE));
    escribir_campo<std::uint32_t>(c.data(), D_NUM_PAGINAS, num_paginas_);
    escribir_campo<std::uint32_t>(c.data(), D_PRINCIPAL, paginas_principal_);
    escribir_campo<double>(c.data(), D_UMBRAL, umbral_reorganizacion_);
    escribir_campo<double>(c.data(), D_FACTOR, factor_llenado_);
    escribir_campo<std::int64_t>(c.data(), D_ULTIMA_REORG, ultima_reorganizacion_us_);
    escribir_campo<std::int64_t>(c.data(), D_TOTAL_REORG, tiempo_reorganizaciones_us_);
    escribir_campo<std::uint64_t>(c.data(), D_REORGS, reorganizaciones_);
    archivo_.seekp(0);
    archivo_.write(reinterpret_cast<const char*>(c.data()), PAGE_SIZE);
    archivo_.flush();
}

void SequentialFile::leer_cabecera_archivo() {
    std::array<std::byte, PAGE_SIZE> c{};
    archivo_.seekg(0);
    archivo_.read(reinterpret_cast<char*>(c.data()), PAGE_SIZE);
    if (archivo_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        archivo_.clear();
        throw std::invalid_argument("cabecera de archivo incompleta");
    }
    if (leer_campo<std::uint32_t>(c.data(), D_MAGIC) != MAGIC_SEQ) {
        throw std::invalid_argument("el archivo no es un sequential file (magic invalido)");
    }
    if (leer_campo<std::uint16_t>(c.data(), D_VERSION) != VERSION_SEQ) {
        throw std::invalid_argument("version de sequential file no soportada");
    }
    if (leer_campo<std::uint32_t>(c.data(), D_PAGE_SIZE) != static_cast<std::uint32_t>(PAGE_SIZE)) {
        throw std::invalid_argument("page_size del archivo incompatible con el compilado");
    }
    num_paginas_ = leer_campo<std::uint32_t>(c.data(), D_NUM_PAGINAS);
    paginas_principal_ = leer_campo<std::uint32_t>(c.data(), D_PRINCIPAL);
    umbral_reorganizacion_ = leer_campo<double>(c.data(), D_UMBRAL);
    factor_llenado_ = leer_campo<double>(c.data(), D_FACTOR);
    ultima_reorganizacion_us_ = leer_campo<std::int64_t>(c.data(), D_ULTIMA_REORG);
    tiempo_reorganizaciones_us_ = leer_campo<std::int64_t>(c.data(), D_TOTAL_REORG);
    reorganizaciones_ = static_cast<std::size_t>(leer_campo<std::uint64_t>(c.data(), D_REORGS));
}

// los contadores no se persisten en cada operación: se reconstruyen leyendo
// la cabecera de cada página al abrir, como hace el heap
void SequentialFile::recontar() {
    registros_vivos_ = registros_auxiliares_ = tumbas_ = bytes_muertos_ = 0;
    std::array<std::byte, CAB_PAGINA> c{};
    for (std::uint32_t p = 0; p < num_paginas_; ++p) {
        archivo_.seekg(offset_de(p));
        archivo_.read(reinterpret_cast<char*>(c.data()), CAB_PAGINA);
        if (archivo_.gcount() != static_cast<std::streamsize>(CAB_PAGINA)) {
            archivo_.clear();
            throw std::runtime_error("lectura de cabecera de pagina incompleta");
        }
        ++paginas_leidas_;
        const std::uint16_t slots = leer_campo<std::uint16_t>(c.data(), 0);
        const std::uint16_t vivos = leer_campo<std::uint16_t>(c.data(), 4);
        registros_vivos_ += vivos;
        tumbas_ += static_cast<std::size_t>(slots) - vivos;
        bytes_muertos_ += leer_campo<std::uint16_t>(c.data(), 6);
        if (p >= paginas_principal_) registros_auxiliares_ += vivos;
    }
}

void SequentialFile::leer_pagina(std::uint32_t page_id, std::byte* destino) const {
    archivo_.seekg(offset_de(page_id));
    archivo_.read(reinterpret_cast<char*>(destino), PAGE_SIZE);
    if (archivo_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        archivo_.clear();
        throw std::runtime_error("lectura de pagina incompleta");
    }
    ++paginas_leidas_;
}

void SequentialFile::escribir_pagina(std::uint32_t page_id, const std::byte* origen) {
    archivo_.seekp(offset_de(page_id));
    archivo_.write(reinterpret_cast<const char*>(origen), PAGE_SIZE);
    if (!archivo_) throw std::runtime_error("escritura de pagina fallida");
    ++paginas_escritas_;
}

std::uint32_t SequentialFile::crear_pagina() {
    const std::uint32_t nueva = num_paginas_++;
    PaginaOrdenada pagina(buffer_.data());
    pagina.inicializar();
    escribir_pagina(nueva, buffer_.data());
    escribir_cabecera_archivo();
    return nueva;
}

// --- búsqueda ---

std::uint32_t SequentialFile::pagina_para(int clave) const {
    PaginaOrdenada pagina(buffer_.data());
    std::uint32_t inicio = 0;
    std::uint32_t fin = paginas_principal_;
    while (inicio < fin) {
        const std::uint32_t medio = inicio + (fin - inicio) / 2;
        leer_pagina(medio, buffer_.data());
        if (pagina.clave(0) <= clave) inicio = medio + 1;
        else fin = medio;
    }
    return inicio == 0 ? 0 : inicio - 1;
}

// deja la página encontrada en buffer_
bool SequentialFile::localizar(int clave, Ubicacion& donde) const {
    PaginaOrdenada pagina(buffer_.data());
    if (paginas_principal_ > 0) {
        const std::uint32_t p = pagina_para(clave);
        leer_pagina(p, buffer_.data());
        for (std::uint16_t i = pagina.posicion(clave); i < pagina.slot_count() && pagina.clave(i) == clave; ++i) {
            if (!pagina.es_tumba(i)) {
                donde = {p, i};
                return true;
            }
        }
    }
    for (std::uint32_t p = paginas_principal_; p < num_paginas_; ++p) {
        leer_pagina(p, buffer_.data());
        for (std::uint16_t i = 0; i < pagina.slot_count(); ++i) {
            if (!pagina.es_tumba(i) && pagina.clave(i) == clave) {
                donde = {p, i};
                return true;
            }
        }
    }
    return false;
}

std::optional<Registro> SequentialFile::buscar(int clave) const {
    Ubicacion donde{};
    if (!localizar(clave, donde)) return std::nullopt;
    PaginaOrdenada pagina(buffer_.data());
    Registro registro;
    if (!decodificar(pagina.datos(donde.slot), pagina.largo(donde.slot), registro)) return std::nullopt;
    return registro;
}

std::vector<Registro> SequentialFile::buscar_rango(int desde, int hasta) const {
    std::vector<Registro> principales;
    std::vector<Registro> auxiliares;
    if (desde > hasta) return principales;

    PaginaOrdenada pagina(buffer_.data());
    if (paginas_principal_ > 0) {
        bool terminar = false;
        for (std::uint32_t p = pagina_para(desde); p < paginas_principal_ && !terminar; ++p) {
            leer_pagina(p, buffer_.data());
            for (std::uint16_t i = pagina.posicion(desde); i < pagina.slot_count(); ++i) {
                if (pagina.clave(i) > hasta) { terminar = true; break; }
                Registro registro;
                if (!pagina.es_tumba(i) && decodificar(pagina.datos(i), pagina.largo(i), registro)) {
                    principales.push_back(std::move(registro));
                }
            }
        }
    }
    for (std::uint32_t p = paginas_principal_; p < num_paginas_; ++p) {
        leer_pagina(p, buffer_.data());
        for (std::uint16_t i = 0; i < pagina.slot_count(); ++i) {
            if (pagina.es_tumba(i)) continue;
            const int clave = pagina.clave(i);
            Registro registro;
            if (clave >= desde && clave <= hasta && decodificar(pagina.datos(i), pagina.largo(i), registro)) {
                auxiliares.push_back(std::move(registro));
            }
        }
    }
    std::stable_sort(auxiliares.begin(), auxiliares.end(), menor_clave);
    std::vector<Registro> resultado;
    resultado.reserve(principales.size() + auxiliares.size());
    std::merge(principales.begin(), principales.end(), auxiliares.begin(), auxiliares.end(),
               std::back_inserter(resultado), menor_clave);
    return resultado;
}

std::vector<Registro> SequentialFile::scan() const {
    return buscar_rango(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
}

// --- inserción ---

bool SequentialFile::insertar(const Registro& registro) {
    if (registro.valor.size() > VALOR_MAXIMO) return false;
    const std::vector<std::byte> bytes = codificar(registro);
    const std::uint16_t largo = static_cast<std::uint16_t>(bytes.size());
    PaginaOrdenada pagina(buffer_.data());

    if (paginas_principal_ == 0) {
        // primer registro: nace el área principal
        const std::uint32_t p = crear_pagina();
        paginas_principal_ = 1;
        pagina.inicializar();
        pagina.insertar_en(0, bytes.data(), largo);
        escribir_pagina(p, buffer_.data());
        escribir_cabecera_archivo();
        ++registros_vivos_;
        return true;
    }

    const std::uint32_t p = pagina_para(registro.clave);
    leer_pagina(p, buffer_.data());
    if (pagina.cabe(largo)) {
        pagina.insertar_en(pagina.posicion_despues(registro.clave), bytes.data(), largo);
        escribir_pagina(p, buffer_.data());
        ++registros_vivos_;
    } else {
        insertar_auxiliar(bytes.data(), largo);
    }
    reorganizar_si_corresponde();
    return true;
}

void SequentialFile::insertar_auxiliar(const std::byte* datos, std::uint16_t largo) {
    PaginaOrdenada pagina(buffer_.data());
    std::uint32_t p = num_paginas_ - 1;
    bool hay_sitio = false;
    if (num_paginas_ > paginas_principal_) {
        leer_pagina(p, buffer_.data());
        hay_sitio = pagina.cabe(largo);
    }
    if (!hay_sitio) {
        p = crear_pagina();
        pagina.inicializar();
    }
    pagina.insertar_en(pagina.slot_count(), datos, largo);  // orden de llegada
    escribir_pagina(p, buffer_.data());
    ++registros_vivos_;
    ++registros_auxiliares_;
}

// --- eliminación ---

bool SequentialFile::eliminar(int clave) {
    Ubicacion donde{};
    if (!localizar(clave, donde)) return false;
    PaginaOrdenada pagina(buffer_.data());
    bytes_muertos_ += pagina.largo(donde.slot);
    pagina.eliminar(donde.slot);
    escribir_pagina(donde.pagina, buffer_.data());
    ++tumbas_;
    --registros_vivos_;
    if (donde.pagina >= paginas_principal_) --registros_auxiliares_;
    reorganizar_si_corresponde();
    return true;
}

// --- reorganización ---

double SequentialFile::porcentaje_desperdicio() const {
    const std::size_t total = registros_vivos_ + tumbas_;
    if (total == 0) return 0.0;
    return static_cast<double>(tumbas_ + registros_auxiliares_) / static_cast<double>(total);
}

void SequentialFile::reorganizar_si_corresponde() {
    if (porcentaje_desperdicio() > umbral_reorganizacion_) reorganizar();
}

// fusiona el área principal (ya ordenada) con el auxiliar (ordenado por un índice
// en RAM de clave + ubicación) y escribe páginas nuevas en un archivo temporal
// que luego reemplaza al original. Las tumbas no se copian.
void SequentialFile::reorganizar() {
    const auto inicio = std::chrono::steady_clock::now();

    struct EntradaAux {
        int clave;
        std::uint32_t pagina;
        std::uint16_t slot;
    };
    std::vector<EntradaAux> auxiliares;
    auxiliares.reserve(registros_auxiliares_);
    PaginaOrdenada pagina_aux(buffer_aux_.data());
    for (std::uint32_t p = paginas_principal_; p < num_paginas_; ++p) {
        leer_pagina(p, buffer_aux_.data());
        for (std::uint16_t i = 0; i < pagina_aux.slot_count(); ++i) {
            if (!pagina_aux.es_tumba(i)) auxiliares.push_back({pagina_aux.clave(i), p, i});
        }
    }
    std::stable_sort(auxiliares.begin(), auxiliares.end(),
                     [](const EntradaAux& a, const EntradaAux& b) { return a.clave < b.clave; });

    const std::string ruta_temporal = ruta_ + ".reorg";
    std::ofstream salida(ruta_temporal, std::ios::binary | std::ios::trunc);
    if (!salida) throw std::runtime_error("no se pudo crear el archivo temporal: " + ruta_temporal);
    std::array<std::byte, PAGE_SIZE> cero{};
    salida.write(reinterpret_cast<const char*>(cero.data()), PAGE_SIZE);

    std::vector<std::byte> buffer_salida(PAGE_SIZE);
    PaginaOrdenada destino(buffer_salida.data());
    destino.inicializar();
    std::uint32_t paginas_nuevas = 0;
    const std::size_t limite = static_cast<std::size_t>(ESPACIO_UTIL * factor_llenado_);

    auto volcar = [&]() {
        if (destino.slot_count() == 0) return;
        salida.write(reinterpret_cast<const char*>(buffer_salida.data()), PAGE_SIZE);
        ++paginas_nuevas;
        ++paginas_escritas_;
        destino.inicializar();
    };
    auto emitir = [&](const std::byte* datos, std::uint16_t largo) {
        const std::size_t ocupado = static_cast<std::size_t>(destino.bytes_usados()) + largo + TAM_SLOT;
        const bool pasa_del_factor = ocupado > limite;
        if (destino.slot_count() > 0 && (pasa_del_factor || !destino.cabe(largo))) volcar();
        destino.insertar_en(destino.slot_count(), datos, largo);
    };

    // cursor sobre el área principal, página por página
    PaginaOrdenada principal(buffer_.data());
    std::uint32_t p_principal = 0;
    std::uint16_t s_principal = 0;
    auto avanzar_principal = [&]() {
        while (p_principal < paginas_principal_) {
            if (s_principal == 0) leer_pagina(p_principal, buffer_.data());
            while (s_principal < principal.slot_count()) {
                if (!principal.es_tumba(s_principal)) return true;
                ++s_principal;
            }
            ++p_principal;
            s_principal = 0;
        }
        return false;
    };
    std::uint32_t pagina_aux_cargada = PAGINA_INVALIDA;
    std::size_t a = 0;

    bool hay_principal = avanzar_principal();
    while (hay_principal || a < auxiliares.size()) {
        const bool tomar_principal =
            hay_principal && (a >= auxiliares.size() || principal.clave(s_principal) <= auxiliares[a].clave);
        if (tomar_principal) {
            emitir(principal.datos(s_principal), principal.largo(s_principal));
            ++s_principal;
            hay_principal = avanzar_principal();
        } else {
            if (auxiliares[a].pagina != pagina_aux_cargada) {
                leer_pagina(auxiliares[a].pagina, buffer_aux_.data());
                pagina_aux_cargada = auxiliares[a].pagina;
            }
            emitir(pagina_aux.datos(auxiliares[a].slot), pagina_aux.largo(auxiliares[a].slot));
            ++a;
        }
    }
    volcar();
    salida.close();
    if (!salida) throw std::runtime_error("fallo al escribir el archivo temporal");

    archivo_.close();
    std::filesystem::remove(ruta_);
    std::filesystem::rename(ruta_temporal, ruta_);
    abrir(ruta_);

    num_paginas_ = paginas_nuevas;
    paginas_principal_ = paginas_nuevas;
    tumbas_ = 0;
    bytes_muertos_ = 0;
    registros_auxiliares_ = 0;

    const auto fin = std::chrono::steady_clock::now();
    ultima_reorganizacion_us_ = std::chrono::duration_cast<std::chrono::microseconds>(fin - inicio).count();
    tiempo_reorganizaciones_us_ += ultima_reorganizacion_us_;
    ++reorganizaciones_;
    escribir_cabecera_archivo();
}

// --- estadísticas ---

EstadisticasArchivo SequentialFile::stats() const {
    EstadisticasArchivo e;
    e.registros = registros_vivos_;
    e.tumbas = tumbas_;
    e.paginas = num_paginas_;
    e.registros_auxiliares = registros_auxiliares_;
    e.porcentaje_desperdicio = porcentaje_desperdicio();
    e.umbral_reorganizacion = umbral_reorganizacion_;
    e.ultima_reorganizacion_us = ultima_reorganizacion_us_;
    return e;
}

EstadisticasSecuencial SequentialFile::stats_secuencial() const {
    EstadisticasSecuencial e;
    e.paginas_principal = paginas_principal_;
    e.paginas_auxiliares = num_paginas_ - paginas_principal_;
    e.registros_vivos = registros_vivos_;
    e.registros_auxiliares = registros_auxiliares_;
    e.tumbas = tumbas_;
    e.bytes_desperdiciados = bytes_muertos_;
    e.tamano_archivo_bytes = tamano_en_disco();
    e.porcentaje_desperdicio = porcentaje_desperdicio();
    e.reorganizaciones = reorganizaciones_;
    e.ultima_reorganizacion_us = ultima_reorganizacion_us_;
    e.tiempo_reorganizaciones_us = tiempo_reorganizaciones_us_;
    return e;
}

void SequentialFile::set_umbral_reorganizacion(double umbral) {
    if (!std::isfinite(umbral) || umbral < 0.0 || umbral > 1.0) {
        throw std::invalid_argument("El umbral debe estar entre 0 y 1");
    }
    umbral_reorganizacion_ = umbral;
}

double SequentialFile::umbral_reorganizacion() const noexcept { return umbral_reorganizacion_; }
double SequentialFile::factor_llenado() const noexcept { return factor_llenado_; }
std::uint32_t SequentialFile::num_paginas() const noexcept { return num_paginas_; }
std::uint32_t SequentialFile::paginas_principal() const noexcept { return paginas_principal_; }

std::size_t SequentialFile::tamano_en_disco() const {
    archivo_.flush();
    archivo_.seekg(0, std::ios::end);
    return static_cast<std::size_t>(archivo_.tellg());
}

std::size_t SequentialFile::paginas_leidas() const noexcept { return paginas_leidas_; }
std::size_t SequentialFile::paginas_escritas() const noexcept { return paginas_escritas_; }

void SequentialFile::reiniciar_contadores() noexcept {
    paginas_leidas_ = 0;
    paginas_escritas_ = 0;
}

bool SequentialFile::verificar_orden() const {
    PaginaOrdenada pagina(buffer_.data());
    bool hay_anterior = false;
    int anterior = 0;
    for (std::uint32_t p = 0; p < num_paginas_; ++p) {
        leer_pagina(p, buffer_.data());
        if (p < paginas_principal_) {
            if (pagina.slot_count() == 0) return false;
            if (!pagina.verificar()) return false;
            if (hay_anterior && pagina.clave(0) < anterior) return false;
            anterior = pagina.clave(static_cast<std::uint16_t>(pagina.slot_count() - 1));
            hay_anterior = true;
        } else {
            // en el auxiliar no hay orden: solo consistencia interna
            const std::uint16_t n = pagina.slot_count();
            std::size_t vivos = 0;
            for (std::uint16_t i = 0; i < n; ++i) vivos += pagina.es_tumba(i) ? 0 : 1;
            if (vivos != pagina.live_count()) return false;
        }
    }
    return true;
}

}  // namespace motor
