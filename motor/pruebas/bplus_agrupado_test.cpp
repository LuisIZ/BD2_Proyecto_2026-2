#include "../indices/bplus_agrupado.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// registro chico para ver el árbol crecer con pocos datos
struct Fila {
    int clave;
    int valor;
    char texto[24];
};
static_assert(sizeof(Fila) == 32, "Fila debe medir 32 bytes");

// registro con un double para comprobar que el árbol no depende de la alineación
struct FilaDouble {
    int clave;
    double peso;
};

Fila fila(int clave) {
    Fila f{};
    f.clave = clave;
    f.valor = clave * 10;
    std::snprintf(f.texto, sizeof(f.texto), "fila-%d", clave);
    return f;
}

std::vector<int> barajadas(int n, unsigned semilla) {
    std::vector<int> claves(n);
    std::iota(claves.begin(), claves.end(), 1);
    std::mt19937 generador(semilla);
    std::shuffle(claves.begin(), claves.end(), generador);
    return claves;
}

const std::string RUTA = ".build/bplus_agrupado_test.dat";
const std::string RUTA_CHICO = ".build/bplus_agrupado_test_chico.dat";
constexpr std::uint16_t DEMO_REGS = 3;
constexpr std::uint16_t DEMO_HIJOS = 4;

void prueba_basica() {
    motor::BPlusAgrupado arbol(RUTA, sizeof(Fila), true, DEMO_REGS, DEMO_HIJOS);
    assert(arbol.tam_registro() == sizeof(Fila));
    assert(arbol.max_regs_hoja() == DEMO_REGS);
    assert(arbol.max_hijos() == DEMO_HIJOS);
    assert(arbol.num_registros() == 0);
    assert(arbol.altura() == 1);

    assert(arbol.insertar(fila(5)));
    assert(arbol.insertar(fila(2)));
    assert(arbol.insertar(fila(9)));
    assert(!arbol.insertar(fila(5)) && "clave repetida debe rechazarse");
    assert(arbol.num_registros() == 3);

    Fila salida{};
    assert(arbol.buscar(2, salida));
    assert(salida.valor == 20 && std::strcmp(salida.texto, "fila-2") == 0);
    assert(!arbol.buscar(7, salida));

    // la cuarta fila parte la única hoja y el árbol crece
    assert(arbol.insertar(fila(7)));
    assert(arbol.altura() == 2);
    assert(arbol.claves_en_orden() == (std::vector<int>{2, 5, 7, 9}));
    assert(arbol.verificar_invariantes());
    std::cout << "basica: insertar/buscar/duplicado y primer split\n";
}

void prueba_inserciones_barajadas() {
    motor::BPlusAgrupado arbol(RUTA, sizeof(Fila), true, DEMO_REGS, DEMO_HIJOS);
    const int n = 300;
    for (int clave : barajadas(n, 7)) assert(arbol.insertar(fila(clave)));

    assert(arbol.num_registros() == n);
    assert(arbol.altura() >= 4);
    assert(arbol.verificar_invariantes());

    const std::vector<int> orden = arbol.claves_en_orden();
    assert(static_cast<int>(orden.size()) == n);
    for (int i = 0; i < n; i++) assert(orden[i] == i + 1);

    Fila salida{};
    for (int clave = 1; clave <= n; clave++) {
        assert(arbol.buscar(clave, salida));
        assert(salida.clave == clave && salida.valor == clave * 10);
    }
    assert(!arbol.buscar(0, salida));
    assert(!arbol.buscar(n + 1, salida));

    long internas = 0, hojas = 0;
    arbol.contar_paginas(internas, hojas);
    assert(hojas >= n / DEMO_REGS);
    std::cout << "barajadas: " << n << " filas, altura " << arbol.altura()
              << ", " << internas << " internas + " << hojas << " hojas\n";
}

