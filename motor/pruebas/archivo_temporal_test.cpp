#include "../consultas/archivo_temporal.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using motor::sql::ArchivoTemporal;
using motor::sql::Fila;
using motor::sql::Valor;
namespace fs = std::filesystem;

// fila i con los 5 tipos; el texto y el vector cambian de largo para que las páginas no sean iguales
static Fila fila_de_prueba(int i) {
    std::vector<float> v(1 + i % 16);
    for (std::size_t j = 0; j < v.size(); ++j) v[j] = static_cast<float>(i) / 7.0f + static_cast<float>(j);
    return {Valor::de_entero(i - 5000), Valor::de_decimal(static_cast<float>(i) * 0.25f),
            Valor::de_texto("fila " + std::to_string(i) + std::string(i % 40, 'x')),
            Valor::de_punto(-12000000 - i, -77000000 + i), Valor::de_vector(v)};
}

static bool iguales(const Fila& a, const Fila& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].es_entero != b[i].es_entero || a[i].es_decimal != b[i].es_decimal ||
            a[i].es_punto != b[i].es_punto || a[i].es_vector != b[i].es_vector)
            return false;
        if (a[i].entero != b[i].entero || a[i].decimal != b[i].decimal || a[i].texto != b[i].texto ||
            a[i].lat_e6 != b[i].lat_e6 || a[i].lon_e6 != b[i].lon_e6 || a[i].componentes != b[i].componentes)
            return false;
    }
    return true;
}

int main() {
    const fs::path base = ".build/archivo_temporal_test";
    fs::remove_all(base);
    const int N = 10000;

    // codificar y decodificar sin archivo, con los 5 tipos
    for (int i : {0, 1, 4999, 9999}) {
        const Fila fila = fila_de_prueba(i);
        assert(iguales(motor::sql::decodificar_fila(motor::sql::codificar_fila(fila)), fila));
    }
    std::cout << "codificar_fila: INT, FLOAT, VARCHAR, POINT y VECTOR vuelven iguales\n";

    // las páginas esperadas: se llenan en orden hasta 4094 bytes de filas
    std::size_t esperadas = 0, ocupado = motor::sql::PAGINA_TEMPORAL;
    for (int i = 0; i < N; ++i) {
        const std::size_t tam = motor::sql::codificar_fila(fila_de_prueba(i)).size();
        if (ocupado + tam > motor::sql::PAGINA_TEMPORAL - motor::sql::CABECERA_TEMPORAL) {
            ++esperadas;
            ocupado = 0;
        }
        ocupado += tam;
    }

    motor::PlanTrace traza("logs/archivo_temporal_test.log");
    fs::path ruta;
    {
        ArchivoTemporal temporal(base, &traza);
        ruta = temporal.ruta();
        assert(ruta.parent_path() == base / "tmp");
        for (int i = 0; i < N; ++i) temporal.agregar(fila_de_prueba(i));
        temporal.cerrar_escritura();
        assert(temporal.filas() == static_cast<std::size_t>(N));
        assert(temporal.paginas_escritas() == esperadas && esperadas > 100);
        assert(fs::file_size(ruta) == esperadas * motor::sql::PAGINA_TEMPORAL);

        Fila fila;
        int leidas = 0;
        while (temporal.siguiente(fila)) {
            assert(iguales(fila, fila_de_prueba(leidas)));
            ++leidas;
        }
        assert(leidas == N);
        assert(temporal.paginas_leidas() == esperadas);
        std::cout << "10000 filas: " << esperadas << " paginas de 4 KB escritas y leidas, todas iguales\n";

        // releer desde el inicio suma otra pasada
        temporal.reiniciar_lectura();
        assert(temporal.siguiente(fila) && iguales(fila, fila_de_prueba(0)));
        while (temporal.siguiente(fila)) {}
        assert(temporal.paginas_leidas() == 2 * esperadas);
        assert(traza.paginas_escritas == esperadas && traza.paginas_leidas == 2 * esperadas);
        std::cout << "reiniciar_lectura: segunda pasada completa; PlanTrace suma las paginas\n";

        // moverlo no borra el archivo; lo borra el nuevo dueño al salir
        ArchivoTemporal movido = std::move(temporal);
        assert(fs::exists(ruta));
        movido.reiniciar_lectura();
        assert(movido.siguiente(fila) && iguales(fila, fila_de_prueba(0)));
    }
    assert(!fs::exists(ruta));
    std::cout << "destructor: el archivo temporal se borra\n";

    // un archivo vacío no tiene páginas
    {
        ArchivoTemporal vacio(base);
        vacio.cerrar_escritura();
        Fila fila;
        assert(!vacio.siguiente(fila));
        assert(vacio.paginas_escritas() == 0 && vacio.paginas_leidas() == 0);
    }

    // errores: fila más grande que una página, escribir después de cerrar, leer antes de cerrar
    {
        ArchivoTemporal temporal(base);
        Fila fila;
        bool fallo = false;
        try {
            temporal.siguiente(fila);
        } catch (const std::runtime_error& e) {
            fallo = std::string(e.what()) == "el archivo temporal sigue abierto para escritura";
        }
        assert(fallo);

        fallo = false;
        try {
            temporal.agregar({Valor::de_entero(1), Valor::de_texto(std::string(4090, 'a'))});
        } catch (const std::runtime_error& e) {
            fallo = std::string(e.what()).find("la fila no cabe en una pagina") == 0;
        }
        assert(fallo);
        temporal.agregar({Valor::de_texto(std::string(4000, 'a'))});  // cerca del límite sí cabe

        temporal.cerrar_escritura();
        fallo = false;
        try {
            temporal.agregar({Valor::de_entero(1)});
        } catch (const std::runtime_error& e) {
            fallo = std::string(e.what()) == "el archivo temporal ya se cerro para escritura";
        }
        assert(fallo);
        assert(temporal.siguiente(fila) && fila[0].texto.size() == 4000);
    }
    std::cout << "errores: fila de mas de 4 KB, escribir tras cerrar y leer antes de cerrar\n";

    assert(fs::is_empty(base / "tmp"));
    fs::remove_all(base);
    std::cout << "Prueba completada correctamente.\n";
}
