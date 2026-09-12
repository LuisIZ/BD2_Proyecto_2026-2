#pragma once

#include "../comun/archivo.h"

#include <cstddef>
#include <vector>

namespace motor {

class SequentialFile final : public IFileOrganization {
public:
    explicit SequentialFile(std::size_t capacidad_pagina = 64,
                            double umbral_reorganizacion = 0.30);

    bool insertar(const Registro& registro) override;
    bool eliminar(int clave) override;
    std::vector<Registro> scan() const override;
    EstadisticasArchivo stats() const override;
    void reorganizar() override;

    void set_umbral_reorganizacion(double umbral);
    std::size_t capacidad_pagina() const noexcept;

private:
    struct Slot {
        Registro registro;
        bool tumba = false;
    };

    struct Pagina {
        std::vector<Slot> slots;
    };

    std::size_t pagina_para(int clave) const;
    bool contiene(int clave) const;
    bool marcar_tumba(std::vector<Slot>& slots, int clave);
    void reorganizar_si_corresponde();
    double porcentaje_desperdicio() const;

    std::size_t capacidad_pagina_;
    double umbral_reorganizacion_;
    std::vector<Pagina> paginas_;
    std::vector<Slot> auxiliar_;
    long long ultima_reorganizacion_us_ = 0;
};

}  // namespace motor