void prueba_rango() {
    motor::BPlusAgrupado arbol(RUTA, sizeof(Fila), true, DEMO_REGS, DEMO_HIJOS);
    for (int clave = 1; clave <= 100; clave++) assert(arbol.insertar(fila(clave * 2)));

    const std::vector<Fila> medio = arbol.buscar_rango<Fila>(50, 75);
    assert(medio.size() == 13);
    assert(medio.front().clave == 50 && medio.back().clave == 74);
    for (std::size_t i = 1; i < medio.size(); i++) assert(medio[i].clave > medio[i - 1].clave);

    assert(arbol.buscar_rango<Fila>(51, 51).empty());
    assert(arbol.buscar_rango<Fila>(300, 400).empty());
    assert(arbol.buscar_rango<Fila>(80, 10).empty());
    assert(arbol.buscar_rango<Fila>(-100, 1000).size() == 100);

    // el visitante puede cortar el recorrido
    int visitados = 0;
    arbol.buscar_rango_bytes(0, 1000, [&](const void*) { return ++visitados < 5; });
    assert(visitados == 5);
    std::cout << "rango: [50,75] devuelve 13 claves pares, cortes y bordes\n";
}

void prueba_eliminacion() {
    motor::BPlusAgrupado arbol(RUTA, sizeof(Fila), true, DEMO_REGS, DEMO_HIJOS);
    const int n = 200;
    for (int clave : barajadas(n, 11)) assert(arbol.insertar(fila(clave)));
    const long paginas_llenas = arbol.tamano_en_disco();

    assert(!arbol.eliminar(0) && "clave inexistente");
    assert(!arbol.eliminar(n + 1));

    // borra la mitad en orden aleatorio verificando invariantes por el camino
    std::vector<int> borrar = barajadas(n, 13);
    borrar.resize(n / 2);
    std::vector<bool> vivo(n + 1, true);
    Fila salida{};
    for (std::size_t i = 0; i < borrar.size(); i++) {
        assert(arbol.eliminar(borrar[i]));
        vivo[borrar[i]] = false;
        assert(!arbol.buscar(borrar[i], salida));
        if (i % 10 == 0) assert(arbol.verificar_invariantes());
    }
    assert(arbol.num_registros() == n / 2);
    assert(arbol.verificar_invariantes());
    for (int clave = 1; clave <= n; clave++) assert(arbol.buscar(clave, salida) == vivo[clave]);

    // borra el resto: el árbol baja hasta una sola hoja
    for (int clave = 1; clave <= n; clave++) {
        if (vivo[clave]) assert(arbol.eliminar(clave));
    }
    assert(arbol.num_registros() == 0);
    assert(arbol.altura() == 1);
    assert(arbol.claves_en_orden().empty());
    assert(arbol.verificar_invariantes());

    // las páginas liberadas se reutilizan: reinsertar en el mismo orden
    // reconstruye la misma forma de árbol sin hacer crecer el archivo
    for (int clave : barajadas(n, 11)) assert(arbol.insertar(fila(clave)));
    assert(arbol.tamano_en_disco() <= paginas_llenas);
    assert(arbol.verificar_invariantes());
    std::cout << "eliminacion: prestamos, fusiones, colapso de raiz y reuso de paginas\n";
}

void prueba_persistencia() {
    {
        motor::BPlusAgrupado arbol(RUTA, sizeof(Fila), true, DEMO_REGS, DEMO_HIJOS);
        for (int clave : barajadas(120, 19)) assert(arbol.insertar(fila(clave)));
        arbol.sincronizar();
    }
    {
        motor::BPlusAgrupado arbol(RUTA, sizeof(Fila));
        assert(arbol.max_regs_hoja() == DEMO_REGS && "las capacidades se leen de la cabecera");
        assert(arbol.max_hijos() == DEMO_HIJOS);
        assert(arbol.num_registros() == 120);
        assert(arbol.verificar_invariantes());
        Fila salida{};
        assert(arbol.buscar(77, salida) && salida.valor == 770);
        assert(arbol.insertar(fila(121)));
        assert(arbol.eliminar(1));
    }
    {
        motor::BPlusAgrupado arbol(RUTA, sizeof(Fila));
        assert(arbol.num_registros() == 120);
        Fila salida{};
        assert(!arbol.buscar(1, salida));
        assert(arbol.buscar(121, salida));
    }

    bool rechazado = false;
    try {
        motor::BPlusAgrupado otro(RUTA, sizeof(FilaDouble));
    } catch (const std::invalid_argument&) {
        rechazado = true;
    }
    assert(rechazado && "abrir con otro tam_registro debe fallar");
    std::cout << "persistencia: cierra, reabre y rechaza tam_registro distinto\n";
}

