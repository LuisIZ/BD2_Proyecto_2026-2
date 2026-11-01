#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace motor {
namespace texto {
namespace detalle {

inline void reemplazar_todos(std::string& texto, const std::string& origen, const std::string& destino) {
    std::size_t posicion = 0;
    while ((posicion = texto.find(origen, posicion)) != std::string::npos) {
        texto.replace(posicion, origen.size(), destino);
        posicion += destino.size();
    }
}

inline std::string ascii_sin_tildes(std::string texto) {
    const std::pair<const char*, const char*> reemplazos[] = {
        {"á", "a"}, {"à", "a"}, {"ä", "a"}, {"â", "a"}, {"ã", "a"}, {"å", "a"},
        {"é", "e"}, {"è", "e"}, {"ë", "e"}, {"ê", "e"}, {"í", "i"}, {"ì", "i"},
        {"ï", "i"}, {"î", "i"}, {"ó", "o"}, {"ò", "o"}, {"ö", "o"}, {"ô", "o"},
        {"õ", "o"}, {"ú", "u"}, {"ù", "u"}, {"ü", "u"}, {"û", "u"}, {"ñ", "n"},
        {"ç", "c"}, {"ý", "y"}, {"ÿ", "y"}, {"Á", "a"}, {"À", "a"}, {"Ä", "a"},
        {"Â", "a"}, {"Ã", "a"}, {"Å", "a"}, {"É", "e"}, {"È", "e"}, {"Ë", "e"},
        {"Ê", "e"}, {"Í", "i"}, {"Ì", "i"}, {"Ï", "i"}, {"Î", "i"}, {"Ó", "o"},
        {"Ò", "o"}, {"Ö", "o"}, {"Ô", "o"}, {"Õ", "o"}, {"Ú", "u"}, {"Ù", "u"},
        {"Ü", "u"}, {"Û", "u"}, {"Ñ", "n"}, {"Ç", "c"}, {"Ý", "y"}, {"ß", "ss"},
    };
    for (const auto& reemplazo : reemplazos) reemplazar_todos(texto, reemplazo.first, reemplazo.second);
    return texto;
}

inline const std::unordered_set<std::string>& stopwords_ingles() {
    static const std::unordered_set<std::string> palabras = [] {
        std::unordered_set<std::string> salida;
        const char* configurada = std::getenv("BD2_STOPWORDS_EN");
        const std::string ruta = configurada && *configurada ? configurada : "datos/stopwords_en.txt";
        std::ifstream archivo(ruta);
        if (!archivo) throw std::runtime_error("no se pudo abrir la lista de stopwords en ingles: " + ruta);
        std::string palabra;
        while (std::getline(archivo, palabra)) {
            palabra.erase(std::remove_if(palabra.begin(), palabra.end(), [](unsigned char c) {
                return std::isspace(c) != 0;
            }), palabra.end());
            if (!palabra.empty() && palabra[0] != '#') salida.insert(palabra);
        }
        return salida;
    }();
    return palabras;
}

inline bool consonante(const std::string& palabra, int i) {
    switch (palabra[static_cast<std::size_t>(i)]) {
        case 'a': case 'e': case 'i': case 'o': case 'u': return false;
        case 'y': return i == 0 || !consonante(palabra, i - 1);
        default: return true;
    }
}

inline int medida(const std::string& palabra, int fin) {
    int m = 0;
    bool anterior_vocal = false;
    for (int i = 0; i <= fin; ++i) {
        const bool vocal = !consonante(palabra, i);
        if (anterior_vocal && !vocal) ++m;
        anterior_vocal = vocal;
    }
    return m;
}

inline bool contiene_vocal(const std::string& palabra, int fin) {
    for (int i = 0; i <= fin; ++i) if (!consonante(palabra, i)) return true;
    return false;
}

inline bool doble_consonante(const std::string& palabra) {
    const std::size_t n = palabra.size();
    return n >= 2 && palabra[n - 1] == palabra[n - 2] && consonante(palabra, static_cast<int>(n - 1));
}

inline bool cvc(const std::string& palabra, int fin) {
    if (fin < 2 || !consonante(palabra, fin) || consonante(palabra, fin - 1) || !consonante(palabra, fin - 2))
        return false;
    const char c = palabra[static_cast<std::size_t>(fin)];
    return c != 'w' && c != 'x' && c != 'y';
}

inline bool reemplazar_sufijo(std::string& palabra, const std::string& sufijo, const std::string& reemplazo,
                              int medida_minima = 0) {
    if (palabra.size() < sufijo.size() || palabra.compare(palabra.size() - sufijo.size(), sufijo.size(), sufijo) != 0)
        return false;
    const int fin_raiz = static_cast<int>(palabra.size() - sufijo.size()) - 1;
    if (fin_raiz < 0 || medida(palabra, fin_raiz) <= medida_minima) return false;
    palabra.replace(static_cast<std::size_t>(fin_raiz + 1), sufijo.size(), reemplazo);
    return true;
}

// Porter (1980), pasos 1a–5b; aplicado únicamente a tokens ASCII.
inline std::string stem_porter(std::string palabra) {
    if (palabra.size() <= 2) return palabra;
    if (palabra[0] == 'y') palabra[0] = 'Y';

    if (palabra.size() > 3 && palabra.compare(palabra.size() - 3, 3, "sses") == 0) palabra.resize(palabra.size() - 2);
    else if (palabra.size() > 2 && palabra.compare(palabra.size() - 2, 2, "ies") == 0) palabra.replace(palabra.size() - 2, 2, "i");
    else if (palabra.size() > 1 && palabra.compare(palabra.size() - 1, 1, "s") == 0 &&
             palabra.compare(palabra.size() - 2, 1, "s") != 0) palabra.pop_back();

    bool paso1b = false;
    if (!reemplazar_sufijo(palabra, "eed", "ee", 0)) {
        if (palabra.size() > 2 && palabra.compare(palabra.size() - 2, 2, "ed") == 0 &&
            contiene_vocal(palabra, static_cast<int>(palabra.size()) - 3)) {
            palabra.resize(palabra.size() - 2);
            paso1b = true;
        } else if (palabra.size() > 3 && palabra.compare(palabra.size() - 3, 3, "ing") == 0 &&
                   contiene_vocal(palabra, static_cast<int>(palabra.size()) - 4)) {
            palabra.resize(palabra.size() - 3);
            paso1b = true;
        }
    }
    if (paso1b) {
        if (palabra.size() >= 2 && palabra.compare(palabra.size() - 2, 2, "at") == 0) palabra += "e";
        else if (palabra.size() >= 2 && palabra.compare(palabra.size() - 2, 2, "bl") == 0) palabra += "e";
        else if (palabra.size() >= 2 && palabra.compare(palabra.size() - 2, 2, "iz") == 0) palabra += "e";
        else if (doble_consonante(palabra) && palabra.back() != 'l' && palabra.back() != 's' && palabra.back() != 'z')
            palabra.pop_back();
        else if (medida(palabra, static_cast<int>(palabra.size()) - 1) == 1 &&
                 cvc(palabra, static_cast<int>(palabra.size()) - 1)) palabra += "e";
    }
    if (palabra.size() > 1 && palabra.back() == 'y' && contiene_vocal(palabra, static_cast<int>(palabra.size()) - 2))
        palabra.back() = 'i';

    const std::pair<const char*, const char*> paso2[] = {
        {"ational", "ate"}, {"tional", "tion"}, {"enci", "ence"}, {"anci", "ance"}, {"izer", "ize"},
        {"abli", "able"}, {"alli", "al"}, {"entli", "ent"}, {"eli", "e"}, {"ousli", "ous"},
        {"ization", "ize"}, {"ation", "ate"}, {"ator", "ate"}, {"alism", "al"}, {"iveness", "ive"},
        {"fulness", "ful"}, {"ousness", "ous"}, {"aliti", "al"}, {"iviti", "ive"}, {"biliti", "ble"},
    };
    for (const auto& regla : paso2) if (reemplazar_sufijo(palabra, regla.first, regla.second, 0)) break;
    const std::pair<const char*, const char*> paso3[] = {
        {"icate", "ic"}, {"ative", ""}, {"alize", "al"}, {"iciti", "ic"}, {"ical", "ic"},
        {"ful", ""}, {"ness", ""},
    };
    for (const auto& regla : paso3) if (reemplazar_sufijo(palabra, regla.first, regla.second, 0)) break;

    const char* paso4[] = {"al", "ance", "ence", "er", "ic", "able", "ible", "ant", "ement", "ment",
                           "ent", "ou", "ism", "ate", "iti", "ous", "ive", "ize"};
    bool eliminado = false;
    for (const char* sufijo : paso4) {
        if (reemplazar_sufijo(palabra, sufijo, "", 1)) { eliminado = true; break; }
    }
    if (!eliminado && palabra.size() > 3 && palabra.compare(palabra.size() - 3, 3, "ion") == 0 &&
        (palabra[palabra.size() - 4] == 's' || palabra[palabra.size() - 4] == 't') &&
        medida(palabra, static_cast<int>(palabra.size()) - 4) > 1) palabra.resize(palabra.size() - 3);
    if (palabra.size() > 1 && palabra.back() == 'e') {
        const int m = medida(palabra, static_cast<int>(palabra.size()) - 2);
        if (m > 1 || (m == 1 && !cvc(palabra, static_cast<int>(palabra.size()) - 2))) palabra.pop_back();
    }
    if (palabra.size() > 1 && palabra.back() == 'l' && doble_consonante(palabra) &&
        medida(palabra, static_cast<int>(palabra.size()) - 1) > 1) palabra.pop_back();
    if (!palabra.empty() && palabra[0] == 'Y') palabra[0] = 'y';
    return palabra;
}

}  // namespace detalle

// Normaliza diacríticos latinos, descarta signos, filtra stopwords inglesas y aplica Porter.
inline std::vector<std::string> tokenizar(const std::string& texto) {
    const std::string normalizado = detalle::ascii_sin_tildes(texto);
    std::vector<std::string> salida;
    std::string palabra;
    const auto procesar = [&]() {
        if (palabra.empty()) return;
        if (detalle::stopwords_ingles().count(palabra) == 0) salida.push_back(detalle::stem_porter(palabra));
        palabra.clear();
    };
    for (unsigned char c : normalizado) {
        if (std::isalpha(c)) palabra.push_back(static_cast<char>(std::tolower(c)));
        else procesar();
    }
    procesar();
    return salida;
}

}  // namespace texto
}  // namespace motor
