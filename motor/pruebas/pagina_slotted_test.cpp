#include "../archivos/pagina_slotted.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

std::array<std::byte, motor::PAGE_SIZE> respaldo;

std::vector<std::byte> relleno(std::uint16_t largo, unsigned char semilla) {
    std::vector<std::byte> datos(largo);
    for (std::uint16_t indice = 0; indice < largo; ++indice) {
        datos[indice] = static_cast<std::byte>((semilla + indice) & 0xFF);
    }
    return datos;
}

bool contenido_igual(const std::byte* datos, std::uint16_t largo,
                     const std::vector<std::byte>& esperado) {
    return largo == esperado.size() &&
           (largo == 0 || std::memcmp(datos, esperado.data(), largo) == 0);
}

void prueba_pagina_vacia() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    assert(pagina.slot_count() == 0);
    assert(pagina.free_ptr() == motor::PAGE_SIZE);
    assert(pagina.live_count() == 0);
    assert(pagina.dead_bytes() == 0);
    assert(pagina.next_page() == motor::PAGINA_INVALIDA);
    assert(pagina.espacio_contiguo() == motor::ESPACIO_UTIL_PAGINA);
    assert(pagina.verificar_invariantes());
    std::cout << "pagina_vacia: free_ptr=" << pagina.free_ptr()
              << " espacio_contiguo=" << pagina.espacio_contiguo() << "\n";
}

void prueba_insertar_leer() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> uno = relleno(50, 1);
    const std::vector<std::byte> dos = relleno(120, 2);
    const std::vector<std::byte> tres = relleno(7, 3);

    const std::uint16_t slot_uno = pagina.insertar(uno.data(), 50);
    const std::uint16_t slot_dos = pagina.insertar(dos.data(), 120);
    const std::uint16_t slot_tres = pagina.insertar(tres.data(), 7);
    assert(slot_uno == 0 && slot_dos == 1 && slot_tres == 2);
    assert(pagina.slot_count() == 3 && pagina.live_count() == 3);

    const std::byte* datos_uno = nullptr;
    const std::byte* datos_dos = nullptr;
    const std::byte* datos_tres = nullptr;
    std::uint16_t largo = 0;

    assert(pagina.obtener(slot_uno, datos_uno, largo) && contenido_igual(datos_uno, largo, uno));
    assert(pagina.obtener(slot_dos, datos_dos, largo) && contenido_igual(datos_dos, largo, dos));
    assert(pagina.obtener(slot_tres, datos_tres, largo) &&
           contenido_igual(datos_tres, largo, tres));

    assert(datos_uno > datos_dos && datos_dos > datos_tres);
    assert(pagina.espacio_contiguo() ==
           motor::ESPACIO_UTIL_PAGINA - (50 + 120 + 7) - 3 * motor::SLOT_SIZE);
    assert(pagina.verificar_invariantes());
    std::cout << "insertar_leer: 3 registros, offsets decrecientes, espacio_contiguo="
              << pagina.espacio_contiguo() << "\n";
}

void prueba_longitud_cero() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::uint16_t slot = pagina.insertar(nullptr, 0);
    assert(slot == 0);

    const std::byte* datos = nullptr;
    std::uint16_t largo = 7;
    assert(pagina.obtener(slot, datos, largo));
    assert(largo == 0);
    assert(!pagina.es_tumba(slot));
    assert(pagina.live_count() == 1);
    assert(pagina.verificar_invariantes());
    std::cout << "longitud_cero: registro de 0 bytes legible y distinguible de una tumba\n";
}

void prueba_eliminar() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> uno = relleno(50, 1);
    const std::vector<std::byte> dos = relleno(120, 2);
    const std::vector<std::byte> tres = relleno(30, 3);
    pagina.insertar(uno.data(), 50);
    pagina.insertar(dos.data(), 120);
    pagina.insertar(tres.data(), 30);

    const std::uint16_t free_ptr_previo = pagina.free_ptr();
    const std::uint16_t espacio_previo = pagina.espacio_contiguo();

    assert(pagina.eliminar(1));
    assert(pagina.es_tumba(1));
    assert(pagina.live_count() == 2);
    assert(pagina.slot_count() == 3);
    assert(pagina.dead_bytes() == 120);
    assert(pagina.free_ptr() == free_ptr_previo);
    assert(pagina.espacio_contiguo() == espacio_previo);
    assert(pagina.espacio_recuperable() == espacio_previo + 120);

    const std::byte* datos = nullptr;
    std::uint16_t largo = 0;
    assert(!pagina.obtener(1, datos, largo));
    assert(pagina.obtener(0, datos, largo) && contenido_igual(datos, largo, uno));
    assert(pagina.obtener(2, datos, largo) && contenido_igual(datos, largo, tres));
    assert(pagina.verificar_invariantes());
    std::cout << "eliminar: slot_count=" << pagina.slot_count()
              << " live_count=" << pagina.live_count()
              << " dead_bytes=" << pagina.dead_bytes() << "\n";
}

void prueba_doble_eliminar() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> uno = relleno(80, 1);
    pagina.insertar(uno.data(), 80);

    assert(pagina.eliminar(0));
    assert(pagina.dead_bytes() == 80);
    assert(!pagina.eliminar(0));
    assert(pagina.dead_bytes() == 80);
    assert(pagina.live_count() == 0);
    assert(pagina.verificar_invariantes());
    std::cout << "doble_eliminar: idempotente, dead_bytes no se duplica\n";
}

