#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace motor {

template <typename T>
inline void escribir_campo(std::byte* buffer, std::size_t desplazamiento, const T& valor) {
    static_assert(std::is_trivially_copyable_v<T>, "solo tipos trivialmente copiables");
    std::memcpy(buffer + desplazamiento, &valor, sizeof(T));
}

template <typename T>
inline T leer_campo(const std::byte* buffer, std::size_t desplazamiento) {
    static_assert(std::is_trivially_copyable_v<T>, "solo tipos trivialmente copiables");
    T valor;
    std::memcpy(&valor, buffer + desplazamiento, sizeof(T));
    return valor;
}

}  // namespace motor
