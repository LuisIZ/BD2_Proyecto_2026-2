#pragma once

#include <charconv>
#include <string>
#include <vector>

namespace motor {
namespace sql {

inline std::string texto_microgrados(long long v) {
    const bool negativo = v < 0;
    if (negativo) v = -v;
    std::string decimales = std::to_string(v % 1000000);
    decimales.insert(0, 6 - decimales.size(), '0');
    return (negativo ? "-" : "") + std::to_string(v / 1000000) + "." + decimales;
}

// un valor de celda: entero, decimal (float32), texto o punto
struct Valor {
    bool es_entero = false;
    long long entero = 0;
    bool es_decimal = false;
    float decimal = 0;
    std::string texto;
    bool es_punto = false;
    int lat_e6 = 0;
    int lon_e6 = 0;

    static Valor de_entero(long long v) {
        Valor valor;
        valor.es_entero = true;
        valor.entero = v;
        return valor;
    }
    static Valor de_decimal(float v) {
        Valor valor;
        valor.es_decimal = true;
        valor.decimal = v;
        return valor;
    }
    static Valor de_texto(std::string t) {
        Valor valor;
        valor.texto = std::move(t);
        return valor;
    }
    static Valor de_punto(int lat_e6, int lon_e6) {
        Valor valor;
        valor.es_punto = true;
        valor.lat_e6 = lat_e6;
        valor.lon_e6 = lon_e6;
        return valor;
    }
    bool es_numero() const { return es_entero || es_decimal; }
    double numero() const { return es_decimal ? static_cast<double>(decimal) : static_cast<double>(entero); }
    std::string a_texto() const {
        if (es_decimal) {
            char buf[32];  // la representación más corta que vuelve al mismo float
            return std::string(buf, std::to_chars(buf, buf + sizeof(buf), decimal).ptr);
        }
        if (es_punto) return "POINT(" + texto_microgrados(lat_e6) + ", " + texto_microgrados(lon_e6) + ")";
        return es_entero ? std::to_string(entero) : texto;
    }
};

inline bool operator<(const Valor& a, const Valor& b) {
    if (a.es_entero && b.es_entero) return a.entero < b.entero;
    if (a.es_numero() && b.es_numero()) return a.numero() < b.numero();
    return a.a_texto() < b.a_texto();
}
inline bool operator>(const Valor& a, const Valor& b) { return b < a; }
inline bool operator==(const Valor& a, const Valor& b) {
    if (a.es_entero && b.es_entero) return a.entero == b.entero;
    if (a.es_numero() && b.es_numero()) return a.numero() == b.numero();
    return a.a_texto() == b.a_texto();
}
inline bool operator!=(const Valor& a, const Valor& b) { return !(a == b); }
inline bool operator<=(const Valor& a, const Valor& b) { return !(b < a); }
inline bool operator>=(const Valor& a, const Valor& b) { return !(a < b); }

using Fila = std::vector<Valor>;

}  // namespace sql
}  // namespace motor
