#include "sequential_file.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace motor {

SequentialFile::SequentialFile(std::size_t capacidad_pagina,
                               double umbral_reorganizacion)
    : capacidad_pagina_(capacidad_pagina),
      umbral_reorganizacion_(umbral_reorganizacion) {
    if (capacidad_pagina_ == 0) {
        throw std::invalid_argument("La capacidad de pagina debe ser positiva");
    }
    set_umbral_reorganizacion(umbral_reorganizacion);
}

bool SequentialFile::insertar(const Registro& registro) {
    if (contiene(registro.clave)) {
        return false;
    }

    if (paginas_.empty()) {
        paginas_.push_back({});
    }

    const std::size_t indice = pagina_para(registro.clave);
    Pagina& pagina = paginas_[indice];
    if (pagina.slots.size() < capacidad_pagina_) {
        const auto posicion = std::lower_bound(
            pagina.slots.begin(), pagina.slots.end(), registro.clave,
            [](const Slot& slot, int clave) { return slot.registro.clave < clave; });
        pagina.slots.insert(posicion, Slot{registro, false});
    } else {
        insertar_auxiliar(registro);
    }

    reorganizar_si_corresponde();
    return true;
}

bool SequentialFile::eliminar(int clave) {
    for (Pagina& pagina : paginas_) {
        if (marcar_tumba(pagina.slots, clave)) {
            reorganizar_si_corresponde();
            return true;
        }
    }
    if (marcar_tumba(auxiliar_, clave)) {
        reorganizar_si_corresponde();
        return true;
    }
    return false;
}

std::vector<Registro> SequentialFile::scan() const {
    std::vector<Registro> principales;
    for (const Pagina& pagina : paginas_) {
        for (const Slot& slot : pagina.slots) {
            if (!slot.tumba) {
                principales.push_back(slot.registro);
            }
        }
    }

    std::vector<Registro> overflow;
    for (const Slot& slot : auxiliar_) {
        if (!slot.tumba) {
            overflow.push_back(slot.registro);
        }
    }

    std::vector<Registro> resultado;
    resultado.reserve(principales.size() + overflow.size());
    std::merge(principales.begin(), principales.end(), overflow.begin(), overflow.end(),
               std::back_inserter(resultado),
               [](const Registro& izquierda, const Registro& derecha) {
                   return izquierda.clave < derecha.clave;
               });
    return resultado;
}

EstadisticasArchivo SequentialFile::stats() const {
    EstadisticasArchivo resultado;
    resultado.paginas = paginas_.size();
    resultado.registros_auxiliares = auxiliar_.size();
    resultado.umbral_reorganizacion = umbral_reorganizacion_;
    resultado.ultima_reorganizacion_us = ultima_reorganizacion_us_;

    for (const Pagina& pagina : paginas_) {
        for (const Slot& slot : pagina.slots) {
            resultado.tumbas += slot.tumba ? 1 : 0;
            resultado.registros += slot.tumba ? 0 : 1;
        }
    }
    for (const Slot& slot : auxiliar_) {
        resultado.tumbas += slot.tumba ? 1 : 0;
        resultado.registros += slot.tumba ? 0 : 1;
    }
    resultado.porcentaje_desperdicio = porcentaje_desperdicio();
    return resultado;
}

void SequentialFile::reorganizar() {
    const auto inicio = std::chrono::steady_clock::now();
    const std::vector<Registro> registros = scan();

    paginas_.clear();
    auxiliar_.clear();
    for (const Registro& registro : registros) {
        if (paginas_.empty() || paginas_.back().slots.size() == capacidad_pagina_) {
            paginas_.push_back({});
        }
        paginas_.back().slots.push_back(Slot{registro, false});
    }

    const auto fin = std::chrono::steady_clock::now();
    ultima_reorganizacion_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                                     fin - inicio)
                                     .count();
}

void SequentialFile::set_umbral_reorganizacion(double umbral) {
    if (!std::isfinite(umbral) || umbral < 0.0 || umbral > 1.0) {
        throw std::invalid_argument("El umbral debe estar entre 0 y 1");
    }
    umbral_reorganizacion_ = umbral;
}

std::size_t SequentialFile::capacidad_pagina() const noexcept {
    return capacidad_pagina_;
}

std::size_t SequentialFile::pagina_para(int clave) const {
    if (paginas_.empty() || paginas_.front().slots.empty()) {
        return 0;
    }

    std::size_t izquierda = 0;
    std::size_t derecha = paginas_.size();
    while (izquierda < derecha) {
        const std::size_t medio = izquierda + (derecha - izquierda) / 2;
        if (paginas_[medio].slots.back().registro.clave < clave) {
            izquierda = medio + 1;
        } else {
            derecha = medio;
        }
    }
    return izquierda == paginas_.size() ? paginas_.size() - 1 : izquierda;
}

bool SequentialFile::contiene(int clave) const {
    for (const Pagina& pagina : paginas_) {
        const auto posicion = std::lower_bound(
            pagina.slots.begin(), pagina.slots.end(), clave,
            [](const Slot& slot, int valor) { return slot.registro.clave < valor; });
        if (posicion != pagina.slots.end() && posicion->registro.clave == clave &&
            !posicion->tumba) {
            return true;
        }
    }
    const auto posicion = std::lower_bound(
        auxiliar_.begin(), auxiliar_.end(), clave,
        [](const Slot& slot, int valor) { return slot.registro.clave < valor; });
    return posicion != auxiliar_.end() && posicion->registro.clave == clave &&
           !posicion->tumba;
}

bool SequentialFile::marcar_tumba(std::vector<Slot>& slots, int clave) {
    const auto posicion = std::lower_bound(
        slots.begin(), slots.end(), clave,
        [](const Slot& slot, int valor) { return slot.registro.clave < valor; });
    if (posicion == slots.end() || posicion->registro.clave != clave || posicion->tumba) {
        return false;
    }
    posicion->tumba = true;
    return true;
}

void SequentialFile::insertar_auxiliar(const Registro& registro) {
    const auto posicion = std::lower_bound(
        auxiliar_.begin(), auxiliar_.end(), registro.clave,
        [](const Slot& slot, int clave) { return slot.registro.clave < clave; });
    auxiliar_.insert(posicion, Slot{registro, false});
}

void SequentialFile::reorganizar_si_corresponde() {
    if (porcentaje_desperdicio() > umbral_reorganizacion_) {
        reorganizar();
    }
}

double SequentialFile::porcentaje_desperdicio() const {
    std::size_t slots = auxiliar_.size();
    for (const Pagina& pagina : paginas_) {
        slots += pagina.slots.size();
    }
    if (slots == 0) {
        return 0.0;
    }

    std::size_t tumbas = 0;
    for (const Pagina& pagina : paginas_) {
        for (const Slot& slot : pagina.slots) {
            tumbas += slot.tumba ? 1 : 0;
        }
    }
    for (const Slot& slot : auxiliar_) {
        tumbas += slot.tumba ? 1 : 0;
    }
    return static_cast<double>(tumbas) / static_cast<double>(slots);
}

}  // namespace motor