void prueba_carga_masiva() {
    motor::BPlusAgrupado arbol(RUTA, sizeof(Fila), true, DEMO_REGS, DEMO_HIJOS);
    const int n = 500;
    std::vector<Fila> filas;
    for (int clave : barajadas(n, 23)) filas.push_back(fila(clave));
    arbol.cargar_masivo(filas);

    assert(arbol.num_registros() == n);
    assert(arbol.verificar_invariantes());
    Fila salida{};
    for (int clave = 1; clave <= n; clave++) assert(arbol.buscar(clave, salida));

    bool rechazado = false;
    try {
        arbol.cargar_masivo(filas);
    } catch (const std::logic_error&) {
        rechazado = true;
    }
    assert(rechazado && "cargar_masivo sobre un arbol con datos debe fallar");

    // el árbol sigue operativo después de la carga
    assert(arbol.insertar(fila(n + 1)));
    assert(!arbol.insertar(fila(250)));
    for (int clave = 1; clave <= n; clave += 3) assert(arbol.eliminar(clave));
    assert(arbol.verificar_invariantes());

    // con distintos factores de llenado y tamaños chicos, siempre válido
    for (int m : {1, 2, 3, 4, 7, 12, 13}) {
        for (double factor : {0.5, 0.9, 1.0}) {
            motor::BPlusAgrupado chico(RUTA_CHICO, sizeof(Fila), true, DEMO_REGS, DEMO_HIJOS);
            std::vector<Fila> pocas;
            for (int clave = 1; clave <= m; clave++) pocas.push_back(fila(clave));
            chico.cargar_masivo(pocas, factor);
            assert(chico.num_registros() == m);
            assert(chico.verificar_invariantes());
        }
    }
    std::cout << "carga masiva: " << n << " filas, altura " << arbol.altura()
              << ", exige arbol vacio\n";
}

void prueba_capacidad_real() {
    struct Organizacion {
        int clave;
        int fundada;
        int empleados;
        char org_id[16];
        char nombre[40];
        char pais[56];
        char industria[56];
    };
    static_assert(sizeof(Organizacion) == 180, "");

    motor::BPlusAgrupado arbol(RUTA, sizeof(Organizacion), true);
    assert(arbol.max_regs_hoja() == (4096 - 8) / 180);
    assert(arbol.max_hijos() == 511);

    const int n = 5000;
    for (int clave : barajadas(n, 29)) {
        Organizacion o{};
        o.clave = clave;
        o.fundada = 2000 + clave % 25;
        std::snprintf(o.nombre, sizeof(o.nombre), "Org %d", clave);
        assert(arbol.insertar(o));
    }
    assert(arbol.altura() == 2);
    assert(arbol.verificar_invariantes());
    Organizacion salida{};
    assert(arbol.buscar(4321, salida) && std::strcmp(salida.nombre, "Org 4321") == 0);
    assert(arbol.buscar_rango<Organizacion>(1000, 1999).size() == 1000);

    long internas = 0, hojas = 0;
    arbol.contar_paginas(internas, hojas);
    std::cout << "capacidad real: 180 B -> " << arbol.max_regs_hoja() << " regs/hoja, "
              << n << " filas en " << hojas << " hojas + " << internas
              << " internas, " << arbol.tamano_en_disco() / 1024 << " KB\n";
}

void prueba_alineacion_y_errores() {
    motor::BPlusAgrupado arbol(RUTA, sizeof(FilaDouble), true, 2, 3);
    for (int clave = 1; clave <= 50; clave++) {
        FilaDouble f{clave, clave * 1.5};
        assert(arbol.insertar(f));
    }
    FilaDouble salida{};
    assert(arbol.buscar(33, salida) && salida.peso == 49.5);
    assert(arbol.verificar_invariantes());

    bool rechazado = false;
    try {
        Fila otra = fila(1);
        arbol.insertar(otra);
    } catch (const std::invalid_argument&) {
        rechazado = true;
    }
    assert(rechazado && "sizeof distinto a tam_registro debe fallar");

    rechazado = false;
    try {
        motor::BPlusAgrupado enorme(RUTA, 3000, true);
    } catch (const std::invalid_argument&) {
        rechazado = true;
    }
    assert(rechazado && "registro que no cabe dos veces en la pagina debe fallar");
    std::cout << "alineacion y errores: double en el registro, tamanos invalidos\n";
}

}  // namespace

int main() {
    std::filesystem::create_directories(".build");
    prueba_basica();
    prueba_inserciones_barajadas();
    prueba_rango();
    prueba_eliminacion();
    prueba_persistencia();
    prueba_carga_masiva();
    prueba_capacidad_real();
    prueba_alineacion_y_errores();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