void prueba_slot_fuera_de_rango() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> uno = relleno(40, 1);
    pagina.insertar(uno.data(), 40);
    std::memcpy(respaldo.data(), buffer.data(), motor::PAGE_SIZE);

    const std::byte* datos = nullptr;
    std::uint16_t largo = 0;
    assert(!pagina.obtener(999, datos, largo));
    assert(!pagina.eliminar(999));
    assert(!pagina.es_tumba(999));
    assert(std::memcmp(respaldo.data(), buffer.data(), motor::PAGE_SIZE) == 0);
    assert(pagina.verificar_invariantes());
    std::cout << "slot_fuera_de_rango: pagina intacta byte a byte\n";
}

void prueba_pagina_exactamente_llena() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> bloque = relleno(100, 9);
    for (int indice = 0; indice < 39; ++indice) {
        assert(pagina.insertar(bloque.data(), 100) != motor::SLOT_INVALIDO);
    }
    assert(pagina.espacio_contiguo() == 28);

    const std::vector<std::byte> ultimo = relleno(24, 5);
    assert(pagina.insertar(ultimo.data(), 24) == 39);
    assert(pagina.espacio_contiguo() == 0);

    const std::vector<std::byte> sobra = relleno(1, 7);
    assert(pagina.insertar(sobra.data(), 1) == motor::SLOT_INVALIDO);
    assert(pagina.slot_count() == 40);
    assert(pagina.verificar_invariantes());
    std::cout << "pagina_exactamente_llena: 40 slots, espacio_contiguo=0\n";
}

void prueba_falta_espacio_para_slot() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> bloque = relleno(100, 9);
    for (int indice = 0; indice < 39; ++indice) {
        pagina.insertar(bloque.data(), 100);
    }
    assert(pagina.espacio_contiguo() == 28);

    std::memcpy(respaldo.data(), buffer.data(), motor::PAGE_SIZE);
    const std::vector<std::byte> justo = relleno(25, 3);
    assert(!pagina.cabe(25));
    assert(pagina.insertar(justo.data(), 25) == motor::SLOT_INVALIDO);
    assert(std::memcmp(respaldo.data(), buffer.data(), motor::PAGE_SIZE) == 0);
    assert(pagina.verificar_invariantes());
    std::cout << "falta_espacio_para_slot: 25 bytes caben pero 25+4 no; pagina sin mutar\n";
}

void prueba_registro_gigante() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> gigante = relleno(motor::CAPACIDAD_MAXIMA_REGISTRO + 1, 1);
    assert(!pagina.cabe(motor::CAPACIDAD_MAXIMA_REGISTRO + 1));
    assert(pagina.insertar(gigante.data(), motor::CAPACIDAD_MAXIMA_REGISTRO + 1) ==
           motor::SLOT_INVALIDO);

    const std::vector<std::byte> maximo = relleno(motor::CAPACIDAD_MAXIMA_REGISTRO, 2);
    assert(pagina.insertar(maximo.data(), motor::CAPACIDAD_MAXIMA_REGISTRO) == 0);
    assert(pagina.espacio_contiguo() == 0);
    assert(pagina.verificar_invariantes());
    std::cout << "registro_gigante: " << motor::CAPACIDAD_MAXIMA_REGISTRO << " cabe, "
              << motor::CAPACIDAD_MAXIMA_REGISTRO + 1 << " no\n";
}

void prueba_compactar() {
    std::array<std::byte, motor::PAGE_SIZE> buffer{};
    motor::PaginaSlotted pagina(buffer.data());
    pagina.inicializar();

    const std::vector<std::byte> uno = relleno(100, 11);
    const std::vector<std::byte> dos = relleno(100, 22);
    const std::vector<std::byte> tres = relleno(100, 33);
    pagina.insertar(uno.data(), 100);
    pagina.insertar(dos.data(), 100);
    pagina.insertar(tres.data(), 100);

    assert(pagina.eliminar(1));
    const std::uint16_t espacio_previo = pagina.espacio_contiguo();
    assert(pagina.dead_bytes() == 100);

    pagina.compactar();

    assert(pagina.dead_bytes() == 0);
    assert(pagina.espacio_contiguo() == espacio_previo + 100);
    assert(pagina.slot_count() == 3);
    assert(pagina.live_count() == 2);
    assert(pagina.es_tumba(1));

    const std::byte* datos = nullptr;
    std::uint16_t largo = 0;
    assert(pagina.obtener(0, datos, largo) && contenido_igual(datos, largo, uno));
    assert(pagina.obtener(2, datos, largo) && contenido_igual(datos, largo, tres));
    assert(!pagina.obtener(1, datos, largo));
    assert(pagina.verificar_invariantes());
    std::cout << "compactar: slot_id preservados, espacio_contiguo=" << pagina.espacio_contiguo()
              << " dead_bytes=0\n";
}

}  // namespace

int main() {
    prueba_pagina_vacia();
    prueba_insertar_leer();
    prueba_longitud_cero();
    prueba_eliminar();
    prueba_doble_eliminar();
    prueba_slot_fuera_de_rango();
    prueba_pagina_exactamente_llena();
    prueba_falta_espacio_para_slot();
    prueba_registro_gigante();
    prueba_compactar();
    std::cout << "Prueba completada correctamente.\n";
}
