#pragma once

#include <cstddef>
#include <cstdint>

namespace motor {

constexpr std::size_t   PAGE_SIZE       = 4096;
constexpr std::uint16_t HEADER_SIZE     = 12;
constexpr std::uint16_t SLOT_SIZE       = 4;
constexpr std::uint16_t SLOT_TUMBA      = 0xFFFF;
constexpr std::uint16_t SLOT_INVALIDO   = 0xFFFF;
constexpr std::uint32_t PAGINA_INVALIDA = 0xFFFFFFFFu;

constexpr std::uint16_t ESPACIO_UTIL_PAGINA =
    static_cast<std::uint16_t>(PAGE_SIZE - HEADER_SIZE);
constexpr std::uint16_t CAPACIDAD_MAXIMA_REGISTRO =
    static_cast<std::uint16_t>(PAGE_SIZE - HEADER_SIZE - SLOT_SIZE);

struct RecordId {
    std::uint32_t page_id = PAGINA_INVALIDA;
    std::uint16_t slot_id = 0;
};

inline bool operator==(const RecordId& izquierda, const RecordId& derecha) {
    return izquierda.page_id == derecha.page_id && izquierda.slot_id == derecha.slot_id;
}

inline bool operator!=(const RecordId& izquierda, const RecordId& derecha) {
    return !(izquierda == derecha);
}

class PaginaSlotted {
public:
    PaginaSlotted() = default;
    explicit PaginaSlotted(std::byte* buffer) noexcept;

    void enlazar(std::byte* buffer) noexcept;
    void inicializar(std::uint32_t siguiente = PAGINA_INVALIDA);

    bool cabe(std::uint16_t largo) const;
    std::uint16_t insertar(const std::byte* registro, std::uint16_t largo);
    bool obtener(std::uint16_t slot_id, const std::byte*& datos, std::uint16_t& largo) const;
    bool eliminar(std::uint16_t slot_id);
    bool es_tumba(std::uint16_t slot_id) const;
    void compactar();

    std::uint16_t slot_count() const;
    std::uint16_t free_ptr() const;
    std::uint16_t live_count() const;
    std::uint16_t dead_bytes() const;
    std::uint32_t next_page() const;
    void set_next_page(std::uint32_t siguiente);

    std::uint16_t espacio_contiguo() const;
    std::uint16_t espacio_recuperable() const;

    bool verificar_invariantes() const;

private:
    static constexpr std::size_t DESPL_SLOT_COUNT = 0;
    static constexpr std::size_t DESPL_FREE_PTR   = 2;
    static constexpr std::size_t DESPL_NEXT_PAGE  = 4;
    static constexpr std::size_t DESPL_LIVE_COUNT = 8;
    static constexpr std::size_t DESPL_DEAD_BYTES = 10;

    std::size_t desplazamiento_slot(std::uint16_t slot_id) const;
    std::uint16_t offset_de(std::uint16_t slot_id) const;
    std::uint16_t largo_de(std::uint16_t slot_id) const;
    void escribir_slot(std::uint16_t slot_id, std::uint16_t offset, std::uint16_t largo);

    void set_slot_count(std::uint16_t valor);
    void set_free_ptr(std::uint16_t valor);
    void set_live_count(std::uint16_t valor);
    void set_dead_bytes(std::uint16_t valor);

    std::byte* buffer_ = nullptr;
};

}  // namespace motor
