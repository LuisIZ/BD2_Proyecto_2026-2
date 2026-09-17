#pragma once

#include <string>
#include <vector>

namespace motor {
namespace sql {

// un valor de celda: entero o texto
struct Valor {
    bool es_entero = false;
    long long entero = 0;
    std::string texto;

    static Valor de_entero(long long v) {
        Valor valor;
        valor.es_entero = true;
        valor.entero = v;
        return valor;
    }
    static Valor de_texto(std::string t) {
        Valor valor;
        valor.texto = std::move(t);
        return valor;
    }
    std::string a_texto() const { return es_entero ? std::to_string(entero) : texto; }
};

inline bool operator<(const Valor& a, const Valor& b) {
    if (a.es_entero && b.es_entero) return a.entero < b.entero;
    return a.a_texto() < b.a_texto();
}
inline bool operator>(const Valor& a, const Valor& b) { return b < a; }
inline bool operator==(const Valor& a, const Valor& b) {
    if (a.es_entero && b.es_entero) return a.entero == b.entero;
    return a.a_texto() == b.a_texto();
}
inline bool operator!=(const Valor& a, const Valor& b) { return !(a == b); }
inline bool operator<=(const Valor& a, const Valor& b) { return !(b < a); }
inline bool operator>=(const Valor& a, const Valor& b) { return !(a < b); }

using Fila = std::vector<Valor>;

}  // namespace sql
}  // namespace motor
