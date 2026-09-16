// Carga organizations-<n>.csv en un B+ agrupado y mide construcción (una a una
// y masiva), búsqueda puntual, rango y eliminación. Se corre desde la raíz del repo:
//
//   bplus_agrupado_csv_test [--n 1000|10000|100000] [--barajar]
//                           [--csv ruta] [--salida datos/resultados/bplus_agrupado.csv]

#include "../indices/bplus_agrupado.h"
#include "cargador_csv.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

// el registro agrupado: 180 bytes, la clave (Index) va primero
struct Organizacion {
    int clave;
    int fundada;
    int empleados;
    char org_id[16];
    char nombre[40];
    char pais[56];
    char industria[56];
};
static_assert(sizeof(Organizacion) == 180, "Organizacion debe medir 180 bytes");

struct Opciones {
    std::size_t n = 1000;
    bool barajar = false;
    std::string csv;
    std::string salida;
    std::string archivo = ".build/bplus_agrupado_csv.dat";
};

Opciones leer_opciones(int argc, char** argv) {
    Opciones o;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        auto valor = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("falta valor para " + arg);
            return argv[++i];
        };
        if (arg == "--n") o.n = std::stoul(valor());
        else if (arg == "--barajar") o.barajar = true;
        else if (arg == "--csv") o.csv = valor();
        else if (arg == "--salida") o.salida = valor();
        else if (arg == "--archivo") o.archivo = valor();
        else throw std::runtime_error("opcion desconocida: " + arg);
    }
    if (o.csv.empty()) o.csv = "datos/organizations-" + std::to_string(o.n) + ".csv";
    return o;
}

void copiar(char* destino, std::size_t capacidad, const std::string& texto) {
    std::memset(destino, 0, capacidad);
    std::memcpy(destino, texto.data(), std::min(capacidad, texto.size()));
}

int entero(const std::string& texto) {
    try {
        return std::stoi(texto);
    } catch (...) {
        return 0;
    }
}

// valor = resto de la línea tras Index: OrgId,Name,Website,Country,Description,Founded,Industry,Employees
Organizacion convertir(const motor::Registro& registro) {
    const std::vector<std::string> campos = motor::pruebas::dividir_campos_csv(registro.valor);
    if (campos.size() != 8) {
        throw std::runtime_error("fila " + std::to_string(registro.clave) + " con " +
                                 std::to_string(campos.size()) + " campos, esperaba 8");
    }
    Organizacion o{};
    o.clave = registro.clave;
    copiar(o.org_id, sizeof(o.org_id), campos[0]);
    copiar(o.nombre, sizeof(o.nombre), campos[1]);
    copiar(o.pais, sizeof(o.pais), campos[3]);
    o.fundada = entero(campos[5]);
    copiar(o.industria, sizeof(o.industria), campos[6]);
    o.empleados = entero(campos[7]);
    return o;
}

using Reloj = std::chrono::steady_clock;

long long micros(Reloj::time_point inicio) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Reloj::now() - inicio).count();
}

struct Medicion {
    std::string modo;
    long long t_construccion_us = 0;
    int altura = 0;
    long internas = 0;
    long hojas = 0;
    long bytes_en_disco = 0;
    long escrituras_construccion = 0;
    double t_busqueda_us = 0;
    double paginas_por_busqueda = 0;
    double t_rango_us = 0;
    double paginas_por_rango = 0;
    double t_eliminacion_us = 0;
};

void medir_consultas(motor::BPlusAgrupado& arbol, const std::vector<Organizacion>& filas,
                     Medicion& m, unsigned semilla) {
    std::mt19937 generador(semilla);
    std::uniform_int_distribution<std::size_t> indice(0, filas.size() - 1);

    // búsqueda puntual con caché fría
    const int consultas = 1000;
    arbol.enfriar_cache();
    const long leidas_antes = arbol.paginas_leidas();
    const auto inicio = Reloj::now();
    for (int i = 0; i < consultas; i++) {
        const Organizacion& esperada = filas[indice(generador)];
        Organizacion salida{};
        assert(arbol.buscar(esperada.clave, salida));
        assert(std::strcmp(salida.nombre, esperada.nombre) == 0);
        arbol.enfriar_cache();
    }
    m.t_busqueda_us = micros(inicio) / static_cast<double>(consultas);
    m.paginas_por_busqueda = (arbol.paginas_leidas() - leidas_antes) / static_cast<double>(consultas);

    Organizacion salida{};
    assert(!arbol.buscar(-1, salida));
    assert(!arbol.buscar(static_cast<int>(filas.size()) + 1, salida));

    // rangos de 100 claves con caché fría
    const int rangos = 100;
    const int ancho = 100;
    std::uniform_int_distribution<int> desde(1, std::max(1, static_cast<int>(filas.size()) - ancho));
    arbol.enfriar_cache();
    const long leidas_rango = arbol.paginas_leidas();
    const auto inicio_rango = Reloj::now();
    for (int i = 0; i < rangos; i++) {
        const int d = desde(generador);
        const std::vector<Organizacion> encontradas = arbol.buscar_rango<Organizacion>(d, d + ancho - 1);
        assert(static_cast<int>(encontradas.size()) == ancho);
        assert(encontradas.front().clave == d && encontradas.back().clave == d + ancho - 1);
        arbol.enfriar_cache();
    }
    m.t_rango_us = micros(inicio_rango) / static_cast<double>(rangos);
    m.paginas_por_rango = (arbol.paginas_leidas() - leidas_rango) / static_cast<double>(rangos);

    // borra el 10 % y comprueba que el resto sigue
    std::vector<int> borrar;
    for (std::size_t i = 0; i < filas.size(); i += 10) borrar.push_back(filas[i].clave);
    std::shuffle(borrar.begin(), borrar.end(), generador);
    const auto inicio_borrado = Reloj::now();
    for (int clave : borrar) assert(arbol.eliminar(clave));
    m.t_eliminacion_us = micros(inicio_borrado) / static_cast<double>(borrar.size());
    assert(arbol.num_registros() == static_cast<long>(filas.size() - borrar.size()));
    assert(arbol.verificar_invariantes());
    for (int clave : borrar) assert(!arbol.buscar(clave, salida));
    assert(arbol.buscar(filas[1].clave, salida));
}

