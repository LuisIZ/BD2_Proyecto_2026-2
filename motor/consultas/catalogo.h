#pragma once

#include "valor.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace motor {
namespace sql {

enum class TipoColumna { INT, VARCHAR };
enum class Organizacion { HEAP, SEQUENTIAL, BPLUS };

struct Columna {
    std::string nombre;
    TipoColumna tipo = TipoColumna::INT;
    std::uint16_t tam = 0;  // largo máximo de VARCHAR
};

struct Indice {
    std::string nombre;
    std::string columna;
    std::string tipo;  // BPLUS
    std::string archivo;
};

struct Tabla {
    std::string nombre;
    Organizacion organizacion = Organizacion::HEAP;
    std::string archivo;
    std::size_t pk = 0;  // posición de la clave primaria en columnas
    std::vector<Columna> columnas;
    std::vector<Indice> indices;

    // -1 si no existe; ignora mayúsculas
    int posicion_columna(const std::string& nombre) const;
    const Indice* indice_sobre(const std::string& columna) const;
    // bytes de un registro de tamaño fijo (B+ agrupado): pk primero, luego el resto
    std::uint16_t tam_registro_fijo() const;
};

std::string nombre_organizacion(Organizacion o);
Organizacion organizacion_desde(const std::string& nombre);
std::string nombre_tipo(TipoColumna t);

// --- filas <-> bytes ---
//
// Variable (heap y secuencial): la clave va aparte (Registro.clave) y el resto
// de columnas se empaquetan en Registro.valor: INT como int32, VARCHAR como
// uint16 largo + bytes.
// Fijo (B+ agrupado): [int32 pk][INT int32 | VARCHAR n bytes rellenos con 0]...

int clave_de(const Tabla& tabla, const Fila& fila);
std::string empaquetar_variable(const Tabla& tabla, const Fila& fila);
Fila desempaquetar_variable(const Tabla& tabla, int clave, const std::string& valor);
std::vector<char> empaquetar_fijo(const Tabla& tabla, const Fila& fila);
Fila desempaquetar_fijo(const Tabla& tabla, const char* datos);

// valida tipos y largos; lanza std::runtime_error con el problema
void validar_fila(const Tabla& tabla, const Fila& fila);

// catálogo persistido en <dir>/catalogo.txt
class Catalogo {
public:
    explicit Catalogo(std::string dir);

    const std::string& dir() const { return dir_; }
    const std::map<std::string, Tabla>& tablas() const { return tablas_; }
    bool existe(const std::string& nombre) const;
    // lanza si no existe; ignora mayúsculas
    Tabla& tabla(const std::string& nombre);
    const Tabla& tabla(const std::string& nombre) const;
    void agregar(const Tabla& tabla);
    void quitar(const std::string& nombre);
    void guardar() const;

    std::string ruta_datos(const std::string& tabla, const std::string& extension) const;

private:
    void cargar();
    static std::string clave_mapa(const std::string& nombre);

    std::string dir_;
    std::map<std::string, Tabla> tablas_;
};

}  // namespace sql
}  // namespace motor
