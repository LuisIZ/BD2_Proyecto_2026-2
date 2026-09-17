#include "catalogo.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace motor {
namespace sql {

namespace {

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> partir(const std::string& linea, char separador) {
    std::vector<std::string> campos;
    std::string actual;
    for (const char c : linea) {
        if (c == separador) { campos.push_back(actual); actual.clear(); }
        else actual.push_back(c);
    }
    campos.push_back(actual);
    return campos;
}

void poner_int32(std::string& salida, long long v) {
    const std::int32_t x = static_cast<std::int32_t>(v);
    salida.append(reinterpret_cast<const char*>(&x), sizeof(x));
}

}  // namespace

// --- Tabla ---

int Tabla::posicion_columna(const std::string& buscada) const {
    const std::string b = minusculas(buscada);
    for (std::size_t i = 0; i < columnas.size(); ++i) {
        if (minusculas(columnas[i].nombre) == b) return static_cast<int>(i);
    }
    return -1;
}

const Indice* Tabla::indice_sobre(const std::string& columna) const {
    const std::string c = minusculas(columna);
    for (const Indice& indice : indices) {
        if (minusculas(indice.columna) == c) return &indice;
    }
    return nullptr;
}

std::uint16_t Tabla::tam_registro_fijo() const {
    std::size_t tam = 0;
    for (const Columna& col : columnas) tam += col.tipo == TipoColumna::INT ? 4 : col.tam;
    return static_cast<std::uint16_t>(tam);
}

std::string nombre_organizacion(Organizacion o) {
    switch (o) {
        case Organizacion::HEAP: return "HEAP";
        case Organizacion::SEQUENTIAL: return "SEQUENTIAL";
        case Organizacion::BPLUS: return "BPLUS";
    }
    return "HEAP";
}

Organizacion organizacion_desde(const std::string& nombre) {
    if (nombre == "SEQUENTIAL") return Organizacion::SEQUENTIAL;
    if (nombre == "BPLUS") return Organizacion::BPLUS;
    return Organizacion::HEAP;
}

std::string nombre_tipo(TipoColumna t) { return t == TipoColumna::INT ? "INT" : "VARCHAR"; }

// --- filas <-> bytes ---

int clave_de(const Tabla& tabla, const Fila& fila) {
    return static_cast<int>(fila[tabla.pk].entero);
}

void validar_fila(const Tabla& tabla, const Fila& fila) {
    if (fila.size() != tabla.columnas.size()) {
        throw std::runtime_error("la tabla " + tabla.nombre + " tiene " + std::to_string(tabla.columnas.size()) +
                                 " columnas y se dieron " + std::to_string(fila.size()) + " valores");
    }
    for (std::size_t i = 0; i < fila.size(); ++i) {
        const Columna& col = tabla.columnas[i];
        if (col.tipo == TipoColumna::INT) {
            if (!fila[i].es_entero) throw std::runtime_error("la columna " + col.nombre + " es INT");
            if (fila[i].entero < -2147483648LL || fila[i].entero > 2147483647LL) {
                throw std::runtime_error("la columna " + col.nombre + " no admite " + std::to_string(fila[i].entero));
            }
        } else {
            if (fila[i].es_entero) throw std::runtime_error("la columna " + col.nombre + " es VARCHAR");
            if (fila[i].texto.size() > col.tam) {
                throw std::runtime_error("la columna " + col.nombre + " admite " + std::to_string(col.tam) +
                                         " caracteres y se dieron " + std::to_string(fila[i].texto.size()));
            }
        }
    }
}

std::string empaquetar_variable(const Tabla& tabla, const Fila& fila) {
    std::string salida;
    for (std::size_t i = 0; i < tabla.columnas.size(); ++i) {
        if (i == tabla.pk) continue;
        if (tabla.columnas[i].tipo == TipoColumna::INT) {
            poner_int32(salida, fila[i].entero);
        } else {
            const std::uint16_t largo = static_cast<std::uint16_t>(fila[i].texto.size());
            salida.append(reinterpret_cast<const char*>(&largo), sizeof(largo));
            salida.append(fila[i].texto);
        }
    }
    return salida;
}

Fila desempaquetar_variable(const Tabla& tabla, int clave, const std::string& valor) {
    Fila fila(tabla.columnas.size());
    std::size_t pos = 0;
    auto exigir = [&](std::size_t n) {
        if (pos + n > valor.size()) throw std::runtime_error("registro corrupto en " + tabla.nombre);
    };
    for (std::size_t i = 0; i < tabla.columnas.size(); ++i) {
        if (i == tabla.pk) { fila[i] = Valor::de_entero(clave); continue; }
        if (tabla.columnas[i].tipo == TipoColumna::INT) {
            exigir(4);
            std::int32_t x;
            std::memcpy(&x, valor.data() + pos, 4);
            pos += 4;
            fila[i] = Valor::de_entero(x);
        } else {
            exigir(2);
            std::uint16_t largo;
            std::memcpy(&largo, valor.data() + pos, 2);
            pos += 2;
            exigir(largo);
            fila[i] = Valor::de_texto(valor.substr(pos, largo));
            pos += largo;
        }
    }
    return fila;
}

std::vector<char> empaquetar_fijo(const Tabla& tabla, const Fila& fila) {
    std::vector<char> salida(tabla.tam_registro_fijo(), 0);
    std::size_t pos = 0;
    auto poner = [&](std::size_t i) {
        const Columna& col = tabla.columnas[i];
        if (col.tipo == TipoColumna::INT) {
            const std::int32_t x = static_cast<std::int32_t>(fila[i].entero);
            std::memcpy(salida.data() + pos, &x, 4);
            pos += 4;
        } else {
            std::memcpy(salida.data() + pos, fila[i].texto.data(), fila[i].texto.size());
            pos += col.tam;
        }
    };
    poner(tabla.pk);
    for (std::size_t i = 0; i < tabla.columnas.size(); ++i) {
        if (i != tabla.pk) poner(i);
    }
    return salida;
}

Fila desempaquetar_fijo(const Tabla& tabla, const char* datos) {
    Fila fila(tabla.columnas.size());
    std::size_t pos = 0;
    auto leer = [&](std::size_t i) {
        const Columna& col = tabla.columnas[i];
        if (col.tipo == TipoColumna::INT) {
            std::int32_t x;
            std::memcpy(&x, datos + pos, 4);
            pos += 4;
            fila[i] = Valor::de_entero(x);
        } else {
            std::size_t largo = 0;
            while (largo < col.tam && datos[pos + largo] != '\0') ++largo;
            fila[i] = Valor::de_texto(std::string(datos + pos, largo));
            pos += col.tam;
        }
    };
    leer(tabla.pk);
    for (std::size_t i = 0; i < tabla.columnas.size(); ++i) {
        if (i != tabla.pk) leer(i);
    }
    return fila;
}

// --- Catalogo ---

Catalogo::Catalogo(std::string dir) : dir_(std::move(dir)) {
    std::filesystem::create_directories(dir_);
    cargar();
}

std::string Catalogo::clave_mapa(const std::string& nombre) { return minusculas(nombre); }

bool Catalogo::existe(const std::string& nombre) const {
    return tablas_.count(clave_mapa(nombre)) > 0;
}

Tabla& Catalogo::tabla(const std::string& nombre) {
    auto it = tablas_.find(clave_mapa(nombre));
    if (it == tablas_.end()) throw std::runtime_error("la tabla " + nombre + " no existe");
    return it->second;
}

const Tabla& Catalogo::tabla(const std::string& nombre) const {
    auto it = tablas_.find(clave_mapa(nombre));
    if (it == tablas_.end()) throw std::runtime_error("la tabla " + nombre + " no existe");
    return it->second;
}

void Catalogo::agregar(const Tabla& tabla) { tablas_[clave_mapa(tabla.nombre)] = tabla; }

void Catalogo::quitar(const std::string& nombre) { tablas_.erase(clave_mapa(nombre)); }

std::string Catalogo::ruta_datos(const std::string& tabla, const std::string& extension) const {
    return (std::filesystem::path(dir_) / (minusculas(tabla) + extension)).string();
}

void Catalogo::guardar() const {
    const std::string ruta = (std::filesystem::path(dir_) / "catalogo.txt").string();
    std::ofstream salida(ruta, std::ios::trunc);
    if (!salida) throw std::runtime_error("no se pudo escribir " + ruta);
    for (const auto& [clave, t] : tablas_) {
        salida << "TABLA|" << t.nombre << '|' << nombre_organizacion(t.organizacion) << '|' << t.archivo
               << '|' << t.pk << '\n';
        for (const Columna& c : t.columnas) {
            salida << "COL|" << c.nombre << '|' << nombre_tipo(c.tipo) << '|' << c.tam << '\n';
        }
        for (const Indice& i : t.indices) {
            salida << "IDX|" << i.nombre << '|' << i.columna << '|' << i.tipo << '|' << i.archivo << '\n';
        }
        salida << "FIN\n";
    }
}

void Catalogo::cargar() {
    const std::string ruta = (std::filesystem::path(dir_) / "catalogo.txt").string();
    std::ifstream entrada(ruta);
    if (!entrada) return;

    std::string linea;
    Tabla actual;
    bool abierta = false;
    while (std::getline(entrada, linea)) {
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.empty()) continue;
        const std::vector<std::string> campos = partir(linea, '|');
        if (campos[0] == "TABLA" && campos.size() == 5) {
            actual = Tabla{};
            actual.nombre = campos[1];
            actual.organizacion = organizacion_desde(campos[2]);
            actual.archivo = campos[3];
            actual.pk = static_cast<std::size_t>(std::stoul(campos[4]));
            abierta = true;
        } else if (campos[0] == "COL" && campos.size() == 4 && abierta) {
            Columna c;
            c.nombre = campos[1];
            c.tipo = campos[2] == "INT" ? TipoColumna::INT : TipoColumna::VARCHAR;
            c.tam = static_cast<std::uint16_t>(std::stoul(campos[3]));
            actual.columnas.push_back(c);
        } else if (campos[0] == "IDX" && campos.size() == 5 && abierta) {
            actual.indices.push_back({campos[1], campos[2], campos[3], campos[4]});
        } else if (campos[0] == "FIN" && abierta) {
            tablas_[clave_mapa(actual.nombre)] = actual;
            abierta = false;
        } else {
            throw std::runtime_error("catalogo corrupto: " + linea);
        }
    }
}

}  // namespace sql
}  // namespace motor
