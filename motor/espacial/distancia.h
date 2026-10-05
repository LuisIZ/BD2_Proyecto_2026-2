#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace motor {
namespace espacial {

// coordenadas en microgrados (grados * 1 000 000), en orden (lat, lon)
struct Punto {
    int lat_e6 = 0;
    int lon_e6 = 0;
};

enum class Metrica { EUCLIDIANA, HAVERSINE };

const double RADIO_TIERRA_M = 6371000.0;

inline Metrica metrica_desde(const std::string& nombre) {
    return nombre == "EUCLIDIANA" ? Metrica::EUCLIDIANA : Metrica::HAVERSINE;
}

// distancia en metros
inline double distancia_m(Punto a, Punto b, Metrica metrica) {
    const double rad = std::acos(-1.0) / 180.0;
    const double lat1 = a.lat_e6 / 1e6 * rad;
    const double lat2 = b.lat_e6 / 1e6 * rad;
    const double dlat = lat2 - lat1;
    const double dlon = (b.lon_e6 - a.lon_e6) / 1e6 * rad;
    if (metrica == Metrica::EUCLIDIANA) return std::sqrt(dlat * dlat + dlon * dlon) * RADIO_TIERRA_M;
    const double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(lat1) * std::cos(lat2) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * RADIO_TIERRA_M * std::asin(std::sqrt(std::min(1.0, h)));
}

}  // namespace espacial
}  // namespace motor
