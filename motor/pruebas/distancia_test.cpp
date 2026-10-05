#include "../espacial/distancia.h"

#include <cassert>
#include <cmath>
#include <iostream>

using motor::espacial::distancia_m;
using motor::espacial::Metrica;
using motor::espacial::metrica_desde;
using motor::espacial::Punto;

namespace {

bool cerca(double obtenido, double esperado, double tolerancia) {
    return std::fabs(obtenido - esperado) <= tolerancia;
}

const Punto ORIGEN{0, 0};
const Punto LIMA{-12046400, -77042800};
const Punto SAN_ISIDRO{-12097700, -77036500};
const Punto CUSCO{-13532000, -71967500};

void prueba_grado() {
    const double un_grado = motor::espacial::RADIO_TIERRA_M * std::acos(-1.0) / 180.0;
    for (const Metrica m : {Metrica::HAVERSINE, Metrica::EUCLIDIANA}) {
        assert(cerca(distancia_m(ORIGEN, {1000000, 0}, m), un_grado, 0.01));
        assert(cerca(distancia_m(ORIGEN, {0, 1000000}, m), un_grado, 0.01));
    }
    assert(cerca(un_grado, 111194.93, 0.01));
    std::cout << "un grado: " << un_grado << " m en latitud y en longitud sobre el ecuador\n";
}

void prueba_propiedades() {
    for (const Metrica m : {Metrica::HAVERSINE, Metrica::EUCLIDIANA}) {
        assert(distancia_m(LIMA, LIMA, m) == 0.0);
        assert(cerca(distancia_m(LIMA, CUSCO, m), distancia_m(CUSCO, LIMA, m), 1e-6));
        assert(distancia_m(LIMA, SAN_ISIDRO, m) < distancia_m(LIMA, CUSCO, m));
    }
    std::cout << "propiedades: distancia cero al mismo punto, simetria y orden\n";
}

void prueba_ciudades() {
    assert(cerca(distancia_m(LIMA, SAN_ISIDRO, Metrica::HAVERSINE), 5745.3, 1.0));
    assert(cerca(distancia_m(LIMA, CUSCO, Metrica::HAVERSINE), 574578.4, 100.0));
    assert(cerca(distancia_m(LIMA, SAN_ISIDRO, Metrica::EUCLIDIANA), distancia_m(LIMA, SAN_ISIDRO, Metrica::HAVERSINE), 5.0));
    std::cout << "ciudades: Lima a San Isidro " << distancia_m(LIMA, SAN_ISIDRO, Metrica::HAVERSINE)
              << " m, Lima a Cusco " << distancia_m(LIMA, CUSCO, Metrica::HAVERSINE) / 1000 << " km\n";
}

void prueba_latitud_alta() {
    const Punto a{60000000, 0};
    const Punto b{60000000, 1000000};
    const double haversine = distancia_m(a, b, Metrica::HAVERSINE);
    const double euclidiana = distancia_m(a, b, Metrica::EUCLIDIANA);
    assert(cerca(haversine, 55596.9, 1.0));
    assert(euclidiana > 1.9 * haversine);
    std::cout << "latitud 60: un grado de longitud mide " << haversine << " m (haversine) y "
              << euclidiana << " m (euclidiana)\n";
}

void prueba_metrica_desde() {
    assert(metrica_desde("EUCLIDIANA") == Metrica::EUCLIDIANA);
    assert(metrica_desde("HAVERSINE") == Metrica::HAVERSINE);
    assert(metrica_desde("") == Metrica::HAVERSINE);
}

}  // namespace

int main() {
    prueba_grado();
    prueba_propiedades();
    prueba_ciudades();
    prueba_latitud_alta();
    prueba_metrica_desde();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
