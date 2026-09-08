#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace motor {

struct Registro {
    int clave;
    std::string valor;
};

struct EstadisticasArchivo {
    std::size_t registros = 0;
    std::size_t tumbas = 0;
    std::size_t paginas = 0;
    std::size_t registros_auxiliares = 0;
    double porcentaje_desperdicio = 0.0;
    double umbral_reorganizacion = 0.30;
    long long ultima_reorganizacion_us = 0;
};

class IFileOrganization {
public:
    virtual ~IFileOrganization() = default;

    virtual bool insertar(const Registro& registro) = 0;
    virtual bool eliminar(int clave) = 0;
    virtual std::vector<Registro> scan() const = 0;
    virtual EstadisticasArchivo stats() const = 0;
    virtual void reorganizar() = 0;
};

}  // namespace motor