#include "pagina_slotted.h"

#include "serializacion.h"

#include <array>
#include <cstring>

namespace motor {

PaginaSlotted::PaginaSlotted(std::byte* buffer) noexcept : buffer_(buffer) {}

void PaginaSlotted::enlazar(std::byte* buffer) noexcept {
    buffer_ = buffer;
}

void PaginaSlotted::inicializar(std::uint32_t siguiente) {
    std::memset(buffer_, 0, PAGE_SIZE);
    set_slot_count(0);
    set_free_ptr(static_cast<std::uint16_t>(PAGE_SIZE));
    set_next_page(siguiente);
    set_live_count(0);
    set_dead_bytes(0);
}

std::size_t PaginaSlotted::desplazamiento_slot(std::uint16_t slot_id) const {
    return HEADER_SIZE + static_cast<std::size_t>(slot_id) * SLOT_SIZE;
}

std::uint16_t PaginaSlotted::offset_de(std::uint16_t slot_id) const {
    return leer_campo<std::uint16_t>(buffer_, desplazamiento_slot(slot_id));
}

std::uint16_t PaginaSlotted::largo_de(std::uint16_t slot_id) const {
    return leer_campo<std::uint16_t>(buffer_, desplazamiento_slot(slot_id) + 2);
}

void PaginaSlotted::escribir_slot(std::uint16_t slot_id, std::uint16_t offset,
                                  std::uint16_t largo) {
    escribir_campo<std::uint16_t>(buffer_, desplazamiento_slot(slot_id), offset);
    escribir_campo<std::uint16_t>(buffer_, desplazamiento_slot(slot_id) + 2, largo);
}

std::uint16_t PaginaSlotted::slot_count() const {
    return leer_campo<std::uint16_t>(buffer_, DESPL_SLOT_COUNT);
}

std::uint16_t PaginaSlotted::free_ptr() const {
    return leer_campo<std::uint16_t>(buffer_, DESPL_FREE_PTR);
}

std::uint16_t PaginaSlotted::live_count() const {
    return leer_campo<std::uint16_t>(buffer_, DESPL_LIVE_COUNT);
}

std::uint16_t PaginaSlotted::dead_bytes() const {
    return leer_campo<std::uint16_t>(buffer_, DESPL_DEAD_BYTES);
}

std::uint32_t PaginaSlotted::next_page() const {
    return leer_campo<std::uint32_t>(buffer_, DESPL_NEXT_PAGE);
}

void PaginaSlotted::set_slot_count(std::uint16_t valor) {
    escribir_campo<std::uint16_t>(buffer_, DESPL_SLOT_COUNT, valor);
}

void PaginaSlotted::set_free_ptr(std::uint16_t valor) {
    escribir_campo<std::uint16_t>(buffer_, DESPL_FREE_PTR, valor);
}

void PaginaSlotted::set_live_count(std::uint16_t valor) {
    escribir_campo<std::uint16_t>(buffer_, DESPL_LIVE_COUNT, valor);
}

void PaginaSlotted::set_dead_bytes(std::uint16_t valor) {
    escribir_campo<std::uint16_t>(buffer_, DESPL_DEAD_BYTES, valor);
}

void PaginaSlotted::set_next_page(std::uint32_t siguiente) {
    escribir_campo<std::uint32_t>(buffer_, DESPL_NEXT_PAGE, siguiente);
}

std::uint16_t PaginaSlotted::espacio_contiguo() const {
    const std::size_t fin_directorio = HEADER_SIZE +
                                       static_cast<std::size_t>(slot_count()) * SLOT_SIZE;
    if (free_ptr() <= fin_directorio) {
        return 0;
    }
    return static_cast<std::uint16_t>(free_ptr() - fin_directorio);
}

std::uint16_t PaginaSlotted::espacio_recuperable() const {
    return static_cast<std::uint16_t>(espacio_contiguo() + dead_bytes());
}

bool PaginaSlotted::cabe(std::uint16_t largo) const {
    return espacio_contiguo() >= static_cast<std::uint32_t>(largo) + SLOT_SIZE;
}

std::uint16_t PaginaSlotted::insertar(const std::byte* registro, std::uint16_t largo) {
    if (!cabe(largo)) {
        return SLOT_INVALIDO;
    }
    const std::uint16_t nuevo_offset = static_cast<std::uint16_t>(free_ptr() - largo);
    if (largo > 0) {
        std::memcpy(buffer_ + nuevo_offset, registro, largo);
    }
    set_free_ptr(nuevo_offset);

    const std::uint16_t slot_id = slot_count();
    escribir_slot(slot_id, nuevo_offset, largo);
    set_slot_count(static_cast<std::uint16_t>(slot_id + 1));
    set_live_count(static_cast<std::uint16_t>(live_count() + 1));
    return slot_id;
}

bool PaginaSlotted::obtener(std::uint16_t slot_id, const std::byte*& datos,
                            std::uint16_t& largo) const {
    if (slot_id >= slot_count()) {
        return false;
    }
    const std::uint16_t largo_slot = largo_de(slot_id);
    if (largo_slot == SLOT_TUMBA) {
        return false;
    }
    datos = buffer_ + offset_de(slot_id);
    largo = largo_slot;
    return true;
}

bool PaginaSlotted::es_tumba(std::uint16_t slot_id) const {
    return slot_id < slot_count() && largo_de(slot_id) == SLOT_TUMBA;
}

bool PaginaSlotted::eliminar(std::uint16_t slot_id) {
    if (slot_id >= slot_count()) {
        return false;
    }
    const std::uint16_t largo = largo_de(slot_id);
    if (largo == SLOT_TUMBA) {
        return false;
    }
    set_dead_bytes(static_cast<std::uint16_t>(dead_bytes() + largo));
    escribir_slot(slot_id, offset_de(slot_id), SLOT_TUMBA);
    set_live_count(static_cast<std::uint16_t>(live_count() - 1));
    return true;
}

void PaginaSlotted::compactar() {
    if (dead_bytes() == 0) {
        return;
    }
    std::array<std::byte, PAGE_SIZE> temporal{};
    std::uint16_t destino = static_cast<std::uint16_t>(PAGE_SIZE);
    const std::uint16_t total_slots = slot_count();

    for (std::uint16_t slot_id = 0; slot_id < total_slots; ++slot_id) {
        const std::uint16_t largo = largo_de(slot_id);
        if (largo == SLOT_TUMBA) {
            continue;
        }
        const std::uint16_t origen = offset_de(slot_id);
        destino = static_cast<std::uint16_t>(destino - largo);
        if (largo > 0) {
            std::memcpy(temporal.data() + destino, buffer_ + origen, largo);
        }
        escribir_slot(slot_id, destino, largo);
    }

    std::memcpy(buffer_ + destino, temporal.data() + destino, PAGE_SIZE - destino);
    set_free_ptr(destino);
    set_dead_bytes(0);
}

bool PaginaSlotted::verificar_invariantes() const {
    const std::uint16_t total_slots = slot_count();
    const std::size_t fin_directorio = HEADER_SIZE +
                                       static_cast<std::size_t>(total_slots) * SLOT_SIZE;

    if (fin_directorio > free_ptr()) {
        return false;
    }
    if (free_ptr() > PAGE_SIZE) {
        return false;
    }

    std::size_t vivos = 0;
    std::size_t bytes_vivos = 0;
    for (std::uint16_t slot_id = 0; slot_id < total_slots; ++slot_id) {
        const std::uint16_t largo = largo_de(slot_id);
        if (largo == SLOT_TUMBA) {
            continue;
        }
        const std::uint16_t offset = offset_de(slot_id);
        if (offset < free_ptr()) {
            return false;
        }
        if (static_cast<std::size_t>(offset) + largo > PAGE_SIZE) {
            return false;
        }
        ++vivos;
        bytes_vivos += largo;

        for (std::uint16_t otro = 0; otro < slot_id; ++otro) {
            const std::uint16_t largo_otro = largo_de(otro);
            if (largo_otro == SLOT_TUMBA) {
                continue;
            }
            const std::uint16_t offset_otro = offset_de(otro);
            const bool se_solapan = offset < offset_otro + largo_otro &&
                                    offset_otro < offset + largo;
            if (se_solapan) {
                return false;
            }
        }
    }

    if (vivos != live_count()) {
        return false;
    }
    if (PAGE_SIZE - free_ptr() != bytes_vivos + dead_bytes()) {
        return false;
    }
    return true;
}

}  // namespace motor
