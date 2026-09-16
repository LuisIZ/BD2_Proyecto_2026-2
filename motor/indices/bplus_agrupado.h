#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace motor {

// Índice B+ agrupado: las hojas son las páginas de datos y guardan el registro
// completo ordenado por clave. Todo vive en un archivo de páginas de 4 KB.
//
// Los registros son de tamaño fijo (tam_registro bytes) y llevan la clave int
// en sus primeros 4 bytes. El árbol no interpreta el resto del registro, así
// cualquier struct trivialmente copiable que empiece con `int clave` sirve.
class BPlusAgrupado {
public:
    // recibe un registro del rango; devolver false corta el recorrido
    using Visitante = std::function<bool(const void* registro)>;

    // abre el archivo; si está vacío o truncar es true lo crea con tam_registro.
    // max_regs_hoja / max_hijos en 0 usan todo lo que cabe en la página; valores
    // menores recortan la capacidad para ver el árbol crecer con pocos datos.
    // Al abrir uno existente se comprueba que tam_registro coincida con la cabecera.
    BPlusAgrupado(const std::string& ruta, std::uint16_t tam_registro,
                  bool truncar = false, std::uint16_t max_regs_hoja = 0,
                  std::uint16_t max_hijos = 0);
    ~BPlusAgrupado();

    BPlusAgrupado(const BPlusAgrupado&) = delete;
    BPlusAgrupado& operator=(const BPlusAgrupado&) = delete;

    // --- API por bytes: registro apunta a tam_registro bytes ---

    // false si la clave ya existe
    bool insertar_bytes(const void* registro);
    // copia el registro en salida; false si no está
    bool buscar_bytes(int clave, void* salida);
    // visita en orden los registros con desde <= clave <= hasta
    void buscar_rango_bytes(int desde, int hasta, const Visitante& visitante);
    bool eliminar(int clave);
    // construye de abajo hacia arriba llenando al factor_llenado. Exige árbol
    // vacío y n registros contiguos ordenados por clave sin repetidos
    void cargar_masivo_bytes(const void* registros, std::size_t n,
                             double factor_llenado = 0.9);

    // --- API tipada: sizeof(R) == tam_registro y R empieza con int clave ---

    template <typename R>
    bool insertar(const R& registro) {
        comprobar_tipo<R>();
        return insertar_bytes(&registro);
    }

    template <typename R>
    bool buscar(int clave, R& salida) {
        comprobar_tipo<R>();
        return buscar_bytes(clave, &salida);
    }

    template <typename R>
    std::vector<R> buscar_rango(int desde, int hasta) {
        comprobar_tipo<R>();
        std::vector<R> salida;
        buscar_rango_bytes(desde, hasta, [&](const void* registro) {
            R copia;
            std::memcpy(&copia, registro, sizeof(R));
            salida.push_back(copia);
            return true;
        });
        return salida;
    }

    // ordena el vector por clave y carga
    template <typename R>
    void cargar_masivo(std::vector<R>& registros, double factor_llenado = 0.9) {
        comprobar_tipo<R>();
        std::sort(registros.begin(), registros.end(), [](const R& a, const R& b) {
            return clave_de(&a) < clave_de(&b);
        });
        cargar_masivo_bytes(registros.data(), registros.size(), factor_llenado);
    }

    static int clave_de(const void* registro) {
        int clave;
        std::memcpy(&clave, registro, sizeof(int));
        return clave;
    }

    // --- estado ---
    std::uint16_t tam_registro() const;
    std::uint16_t max_regs_hoja() const;
    std::uint16_t max_hijos() const;
    int altura();
    long num_registros() const;
    long tamano_en_disco();
    void contar_paginas(long& internas, long& hojas);

    // vuelca cabecera y páginas sucias al disco
    void sincronizar();
    // vacía la caché para medir con lecturas frías
    void enfriar_cache();

    // --- contadores ---
    long paginas_leidas() const;
    long paginas_escritas() const;
    long aciertos_cache() const;
    long fallos_cache() const;
    long desalojos_cache() const;
    int paginas_en_cache() const;

    // --- verificación, para pruebas ---
    // todas las claves siguiendo la cadena de hojas
    std::vector<int> claves_en_orden();
    // hojas a la misma altura, claves crecientes, ocupación mínima y separadores correctos
    bool verificar_invariantes();

private:
    template <typename R>
    void comprobar_tipo() const {
        static_assert(std::is_trivially_copyable<R>::value,
                      "el registro debe poder copiarse byte a byte a la pagina");
        static_assert(sizeof(R) >= sizeof(int), "el registro debe empezar con la clave int");
        comprobar_tam(sizeof(R));
    }
    void comprobar_tam(std::size_t tam) const;

    struct Impl;
    Impl* impl_;
};

}  // namespace motor
