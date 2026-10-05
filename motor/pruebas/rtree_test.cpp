#include "../indices/rtree.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using motor::espacial::distancia_m;
using motor::espacial::Metrica;
using motor::espacial::Punto;

namespace {

const std::string RUTA = ".build/rtree_test.rtree";
const int N = 100000;

void limpiar() {
    std::error_code ec;
    std::filesystem::remove(RUTA, ec);
}

std::vector<Punto> puntos_de_lima() {
    std::mt19937 generador(42);
    std::uniform_int_distribution<int> lat(-12300000, -11800000);
    std::uniform_int_distribution<int> lon(-77200000, -76800000);
    std::vector<Punto> puntos(N);
    for (Punto& p : puntos) p = {lat(generador), lon(generador)};
    return puntos;
}

std::vector<long long> rango_secuencial(const std::vector<Punto>& puntos, Punto c, double r, Metrica m) {
    std::vector<long long> salida;
    for (int i = 0; i < N; ++i) if (distancia_m(c, puntos[i], m) <= r) salida.push_back(i);
    return salida;
}

std::vector<double> distancias_knn_secuencial(const std::vector<Punto>& puntos, Punto c, int k, Metrica m) {
    std::vector<double> d(N);
    for (int i = 0; i < N; ++i) d[i] = distancia_m(c, puntos[i], m);
    std::partial_sort(d.begin(), d.begin() + k, d.end());
    d.resize(k);
    return d;
}

void prueba_construccion(RTree& arbol, const std::vector<Punto>& puntos) {
    for (int i = 0; i < N; ++i) arbol.insertar(puntos[i], i);
    assert(arbol.num_entradas() == N);
    assert(arbol.verificar_invariante() && "el MBR de cada nodo contiene a sus hijos");
    std::cout << "construccion: " << N << " puntos, altura " << arbol.altura() << ", " << arbol.num_nodos()
              << " nodos, " << arbol.tamano_en_disco() / 1024 << " KB; invariante de MBR correcto\n";
}

void prueba_rango(RTree& arbol, const std::vector<Punto>& puntos) {
    std::mt19937 generador(7);
    for (int q = 0; q < 10; ++q) {
        const Punto c = puntos[generador() % N];
        for (const double radio : {1000.0, 5000.0, 10000.0}) {
            for (const Metrica m : {Metrica::HAVERSINE, Metrica::EUCLIDIANA}) {
                std::vector<long long> obtenido = arbol.rango(c, radio, m);
                std::sort(obtenido.begin(), obtenido.end());
                assert(obtenido == rango_secuencial(puntos, c, radio, m) && "el rango coincide con la busqueda secuencial");
            }
        }
    }
    arbol.rango(puntos[0], 1000.0, Metrica::HAVERSINE);
    const long visitados = arbol.nodos_visitados();
    assert(visitados < arbol.num_nodos() / 10 && "un radio chico visita pocos nodos");
    std::cout << "rango: 1, 5 y 10 km iguales a la busqueda secuencial; un radio de 1 km visita " << visitados << " de "
              << arbol.num_nodos() << " nodos\n";
}

void prueba_caja(RTree& arbol, const std::vector<Punto>& puntos) {
    std::mt19937 generador(13);
    std::uniform_int_distribution<int> lat(-12300000, -11900000);
    std::uniform_int_distribution<int> lon(-77200000, -76900000);
    std::uniform_int_distribution<int> lado(10000, 100000);
    for (int q = 0; q < 10; ++q) {
        const Punto minimo{lat(generador), lon(generador)};
        const Punto maximo{minimo.lat_e6 + lado(generador), minimo.lon_e6 + lado(generador)};
        std::vector<long long> obtenido = arbol.en_caja(minimo, maximo);
        std::sort(obtenido.begin(), obtenido.end());
        std::vector<long long> esperado;
        for (int i = 0; i < N; ++i) {
            const Punto& p = puntos[i];
            if (p.lat_e6 >= minimo.lat_e6 && p.lat_e6 <= maximo.lat_e6 && p.lon_e6 >= minimo.lon_e6 && p.lon_e6 <= maximo.lon_e6) {
                esperado.push_back(i);
            }
        }
        assert(obtenido == esperado && "la caja coincide con la busqueda secuencial");
    }
    std::cout << "caja: 10 rectangulos al azar iguales a la busqueda secuencial\n";
}

void prueba_knn(RTree& arbol, const std::vector<Punto>& puntos) {
    std::mt19937 generador(11);
    for (int q = 0; q < 10; ++q) {
        const Punto c = puntos[generador() % N];
        for (const int k : {10, 50, 100}) {
            const std::vector<long long> obtenido = arbol.knn(c, k, Metrica::HAVERSINE);
            assert(static_cast<int>(obtenido.size()) == k);
            const std::vector<double> esperado = distancias_knn_secuencial(puntos, c, k, Metrica::HAVERSINE);
            for (int i = 0; i < k; ++i) {
                assert(std::fabs(distancia_m(c, puntos[obtenido[i]], Metrica::HAVERSINE) - esperado[i]) < 1e-6 &&
                       "el k-NN coincide con la busqueda secuencial y viene ordenado");
            }
        }
    }
    arbol.knn(puntos[0], 10, Metrica::HAVERSINE);
    std::cout << "knn: k = 10, 50 y 100 iguales a la busqueda secuencial; k = 10 visita " << arbol.nodos_visitados()
              << " nodos\n";
}

void prueba_eliminar(RTree& arbol, const std::vector<Punto>& puntos) {
    for (int i = 0; i < 1000; ++i) assert(arbol.eliminar(puntos[i], i));
    assert(!arbol.eliminar(puntos[0], 0) && "ya no estaba");
    assert(arbol.num_entradas() == N - 1000);
    const std::vector<long long> cerca = arbol.rango(puntos[5], 1.0, Metrica::HAVERSINE);
    assert(std::find(cerca.begin(), cerca.end(), 5) == cerca.end());
    assert(arbol.verificar_invariante());
    std::cout << "eliminar: 1000 puntos borrados dejan de aparecer en las consultas\n";
}

void prueba_persistencia(const std::vector<Punto>& puntos) {
    std::vector<long long> antes;
    {
        RTree arbol(RUTA, false);
        antes = arbol.knn(puntos[2000], 20, Metrica::HAVERSINE);
        arbol.sincronizar();
    }
    RTree arbol(RUTA, false);
    assert(arbol.num_entradas() == N - 1000 && arbol.verificar_invariante());
    assert(arbol.knn(puntos[2000], 20, Metrica::HAVERSINE) == antes);
    std::cout << "persistencia: cierra, reabre y responde igual\n";
}

}  // namespace

int main() {
    std::filesystem::create_directories(".build");
    limpiar();
    const std::vector<Punto> puntos = puntos_de_lima();
    {
        RTree arbol(RUTA, true);
        prueba_construccion(arbol, puntos);
        prueba_rango(arbol, puntos);
        prueba_caja(arbol, puntos);
        prueba_knn(arbol, puntos);
        prueba_eliminar(arbol, puntos);
        arbol.sincronizar();
    }
    prueba_persistencia(puntos);
    limpiar();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
