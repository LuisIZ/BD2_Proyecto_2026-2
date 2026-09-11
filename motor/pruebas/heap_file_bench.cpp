#include "../archivos/heap_file.h"
#include "cargador_csv.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using Reloj = std::chrono::steady_clock;

struct Opciones {
    std::size_t n = 10000;
    std::string csv = "datos/organizations-100000.csv";
    std::string salida = "datos/resultados/heap_bench.csv";
    std::string archivo = ".build/bench.heap";
    bool barajar = false;
    unsigned semilla = 42;
    int borrar_pct = 30;
    std::size_t muestras_busqueda = 100;
};

long long microsegundos(const Reloj::time_point& inicio, const Reloj::time_point& fin) {
    return std::chrono::duration_cast<std::chrono::microseconds>(fin - inicio).count();
}

bool leer_opciones(int argc, char** argv, Opciones& opciones) {
    for (int indice = 1; indice < argc; ++indice) {
        const std::string bandera = argv[indice];
        const bool tiene_valor = indice + 1 < argc;

        if (bandera == "--barajar") {
            opciones.barajar = true;
        } else if (bandera == "--n" && tiene_valor) {
            opciones.n = static_cast<std::size_t>(std::atoll(argv[++indice]));
        } else if (bandera == "--csv" && tiene_valor) {
            opciones.csv = argv[++indice];
        } else if (bandera == "--salida" && tiene_valor) {
            opciones.salida = argv[++indice];
        } else if (bandera == "--archivo" && tiene_valor) {
            opciones.archivo = argv[++indice];
        } else if (bandera == "--semilla" && tiene_valor) {
            opciones.semilla = static_cast<unsigned>(std::atoi(argv[++indice]));
        } else if (bandera == "--borrar-pct" && tiene_valor) {
            opciones.borrar_pct = std::atoi(argv[++indice]);
        } else if (bandera == "--muestras-busqueda" && tiene_valor) {
            opciones.muestras_busqueda = static_cast<std::size_t>(std::atoll(argv[++indice]));
        } else {
            std::cerr << "opcion no reconocida: " << bandera << "\n";
            return false;
        }
    }
    return true;
}

const char* kEncabezado =
    "implementacion,n,barajado,semilla,"
    "t_carga_us,t_insercion_us,t_busqueda_pk_prom_us,t_busqueda_pk_peor_us,t_scan_us,"
    "t_reorganizacion_us,"
    "paginas,registros_vivos,bytes_en_disco,tumbas,bytes_desperdiciados,"
    "paginas_leidas,paginas_escritas,paginas_leidas_por_busqueda";

bool existe_archivo(const std::string& ruta) {
    std::ifstream sonda(ruta, std::ios::binary);
    return sonda.good();
}

}  // namespace

