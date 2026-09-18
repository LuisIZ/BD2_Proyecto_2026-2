#include "../indices/bplus_no_agrupado.h"
#include "../indices/hash_extensible_disco.h"
#include "cargador_csv.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using Reloj = std::chrono::steady_clock;

struct Opciones {
    std::size_t n = 10000;
    std::string csv = "datos/organizations-100000.csv";
    std::string columna = "Founded";
    std::string salida = "datos/resultados/indices_secundarios.csv";
    std::string prefijo = ".build/bench_indice";
    unsigned semilla = 42;
    std::size_t muestras = 200;
    int ancho_rango = 5;
    bool dedupe = false;
};

struct Medida {
    long long t_construccion_us = 0;
    long escrituras_construccion = 0;
    double t_igualdad_us = 0;
    double paginas_igualdad = 0;
    double filas_igualdad = 0;
    bool soporta_rango = false;
    double t_rango_us = 0;
    double paginas_rango = 0;
    double filas_rango = 0;
    double t_insercion_us = 0;
    double t_eliminacion_us = 0;
    long bytes_en_disco = 0;
    long paginas_estructura = 0;
    std::string detalle;
};

long long microsegundos(const Reloj::time_point& inicio, const Reloj::time_point& fin) {
    return std::chrono::duration_cast<std::chrono::microseconds>(fin - inicio).count();
}

bool leer_opciones(int argc, char** argv, Opciones& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string bandera = argv[i];
        const bool valor = i + 1 < argc;
        if (bandera == "--n" && valor) o.n = static_cast<std::size_t>(std::atoll(argv[++i]));
        else if (bandera == "--csv" && valor) o.csv = argv[++i];
        else if (bandera == "--columna" && valor) o.columna = argv[++i];
        else if (bandera == "--salida" && valor) o.salida = argv[++i];
        else if (bandera == "--prefijo" && valor) o.prefijo = argv[++i];
        else if (bandera == "--semilla" && valor) o.semilla = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (bandera == "--muestras" && valor) o.muestras = static_cast<std::size_t>(std::atoll(argv[++i]));
        else if (bandera == "--rango" && valor) o.ancho_rango = std::atoi(argv[++i]);
        else if (bandera == "--dedupe") o.dedupe = true;
        else {
            std::cerr << "opcion no reconocida: " << bandera << "\n";
            return false;
        }
    }
    return true;
}

bool cargar_columna(const Opciones& o, std::vector<std::pair<int, long long>>& pares) {
    std::ifstream archivo(o.csv);
    if (!archivo) {
        std::cerr << "no se pudo abrir " << o.csv << "\n";
        return false;
    }
    std::string linea;
    if (!std::getline(archivo, linea)) return false;
    const std::vector<std::string> cabecera = motor::pruebas::dividir_campos_csv(linea);
    int columna = -1;
    for (std::size_t i = 0; i < cabecera.size(); ++i) {
        if (cabecera[i] == o.columna) columna = static_cast<int>(i);
    }
    if (columna < 0) {
        std::cerr << "la columna " << o.columna << " no esta en " << o.csv << "\n";
        return false;
    }
    long long pos = 0;
    while (std::getline(archivo, linea) && (o.n == 0 || pares.size() < o.n)) {
        const std::vector<std::string> campos = motor::pruebas::dividir_campos_csv(linea);
        if (static_cast<int>(campos.size()) <= columna) continue;
        try {
            pares.push_back({std::stoi(campos[static_cast<std::size_t>(columna)]), pos++});
        } catch (const std::exception&) {
            std::cerr << "la columna " << o.columna << " no es entera\n";
            return false;
        }
    }
    return !pares.empty();
}

template <typename T>
struct tiene_rango : std::false_type {};
template <>
struct tiene_rango<BPlusNoAgrupado> : std::true_type {};

void insertar_en(BPlusNoAgrupado& indice, int clave, long long pos, bool) { indice.insertar(clave, pos); }
void insertar_en(HashExtensibleDisco& indice, int clave, long long pos, bool dedupe) {
    indice.insertar(clave, pos, dedupe);
}