void imprimir(const Medicion& m, std::size_t n) {
    std::cout << "[" << m.modo << "] n=" << n
              << " construccion=" << m.t_construccion_us / 1000.0 << " ms"
              << " escrituras=" << m.escrituras_construccion
              << " altura=" << m.altura
              << " internas=" << m.internas << " hojas=" << m.hojas
              << " disco=" << m.bytes_en_disco / 1024 << " KB\n"
              << "    busqueda=" << m.t_busqueda_us << " us (" << m.paginas_por_busqueda << " pag)"
              << " rango100=" << m.t_rango_us << " us (" << m.paginas_por_rango << " pag)"
              << " eliminacion=" << m.t_eliminacion_us << " us\n";
}

void guardar(const Medicion& m, const Opciones& o) {
    if (o.salida.empty()) return;
    const bool nuevo = !std::filesystem::exists(o.salida);
    std::filesystem::create_directories(std::filesystem::path(o.salida).parent_path());
    std::ofstream archivo(o.salida, std::ios::app);
    if (nuevo) {
        archivo << "implementacion,modo,n,barajado,t_construccion_us,escrituras_construccion,"
                   "altura,paginas_internas,paginas_hoja,bytes_en_disco,t_busqueda_us,"
                   "paginas_por_busqueda,t_rango100_us,paginas_por_rango,t_eliminacion_us\n";
    }
    archivo << "bplus_agrupado," << m.modo << ',' << o.n << ',' << (o.barajar ? 1 : 0) << ','
            << m.t_construccion_us << ',' << m.escrituras_construccion << ',' << m.altura << ','
            << m.internas << ',' << m.hojas << ',' << m.bytes_en_disco << ',' << m.t_busqueda_us
            << ',' << m.paginas_por_busqueda << ',' << m.t_rango_us << ',' << m.paginas_por_rango
            << ',' << m.t_eliminacion_us << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const Opciones opciones = leer_opciones(argc, argv);
    std::filesystem::create_directories(".build");

    motor::pruebas::OpcionesCarga carga;
    carga.limite = opciones.n;
    carga.barajar = opciones.barajar;
    const std::vector<motor::Registro> crudos = motor::pruebas::cargar_csv(opciones.csv, carga);
    assert(crudos.size() == opciones.n);

    std::vector<Organizacion> filas;
    filas.reserve(crudos.size());
    for (const motor::Registro& registro : crudos) filas.push_back(convertir(registro));
    std::cout << "cargadas " << filas.size() << " organizaciones de " << opciones.csv
              << (opciones.barajar ? " (barajadas)" : "") << "\n";

    // inserción una a una
    {
        Medicion m;
        m.modo = "insercion";
        motor::BPlusAgrupado arbol(opciones.archivo, sizeof(Organizacion), true);
        assert(arbol.max_regs_hoja() == 22 && arbol.max_hijos() == 511);

        const auto inicio = Reloj::now();
        for (const Organizacion& fila : filas) assert(arbol.insertar(fila));
        arbol.sincronizar();
        m.t_construccion_us = micros(inicio);
        m.escrituras_construccion = arbol.paginas_escritas();

        assert(arbol.num_registros() == static_cast<long>(filas.size()));
        assert(arbol.verificar_invariantes());
        assert(!arbol.insertar(filas.front()) && "clave repetida");
        m.altura = arbol.altura();
        arbol.contar_paginas(m.internas, m.hojas);
        m.bytes_en_disco = arbol.tamano_en_disco();

        medir_consultas(arbol, filas, m, 42);
        imprimir(m, opciones.n);
        guardar(m, opciones);
    }

    // carga masiva
    {
        Medicion m;
        m.modo = "masiva";
        motor::BPlusAgrupado arbol(opciones.archivo, sizeof(Organizacion), true);

        std::vector<Organizacion> copia = filas;
        const auto inicio = Reloj::now();
        arbol.cargar_masivo(copia);
        arbol.sincronizar();
        m.t_construccion_us = micros(inicio);
        m.escrituras_construccion = arbol.paginas_escritas();

        assert(arbol.num_registros() == static_cast<long>(filas.size()));
        assert(arbol.verificar_invariantes());
        m.altura = arbol.altura();
        arbol.contar_paginas(m.internas, m.hojas);
        m.bytes_en_disco = arbol.tamano_en_disco();

        medir_consultas(arbol, filas, m, 43);
        imprimir(m, opciones.n);
        guardar(m, opciones);
    }

    // el archivo sobrevive al cierre
    {
        motor::BPlusAgrupado arbol(opciones.archivo, sizeof(Organizacion));
        assert(arbol.num_registros() == static_cast<long>(filas.size() - (filas.size() + 9) / 10));
        assert(arbol.verificar_invariantes());
    }

    std::cout << "CSV B+ agrupado: OK\n";
    return 0;
}
