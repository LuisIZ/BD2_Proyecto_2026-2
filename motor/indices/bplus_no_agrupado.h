#pragma once

#include <cstdint>
#include <string>
#include <vector>

class BPlusNoAgrupado {
public:
    // abre el índice en ruta; con truncar en true lo crea vacío
    explicit BPlusNoAgrupado(const std::string& ruta, bool truncar = false);
    ~BPlusNoAgrupado();

    BPlusNoAgrupado(const BPlusNoAgrupado&) = delete;
    BPlusNoAgrupado& operator=(const BPlusNoAgrupado&) = delete;

    // inserta el par (clave, pos) if ya existe no hace nada
    void insertar(int clave, long long pos);
    std::vector<long long> buscar(int clave);
    // devuelve las pos con clave entre desde y hasta, en orden
    std::vector<long long> buscar_rango(int desde, int hasta);
    int eliminar(int clave);
    // borra un par exacto; devuelve false si no estaba
    bool eliminar_entrada(int clave, long long pos);

    int altura();
    long num_entradas() const;
    long tamano_en_disco();

    void sincronizar();
    void enfriar_cache();

    long paginas_leidas() const;
    long paginas_escritas() const;
    long aciertos_cache() const;
    long fallos_cache() const;
    long desalojos_cache() const;
    int paginas_en_cache() const;

private:
    struct Impl;
    Impl* impl_;
};