std::string detalle_de(BPlusNoAgrupado& indice) { return "altura=" + std::to_string(indice.altura()); }
std::string detalle_de(HashExtensibleDisco& indice) {
    return "profundidad=" + std::to_string(indice.profundidad_global()) +
           " buckets=" + std::to_string(indice.paginas_bucket());
}

long paginas_de(BPlusNoAgrupado& indice) { return indice.tamano_en_disco() / 4096; }
long paginas_de(HashExtensibleDisco& indice) { return indice.paginas_bucket() + indice.paginas_directorio(); }

template <typename Indice>
Medida medir(const Opciones& o, const std::string& ruta, const std::vector<std::pair<int, long long>>& pares,
             const std::vector<int>& claves_distintas) {
    Medida m;
    Indice indice(ruta, true);

    const long escrituras_antes = indice.paginas_escritas();
    const Reloj::time_point inicio = Reloj::now();
    for (const auto& par : pares) insertar_en(indice, par.first, par.second, o.dedupe);
    m.t_construccion_us = microsegundos(inicio, Reloj::now());
    m.escrituras_construccion = indice.paginas_escritas() - escrituras_antes;
    indice.sincronizar();

    std::mt19937 generador(o.semilla);
    std::uniform_int_distribution<std::size_t> sorteo(0, claves_distintas.size() - 1);
    const std::size_t muestras = std::min(o.muestras, claves_distintas.size());

    std::vector<int> sorteadas;
    sorteadas.reserve(muestras);
    for (std::size_t k = 0; k < muestras; ++k) sorteadas.push_back(claves_distintas[sorteo(generador)]);

    long leidas_antes = indice.paginas_leidas();
    std::size_t encontradas = 0;
    for (const int clave : sorteadas) {
        indice.enfriar_cache();
        encontradas += indice.buscar(clave).size();
    }
    m.paginas_igualdad = static_cast<double>(indice.paginas_leidas() - leidas_antes) / static_cast<double>(muestras);
    if (encontradas == 0) std::cerr << "aviso: ninguna busqueda por igualdad encontro filas\n";
    m.filas_igualdad = static_cast<double>(encontradas) / static_cast<double>(muestras);

    const Reloj::time_point i_igualdad = Reloj::now();
    for (const int clave : sorteadas) indice.buscar(clave);
    m.t_igualdad_us = static_cast<double>(microsegundos(i_igualdad, Reloj::now())) / static_cast<double>(muestras);

    if constexpr (tiene_rango<Indice>::value) {
        m.soporta_rango = true;
        leidas_antes = indice.paginas_leidas();
        std::size_t filas = 0;
        for (const int desde : sorteadas) {
            indice.enfriar_cache();
            filas += indice.buscar_rango(desde, desde + o.ancho_rango).size();
        }
        m.paginas_rango = static_cast<double>(indice.paginas_leidas() - leidas_antes) / static_cast<double>(muestras);
        m.filas_rango = static_cast<double>(filas) / static_cast<double>(muestras);

        const Reloj::time_point i_rango = Reloj::now();
        for (const int desde : sorteadas) indice.buscar_rango(desde, desde + o.ancho_rango);
        m.t_rango_us = static_cast<double>(microsegundos(i_rango, Reloj::now())) / static_cast<double>(muestras);
    }

    std::vector<std::pair<int, long long>> nuevos;
    nuevos.reserve(muestras);
    for (std::size_t k = 0; k < muestras; ++k) {
        nuevos.push_back({claves_distintas[sorteo(generador)], static_cast<long long>(pares.size() + k)});
    }
    const Reloj::time_point i_insercion = Reloj::now();
    for (const auto& par : nuevos) insertar_en(indice, par.first, par.second, o.dedupe);
    m.t_insercion_us = static_cast<double>(microsegundos(i_insercion, Reloj::now())) / static_cast<double>(muestras);

    const Reloj::time_point i_eliminacion = Reloj::now();
    for (const auto& par : nuevos) indice.eliminar_entrada(par.first, par.second);
    m.t_eliminacion_us = static_cast<double>(microsegundos(i_eliminacion, Reloj::now())) / static_cast<double>(muestras);

    indice.sincronizar();
    m.bytes_en_disco = indice.tamano_en_disco();
    m.paginas_estructura = paginas_de(indice);
    m.detalle = detalle_de(indice);
    return m;
}