int main(int argc, char** argv) {
    Opciones opciones;
    if (!leer_opciones(argc, argv, opciones)) {
        return 1;
    }

    const Reloj::time_point inicio_carga = Reloj::now();
    const motor::pruebas::OpcionesCarga carga{opciones.n, opciones.barajar, opciones.semilla};
    const std::vector<motor::Registro> registros =
        motor::pruebas::cargar_csv(opciones.csv, carga);
    const long long t_carga = microsegundos(inicio_carga, Reloj::now());

    if (registros.empty()) {
        std::cerr << "el CSV no aporto registros\n";
        return 1;
    }

    motor::HeapFile archivo(opciones.archivo, true);
    archivo.reiniciar_contadores();

    std::vector<motor::RecordId> rids;
    rids.reserve(registros.size());
    const Reloj::time_point inicio_insercion = Reloj::now();
    for (const motor::Registro& registro : registros) {
        const std::vector<std::byte> bytes = motor::codificar_registro(registro);
        rids.push_back(
            archivo.insertar_bytes(bytes.data(), static_cast<std::uint16_t>(bytes.size())));
    }
    const long long t_insercion = microsegundos(inicio_insercion, Reloj::now());

    const motor::EstadisticasHeap tras_insercion = archivo.stats_heap();
    const std::size_t paginas_leidas_insercion = archivo.paginas_leidas();
    const std::size_t paginas_escritas_insercion = archivo.paginas_escritas();

    const Reloj::time_point inicio_scan = Reloj::now();
    std::size_t vistos = 0;
    archivo.recorrer([&](const motor::RecordId&, const std::byte*, std::uint16_t) {
        ++vistos;
        return true;
    });
    const long long t_scan = microsegundos(inicio_scan, Reloj::now());

    std::mt19937 generador(opciones.semilla);
    std::uniform_int_distribution<std::size_t> sorteo(0, registros.size() - 1);

    archivo.reiniciar_contadores();
    const std::size_t muestras =
        std::min(opciones.muestras_busqueda, registros.size());
    const Reloj::time_point inicio_busqueda = Reloj::now();
    for (std::size_t intento = 0; intento < muestras; ++intento) {
        const int clave = registros[sorteo(generador)].clave;
        if (!archivo.buscar(clave).has_value()) {
            std::cerr << "clave existente no encontrada: " << clave << "\n";
            return 1;
        }
    }
    const long long t_busqueda_total = microsegundos(inicio_busqueda, Reloj::now());
    const long long t_busqueda_promedio =
        muestras == 0 ? 0 : t_busqueda_total / static_cast<long long>(muestras);
    const std::size_t paginas_por_busqueda =
        muestras == 0 ? 0 : archivo.paginas_leidas() / muestras;

    const Reloj::time_point inicio_peor = Reloj::now();
    if (archivo.buscar(-1).has_value()) {
        std::cerr << "una clave inexistente fue encontrada\n";
        return 1;
    }
    const long long t_busqueda_peor = microsegundos(inicio_peor, Reloj::now());

    const std::size_t a_borrar = registros.size() * static_cast<std::size_t>(opciones.borrar_pct) / 100;
    for (std::size_t indice = 0; indice < a_borrar; ++indice) {
        archivo.eliminar_rid(rids[indice]);
    }
    const motor::EstadisticasHeap tras_borrado = archivo.stats_heap();

    archivo.reorganizar();
    const long long t_reorganizacion = archivo.stats().ultima_reorganizacion_us;

    const bool primera_vez = !existe_archivo(opciones.salida);
    std::ofstream salida(opciones.salida, std::ios::app);
    if (!salida) {
        std::cerr << "no se pudo abrir la salida: " << opciones.salida
                  << " (existe el directorio?)\n";
        return 1;
    }
    if (primera_vez) {
        salida << kEncabezado << "\n";
    }
    salida << "heap," << registros.size() << ',' << (opciones.barajar ? 1 : 0) << ','
           << opciones.semilla << ',' << t_carga << ',' << t_insercion << ','
           << t_busqueda_promedio << ',' << t_busqueda_peor << ',' << t_scan << ','
           << t_reorganizacion << ',' << tras_insercion.num_paginas << ','
           << tras_insercion.registros_vivos << ',' << tras_insercion.tamano_archivo_bytes << ','
           << tras_borrado.tumbas << ',' << tras_borrado.bytes_desperdiciados << ','
           << paginas_leidas_insercion << ',' << paginas_escritas_insercion << ','
           << paginas_por_busqueda << "\n";

    std::cout << "n=" << registros.size() << (opciones.barajar ? " barajado" : " ordenado")
              << " | insercion=" << t_insercion / 1000 << " ms"
              << " | scan=" << t_scan / 1000 << " ms (" << vistos << " vivos)"
              << " | busqueda_pk=" << t_busqueda_promedio << " us/" << paginas_por_busqueda
              << " pag"
              << " | peor=" << t_busqueda_peor << " us"
              << " | reorg=" << t_reorganizacion << " us"
              << " | " << tras_insercion.num_paginas << " pag / "
              << tras_insercion.tamano_archivo_bytes << " B\n";
    return 0;
}
