#include "../indices/hash_extensible_disco.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

const std::string RUTA = ".build/hash_disco_test.hash";

void limpiar() { std::filesystem::remove(RUTA); }

bool tiene(HashExtensibleDisco& h, int clave, long long pos) {
    const std::vector<long long> v = h.buscar(clave);
    return std::find(v.begin(), v.end(), pos) != v.end();
}

void prueba_basica() {
    limpiar();
    HashExtensibleDisco h(RUTA, true);
    assert(h.num_entradas() == 0 && h.profundidad_global() == 0);

    for (int i = 1; i <= 100; ++i) h.insertar(i, i * 10);
    assert(h.num_entradas() == 100);
    for (int i = 1; i <= 100; ++i) assert(tiene(h, i, i * 10));
    assert(h.buscar(101).empty());

    h.insertar(7, 70);
    assert(h.num_entradas() == 100 && h.buscar(7).size() == 1);

    h.insertar(7, 71);
    assert(h.num_entradas() == 101 && h.buscar(7).size() == 2);
    std::cout << "basica: ida y vuelta, par repetido ignorado, clave repetida admitida\n";
}

void prueba_division() {
    limpiar();
    HashExtensibleDisco h(RUTA, true);
    for (int i = 1; i <= 5000; ++i) h.insertar(i, i);
    assert(h.num_entradas() == 5000);
    assert(h.profundidad_global() >= 5 && "el directorio se duplico varias veces");
    assert(h.paginas_bucket() >= 20);
    for (int i = 1; i <= 5000; ++i) assert(h.buscar(i).size() == 1);
    std::cout << "division: 5000 claves, profundidad global " << h.profundidad_global() << ", "
              << h.paginas_bucket() << " paginas de bucket\n";
}

void prueba_desborde() {
    limpiar();
    HashExtensibleDisco h(RUTA, true);
    for (int i = 0; i < 2000; ++i) h.insertar(42, i);
    assert(h.num_entradas() == 2000);
    assert(h.buscar(42).size() == 2000);
    assert(h.profundidad_global() == 0 && "claves identicas no deben duplicar el directorio");

    for (int i = 1; i <= 500; ++i) h.insertar(i, i + 100000);
    assert(h.buscar(42).size() == 2001);
    assert(tiene(h, 42, 100042) && tiene(h, 42, 7));
    std::cout << "desborde: 2000 entradas con la misma clave sin duplicar el directorio\n";
}

void prueba_eliminar() {
    limpiar();
    HashExtensibleDisco h(RUTA, true);
    for (int i = 1; i <= 600; ++i) h.insertar(i % 50, i);
    assert(h.num_entradas() == 600);

    assert(h.eliminar_entrada(3, 3) && "el par existia");
    assert(!h.eliminar_entrada(3, 3) && "idempotente");
    assert(h.num_entradas() == 599);

    const int quitadas = h.eliminar(7);
    assert(quitadas == 12 && h.buscar(7).empty());
    assert(h.num_entradas() == 599 - 12);
    assert(h.eliminar(9999) == 0);
    std::cout << "eliminar: por par exacto, por clave completa e idempotencia\n";
}

void prueba_persistencia() {
    limpiar();
    int profundidad = 0;
    {
        HashExtensibleDisco h(RUTA, true);
        for (int i = 1; i <= 3000; ++i) h.insertar(i, i * 3);
        h.eliminar(5);
        h.sincronizar();
        profundidad = h.profundidad_global();
    }
    {
        HashExtensibleDisco h(RUTA, false);
        assert(h.profundidad_global() == profundidad);
        assert(h.num_entradas() == 2999);
        for (int i = 1; i <= 3000; ++i) {
            if (i == 5) assert(h.buscar(i).empty());
            else assert(tiene(h, i, i * 3));
        }
        h.insertar(9001, 90010);
        assert(tiene(h, 9001, 90010));
    }
    std::cout << "persistencia: cierra, reabre y sigue respondiendo\n";
}

void prueba_cabecera_invalida() {
    limpiar();
    {
        std::ofstream f(RUTA, std::ios::binary);
        std::vector<char> basura(4096 * 2, 0);
        basura[0] = 'X';
        basura[1] = 'Y';
        f.write(basura.data(), static_cast<std::streamsize>(basura.size()));
    }
    bool lanzo = false;
    try {
        HashExtensibleDisco h(RUTA, false);
    } catch (const std::invalid_argument&) {
        lanzo = true;
    }
    assert(lanzo && "un archivo ajeno debe rechazarse");
    std::cout << "cabecera: se rechaza un archivo que no es del indice hash\n";
}

void prueba_masiva() {
    limpiar();
    std::mt19937 generador(42);
    std::vector<int> claves(20000);
    for (int i = 0; i < 20000; ++i) claves[i] = static_cast<int>(generador() % 100000);

    HashExtensibleDisco h(RUTA, true);
    std::set<std::pair<int, long long>> esperado;
    for (int i = 0; i < 20000; ++i) {
        h.insertar(claves[i], i);
        esperado.insert({claves[i], i});
    }
    assert(h.num_entradas() == static_cast<long>(esperado.size()));
    for (int i = 0; i < 20000; i += 97) assert(tiene(h, claves[i], i));

    const long bytes = h.tamano_en_disco();
    std::cout << "masiva: " << esperado.size() << " entradas, profundidad " << h.profundidad_global()
              << ", " << h.paginas_bucket() << " paginas de bucket, " << bytes / 1024 << " KB en disco\n";
}

}

int main() {
    std::filesystem::create_directories(".build");
    prueba_basica();
    prueba_division();
    prueba_desborde();
    prueba_eliminar();
    prueba_persistencia();
    prueba_cabecera_invalida();
    prueba_masiva();
    limpiar();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
