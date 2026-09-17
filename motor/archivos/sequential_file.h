#pragma once

#include "../comun/archivo.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace motor {

constexpr std::uint32_t MAGIC_SEQ   = 0x53455150u;  // "SEQP"
constexpr std::uint16_t VERSION_SEQ = 1;

struct EstadisticasSecuencial {
    std::size_t paginas_principal = 0;
    std::size_t paginas_auxiliares = 0;
    std::size_t registros_vivos = 0;
    std::size_t registros_auxiliares = 0;
    std::size_t tumbas = 0;
    std::size_t bytes_desperdiciados = 0;
    std::size_t tamano_archivo_bytes = 0;
    double porcentaje_desperdicio = 0.0;
    std::size_t reorganizaciones = 0;
    long long ultima_reorganizacion_us = 0;
    long long tiempo_reorganizaciones_us = 0;
};

std::ostream& operator<<(std::ostream& salida, const EstadisticasSecuencial& estadisticas);

// Archivo secuencial paginado en disco. Páginas de PAGE_SIZE bytes: la página 0 es
// la cabecera; después vienen las páginas del área principal, ordenadas por clave
// dentro de cada página y entre páginas; y al final las del área auxiliar, en
// orden de llegada. Los registros se codifican igual que en el heap
// ([int32 clave][uint32 largo][valor]) para que el espacio en disco sea comparable.
//
// En RAM solo hay dos buffers de una página y unos contadores.
class SequentialFile final : public IFileOrganization {
public:
    // abre o crea el archivo. umbral: fracción de registros fuera de sitio
    // (tumbas + auxiliares) que dispara la reorganización automática.
    // factor_llenado: cuánto se llena cada página al reorganizar; el resto queda
    // libre para que las inserciones siguientes no caigan al auxiliar.
    explicit SequentialFile(const std::string& ruta, bool truncar = false,
                            double umbral_reorganizacion = 0.30,
                            double factor_llenado = 0.80);
    ~SequentialFile() override;

    SequentialFile(const SequentialFile&) = delete;
    SequentialFile& operator=(const SequentialFile&) = delete;

    // inserta en la página que le toca por clave; si no hay sitio va al auxiliar
    bool insertar(const Registro& registro) override;
    // eliminación lazy: marca la tumba, el espacio se recupera al reorganizar
    bool eliminar(int clave) override;
    // todos los vivos en orden de clave (principal fusionado con auxiliar)
    std::vector<Registro> scan() const override;
    EstadisticasArchivo stats() const override;
    // fusiona principal + auxiliar en páginas nuevas sin tumbas
    void reorganizar() override;

    // búsqueda binaria sobre las páginas principales y luego recorrido del auxiliar
    std::optional<Registro> buscar(int clave) const;
    std::vector<Registro> buscar_rango(int desde, int hasta) const;

    EstadisticasSecuencial stats_secuencial() const;
    void set_umbral_reorganizacion(double umbral);
    double umbral_reorganizacion() const noexcept;
    double factor_llenado() const noexcept;

    std::uint32_t num_paginas() const noexcept;
    std::uint32_t paginas_principal() const noexcept;
    std::size_t tamano_en_disco() const;

    std::size_t paginas_leidas() const noexcept;
    std::size_t paginas_escritas() const noexcept;
    void reiniciar_contadores() noexcept;

    // para pruebas: el área principal está ordenada en disco y cada página es consistente
    bool verificar_orden() const;

private:
    struct Ubicacion {
        std::uint32_t pagina;
        std::uint16_t slot;
    };

    static std::streamoff offset_de(std::uint32_t page_id) noexcept;

    void abrir_o_crear(const std::string& ruta, bool truncar);
    void abrir(const std::string& ruta);
    void escribir_cabecera_archivo();
    void leer_cabecera_archivo();
    void recontar();
    void leer_pagina(std::uint32_t page_id, std::byte* destino) const;
    void escribir_pagina(std::uint32_t page_id, const std::byte* origen);
    std::uint32_t crear_pagina();

    // última página principal cuya primera clave es <= clave (0 si ninguna)
    std::uint32_t pagina_para(int clave) const;
    bool localizar(int clave, Ubicacion& donde) const;
    void insertar_auxiliar(const std::byte* datos, std::uint16_t largo);
    void reorganizar_si_corresponde();
    double porcentaje_desperdicio() const;

    std::string ruta_;
    mutable std::fstream archivo_;
    mutable std::vector<std::byte> buffer_;
    mutable std::vector<std::byte> buffer_aux_;

    std::uint32_t num_paginas_ = 0;
    std::uint32_t paginas_principal_ = 0;
    double umbral_reorganizacion_ = 0.30;
    double factor_llenado_ = 0.80;

    std::size_t registros_vivos_ = 0;
    std::size_t registros_auxiliares_ = 0;
    std::size_t tumbas_ = 0;
    std::size_t bytes_muertos_ = 0;
    std::size_t reorganizaciones_ = 0;
    long long ultima_reorganizacion_us_ = 0;
    long long tiempo_reorganizaciones_us_ = 0;

    mutable std::size_t paginas_leidas_ = 0;
    std::size_t paginas_escritas_ = 0;
};

}  // namespace motor