const char* kEncabezado =
    "implementacion,n,columna,claves_distintas,semilla,verifica_duplicado,"
    "t_construccion_us,paginas_escritas_construccion,"
    "t_igualdad_us,paginas_por_igualdad,filas_por_igualdad,"
    "soporta_rango,t_rango_us,paginas_por_rango,filas_por_rango,"
    "t_insercion_us,t_eliminacion_us,"
    "bytes_en_disco,paginas_estructura,detalle";

bool existe(const std::string& ruta) {
    std::ifstream sonda(ruta, std::ios::binary);
    return sonda.good();
}

void escribir(std::ostream& salida, const std::string& nombre, const Opciones& o, std::size_t n,
              std::size_t distintas, const Medida& m) {
    salida << nombre << ',' << n << ',' << o.columna << ',' << distintas << ',' << o.semilla << ','
           << (o.dedupe ? 1 : 0) << ','
           << m.t_construccion_us << ',' << m.escrituras_construccion << ',' << m.t_igualdad_us << ','
           << m.paginas_igualdad << ',' << m.filas_igualdad << ','
           << (m.soporta_rango ? 1 : 0) << ',' << m.t_rango_us << ','
           << m.paginas_rango << ',' << m.filas_rango << ',' << m.t_insercion_us << ',' << m.t_eliminacion_us
           << ',' << m.bytes_en_disco << ',' << m.paginas_estructura << ',' << m.detalle << "\n";
}

void resumir(const std::string& nombre, const Medida& m) {
    std::cout << "  " << nombre << ": construccion=" << m.t_construccion_us / 1000 << " ms"
              << " | igualdad=" << m.t_igualdad_us << " us/" << m.paginas_igualdad << " pag ("
              << m.filas_igualdad << " filas)";
    if (m.soporta_rango) {
        std::cout << " | rango=" << m.t_rango_us << " us/" << m.paginas_rango << " pag ("
                  << m.filas_rango << " filas)";
    } else {
        std::cout << " | rango=NO SOPORTADO";
    }
    std::cout << " | insertar=" << m.t_insercion_us << " us | borrar=" << m.t_eliminacion_us << " us"
              << " | " << m.bytes_en_disco / 1024 << " KB (" << m.detalle << ")\n";
}

}

int main(int argc, char** argv) {
    Opciones o;
    if (!leer_opciones(argc, argv, o)) return 1;

    std::vector<std::pair<int, long long>> pares;
    if (!cargar_columna(o, pares)) return 1;

    std::set<int> unicas;
    for (const auto& par : pares) unicas.insert(par.first);
    const std::vector<int> claves(unicas.begin(), unicas.end());

    std::cout << "n=" << pares.size() << " columna=" << o.columna << " claves distintas=" << claves.size()
              << " (" << static_cast<double>(pares.size()) / static_cast<double>(claves.size())
              << " filas por clave)\n";

    const Medida bplus = medir<BPlusNoAgrupado>(o, o.prefijo + ".bplus", pares, claves);
    const Medida hash = medir<HashExtensibleDisco>(o, o.prefijo + ".hash", pares, claves);
    resumir("bplus_no_agrupado", bplus);
    resumir("hash_extensible  ", hash);

    const bool primera = !existe(o.salida);
    std::ofstream salida(o.salida, std::ios::app);
    if (!salida) {
        std::cerr << "no se pudo abrir la salida: " << o.salida << " (existe el directorio?)\n";
        return 1;
    }
    if (primera) salida << kEncabezado << "\n";
    escribir(salida, "bplus_no_agrupado", o, pares.size(), claves.size(), bplus);
    escribir(salida, "hash_extensible", o, pares.size(), claves.size(), hash);
    return 0;
}
