#pragma once

#include "pagina_slotted.h"

#include "../comun/archivo.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace motor {

constexpr std::uint32_t MAGIC_HEAP   = 0x48454150u;
constexpr std::uint16_t VERSION_HEAP = 1;

struct EstadisticasHeap {
    std::size_t num_paginas = 0;
    std::size_t registros_vivos = 0;
    std::size_t tumbas = 0;
    std::size_t bytes_desperdiciados = 0;
    std::size_t bytes_libres = 0;
    std::size_t tamano_archivo_bytes = 0;
};

std::ostream& operator<<(std::ostream& salida, const EstadisticasHeap& estadisticas);

constexpr std::uint16_t CABECERA_REGISTRO = 8;
constexpr std::uint16_t CAPACIDAD_MAXIMA_VALOR = CAPACIDAD_MAXIMA_REGISTRO - CABECERA_REGISTRO;

std::vector<std::byte> codificar_registro(const Registro& registro);
bool decodificar_registro(const std::byte* datos, std::uint16_t largo, Registro& salida);
bool leer_clave(const std::byte* datos, std::uint16_t largo, int& clave);

using VisitanteRegistro =
    std::function<bool(const RecordId&, const std::byte*, std::uint16_t)>;

class HeapFile final : public IFileOrganization {
public:
    explicit HeapFile(const std::string& ruta, bool truncar = false);
    ~HeapFile() override;

    HeapFile(const HeapFile&) = delete;
    HeapFile& operator=(const HeapFile&) = delete;

    RecordId insertar_bytes(const std::byte* datos, std::uint16_t largo);
    std::optional<std::vector<std::byte>> obtener(RecordId rid) const;
    bool eliminar_rid(RecordId rid);
    bool compactar_pagina(std::uint32_t page_id);
    void recorrer(const VisitanteRegistro& visitante) const;

    EstadisticasHeap stats_heap() const;

    bool insertar(const Registro& registro) override;
    bool eliminar(int clave) override;
    std::vector<Registro> scan() const override;
    EstadisticasArchivo stats() const override;
    void reorganizar() override;

    std::optional<Registro> buscar(int clave) const;

    std::uint32_t num_paginas() const noexcept;
    std::uint32_t primera_pagina() const noexcept;
    std::uint32_t ultima_pagina() const noexcept;
    std::size_t tamano_en_disco() const;

    std::size_t paginas_leidas() const noexcept;
    std::size_t paginas_escritas() const noexcept;
    void reiniciar_contadores() noexcept;

    bool verificar_cadena() const;
    bool verificar_mapa() const;

private:
    static constexpr std::size_t DESPL_MAGIC      = 0;
    static constexpr std::size_t DESPL_VERSION    = 4;
    static constexpr std::size_t DESPL_RESERVADO  = 6;
    static constexpr std::size_t DESPL_PAGE_SIZE  = 8;
    static constexpr std::size_t DESPL_NUM_PAGES  = 12;
    static constexpr std::size_t DESPL_FIRST_PAGE = 16;
    static constexpr std::size_t DESPL_LAST_PAGE  = 20;

    static std::streamoff offset_de(std::uint32_t page_id) noexcept;

    void abrir_o_crear(const std::string& ruta, bool truncar);
    void escribir_cabecera_archivo();
    void leer_cabecera_archivo();
    void leer_pagina(std::uint32_t page_id, std::byte* destino) const;
    void escribir_pagina(std::uint32_t page_id, const std::byte* origen);
    std::uint32_t crear_pagina();

    void escanear_cabeceras(std::vector<std::uint16_t>& espacio_contiguo,
                            std::vector<std::uint16_t>& bytes_muertos,
                            std::size_t& registros_vivos, std::size_t& tumbas,
                            std::size_t& bytes_muertos_totales) const;
    void reconstruir_mapa();
    std::uint32_t seleccionar_pagina(std::uint16_t necesario);

    mutable std::fstream archivo_;
    mutable std::vector<std::byte> buffer_;

    std::uint32_t num_paginas_ = 0;
    std::uint32_t primera_pagina_ = PAGINA_INVALIDA;
    std::uint32_t ultima_pagina_ = PAGINA_INVALIDA;

    std::vector<std::uint16_t> espacio_contiguo_;
    std::vector<std::uint16_t> bytes_muertos_;
    std::size_t registros_vivos_ = 0;
    std::size_t tumbas_ = 0;
    std::size_t bytes_muertos_totales_ = 0;
    std::uint32_t cursor_ = 0;
    long long ultima_reorganizacion_us_ = 0;

    mutable std::size_t paginas_leidas_ = 0;
    std::size_t paginas_escritas_ = 0;
};

}  // namespace motor
