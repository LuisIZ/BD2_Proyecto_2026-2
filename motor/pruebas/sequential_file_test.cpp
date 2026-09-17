#include "../archivos/pagina_slotted.h"
#include "../archivos/sequential_file.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

const std::string RUTA = ".build/prueba_secuencial.seq";

motor::Registro reg(int clave) { return {clave, "valor-" + std::to_string(clave)}; }

std::vector<int> barajadas(int n, unsigned semilla) {
    std::vector<int> claves(n);
    std::iota(claves.begin(), claves.end(), 1);
    std::mt19937 generador(semilla);
    std::shuffle(claves.begin(), claves.end(), generador);
    return claves;
}

bool iguales(const std::vector<motor::Registro>& a, const std::vector<motor::Registro>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].clave != b[i].clave || a[i].valor != b[i].valor) return false;
    }
    return true;
}

bool ordenado(const std::vector<motor::Registro>& registros) {
    for (std::size_t i = 1; i < registros.size(); ++i) {
        if (registros[i].clave < registros[i - 1].clave) return false;
    }
    return true;
}

void imprimir(const char* titulo, const motor::EstadisticasSecuencial& e) {
    std::cout << titulo << ": " << e << "\n";
}

// el flujo de la prueba original, ahora contra un archivo real
void prueba_basica() {
    motor::SequentialFile archivo(RUTA, true, 1.0);
    assert(archivo.insertar(reg(2)));
    assert(archivo.insertar(reg(1)));
    assert(archivo.insertar(reg(4)));
    assert(archivo.insertar(reg(3)));
    assert(archivo.insertar(reg(5)));

    const std::vector<motor::Registro> orden = archivo.scan();
    assert(orden.size() == 5);
    for (std::size_t i = 0; i < orden.size(); ++i) assert(orden[i].clave == static_cast<int>(i + 1));
    assert(archivo.buscar(3).value().valor == "valor-3");
    assert(!archivo.buscar(9).has_value());
    assert(archivo.verificar_orden());

    // la única página cabe en 4 KB, no hay auxiliar todavía
    const motor::EstadisticasSecuencial e = archivo.stats_secuencial();
    assert(e.paginas_principal == 1 && e.paginas_auxiliares == 0);
    assert(e.tamano_archivo_bytes == 2 * motor::PAGE_SIZE);  // cabecera + 1 página
    std::cout << "basica: 5 registros ordenados en una pagina de disco\n";
}

// inserción manteniendo el orden: en disco, con muchas páginas y llegada aleatoria
void prueba_insercion_ordenada() {
    const int n = 3000;
    motor::SequentialFile archivo(RUTA, true, 0.30);
    for (int clave : barajadas(n, 7)) assert(archivo.insertar(reg(clave)));

    assert(archivo.verificar_orden());
    const std::vector<motor::Registro> orden = archivo.scan();
    assert(static_cast<int>(orden.size()) == n);
    for (int i = 0; i < n; ++i) assert(orden[i].clave == i + 1);
    for (int clave = 1; clave <= n; clave += 97) assert(archivo.buscar(clave).value().clave == clave);

    const motor::EstadisticasSecuencial e = archivo.stats_secuencial();
    assert(e.paginas_principal > 5);
    assert(e.reorganizaciones > 0 && "con llegada aleatoria el auxiliar desborda y reorganiza");
    assert(e.tamano_archivo_bytes == (1 + e.paginas_principal + e.paginas_auxiliares) * motor::PAGE_SIZE);
    imprimir("insercion ordenada", e);
}

// cuando la página no tiene sitio el registro va al auxiliar; scan y buscar lo ven igual
void prueba_area_auxiliar() {
    motor::SequentialFile archivo(RUTA, true, 1.0);  // umbral 1.0: nunca reorganiza sola
    for (int clave = 1; clave <= 400; ++clave) assert(archivo.insertar(reg(clave * 10)));

    const motor::EstadisticasSecuencial antes = archivo.stats_secuencial();
    assert(antes.paginas_principal == 1 && "sin reorganizar, el principal es la primera pagina");
    assert(antes.registros_auxiliares > 0 && antes.paginas_auxiliares > 0);

    // uno en medio, otro al inicio, otro al final: todos caen al auxiliar
    assert(archivo.insertar(reg(1005)));
    assert(archivo.insertar(reg(1)));
    assert(archivo.insertar(reg(9999)));
    assert(archivo.buscar(1005).has_value());
    assert(archivo.buscar(1).has_value());
    assert(archivo.buscar(9999).has_value());

    const std::vector<motor::Registro> todo = archivo.scan();
    assert(todo.size() == 403 && ordenado(todo));
    assert(todo.front().clave == 1 && todo.back().clave == 9999);
    assert(archivo.verificar_orden());

    // reorganizar manualmente vacía el auxiliar y deja todo en el principal
    archivo.reorganizar();
    const motor::EstadisticasSecuencial despues = archivo.stats_secuencial();
    assert(despues.registros_auxiliares == 0 && despues.paginas_auxiliares == 0);
    assert(despues.paginas_principal > 1 && despues.registros_vivos == 403);
    assert(iguales(archivo.scan(), todo));
    assert(archivo.verificar_orden());
    imprimir("auxiliar tras reorganizar", despues);
}

// eliminación lazy: la tumba queda en disco y solo desaparece al reorganizar
void prueba_eliminacion_lazy() {
    motor::SequentialFile archivo(RUTA, true, 1.0);
    for (int clave = 1; clave <= 50; ++clave) assert(archivo.insertar(reg(clave)));
    const std::size_t bytes_antes = archivo.tamano_en_disco();

    assert(archivo.eliminar(25));
    assert(!archivo.eliminar(25) && "ya era tumba");
    assert(!archivo.eliminar(500) && "no existe");
    assert(!archivo.buscar(25).has_value());

    motor::EstadisticasSecuencial e = archivo.stats_secuencial();
    assert(e.tumbas == 1 && e.registros_vivos == 49);
    assert(e.bytes_desperdiciados > 0);
    assert(std::fabs(e.porcentaje_desperdicio - 1.0 / 50.0) < 1e-9);
    assert(archivo.tamano_en_disco() == bytes_antes && "lazy: el archivo no cambia de tamano");

    const std::vector<motor::Registro> vivos = archivo.scan();
    assert(vivos.size() == 49 && ordenado(vivos));
    assert(std::none_of(vivos.begin(), vivos.end(), [](const motor::Registro& r) { return r.clave == 25; }));

    // reinsertar la clave borrada: entra en su sitio junto a la tumba
    assert(archivo.insertar(reg(25)));
    assert(archivo.buscar(25).has_value());
    assert(archivo.verificar_orden());
    e = archivo.stats_secuencial();
    assert(e.tumbas == 1 && e.registros_vivos == 50);
    std::cout << "eliminacion lazy: tumba en disco, buscar/scan la ignoran, reinsercion ok\n";
}

// reorganización automática al superar el 30 % de desperdicio
void prueba_reorganizacion_automatica() {
    motor::SequentialFile archivo(RUTA, true, 0.30);
    for (int clave = 1; clave <= 100; ++clave) assert(archivo.insertar(reg(clave)));
    const std::size_t reorgs_carga = archivo.stats_secuencial().reorganizaciones;

    // 30 tumbas de 100 = 30 %: todavía no (la condición es estricta)
    for (int clave = 1; clave <= 30; ++clave) assert(archivo.eliminar(clave));
    motor::EstadisticasSecuencial e = archivo.stats_secuencial();
    assert(e.tumbas == 30 && e.reorganizaciones == reorgs_carga);
    assert(std::fabs(e.porcentaje_desperdicio - 0.30) < 1e-9);

    // la 31 dispara la reorganización: sin tumbas, sin auxiliar, orden intacto
    assert(archivo.eliminar(31));
    e = archivo.stats_secuencial();
    assert(e.reorganizaciones == reorgs_carga + 1);
    assert(e.tumbas == 0 && e.registros_auxiliares == 0 && e.bytes_desperdiciados == 0);
    assert(e.registros_vivos == 69);
    assert(e.ultima_reorganizacion_us >= 0 && e.tiempo_reorganizaciones_us >= e.ultima_reorganizacion_us);
    assert(archivo.verificar_orden());

    const std::vector<motor::Registro> vivos = archivo.scan();
    assert(vivos.size() == 69 && vivos.front().clave == 32 && vivos.back().clave == 100);
    imprimir("reorganizacion automatica", e);
}

// el factor de llenado deja hueco en cada página para que las inserciones no caigan al auxiliar
void prueba_factor_llenado() {
    std::size_t paginas_lleno = 0;
    {
        motor::SequentialFile lleno(RUTA, true, 1.0, 1.0);
        for (int clave = 1; clave <= 2000; ++clave) assert(lleno.insertar(reg(clave * 2)));
        lleno.reorganizar();
        paginas_lleno = lleno.paginas_principal();
        // sin hueco: la siguiente inserción en medio va derecha al auxiliar
        assert(lleno.insertar(reg(1001)));
        assert(lleno.stats_secuencial().registros_auxiliares == 1);
    }
    {
        motor::SequentialFile holgado(RUTA, true, 1.0, 0.8);
        for (int clave = 1; clave <= 2000; ++clave) assert(holgado.insertar(reg(clave * 2)));
        holgado.reorganizar();
        assert(holgado.paginas_principal() > paginas_lleno);
        // con 20 % libre, entra en su página sin tocar el auxiliar
        assert(holgado.insertar(reg(1001)));
        assert(holgado.stats_secuencial().registros_auxiliares == 0);
        assert(holgado.verificar_orden());
        std::cout << "factor de llenado: 1.0 -> " << paginas_lleno << " pag, 0.8 -> "
                  << holgado.paginas_principal() << " pag\n";
    }
}

// cerrar y reabrir: todo sigue en el archivo, incluidos tumbas y auxiliar
void prueba_persistencia() {
    std::vector<motor::Registro> esperado;
    {
        motor::SequentialFile archivo(RUTA, true, 1.0);
        for (int clave : barajadas(800, 11)) assert(archivo.insertar(reg(clave)));
        for (int clave = 1; clave <= 800; clave += 5) assert(archivo.eliminar(clave));
        esperado = archivo.scan();
        assert(archivo.stats_secuencial().tumbas == 160);
        assert(archivo.stats_secuencial().registros_auxiliares > 0);
    }
    {
        motor::SequentialFile archivo(RUTA);
        const motor::EstadisticasSecuencial e = archivo.stats_secuencial();
        assert(e.registros_vivos == 640 && e.tumbas == 160);
        assert(e.registros_auxiliares > 0 && "los contadores se reconstruyen al abrir");
        assert(archivo.umbral_reorganizacion() == 1.0);
        assert(iguales(archivo.scan(), esperado));
        assert(!archivo.buscar(6).has_value() && archivo.buscar(7).has_value());
        assert(archivo.verificar_orden());
        archivo.reorganizar();
        assert(iguales(archivo.scan(), esperado));
    }
    {
        motor::SequentialFile archivo(RUTA);
        assert(archivo.stats_secuencial().tumbas == 0);
        assert(iguales(archivo.scan(), esperado));
    }

    bool rechazado = false;
    try {
        motor::SequentialFile otro(".build/prueba_heap.heap");
    } catch (const std::invalid_argument&) {
        rechazado = true;
    } catch (const std::runtime_error&) {
        rechazado = true;  // si el archivo del heap no existe en este entorno
    }
    assert(rechazado && "abrir un archivo que no es secuencial debe fallar");
    std::cout << "persistencia: reabre con 640 vivos, 160 tumbas y auxiliar reconstruido\n";
}

void prueba_rango() {
    motor::SequentialFile archivo(RUTA, true, 1.0);
    for (int clave : barajadas(600, 13)) assert(archivo.insertar(reg(clave)));
    assert(archivo.stats_secuencial().registros_auxiliares > 0 && "hay datos en principal y auxiliar");

    const std::vector<motor::Registro> medio = archivo.buscar_rango(100, 199);
    assert(medio.size() == 100 && ordenado(medio));
    assert(medio.front().clave == 100 && medio.back().clave == 199);
    assert(archivo.buscar_rango(700, 800).empty());
    assert(archivo.buscar_rango(50, 10).empty());
    assert(archivo.buscar_rango(-5, 5).size() == 5);
    assert(archivo.buscar_rango(595, 5000).size() == 6);
    std::cout << "rango: [100,199] mezcla principal y auxiliar en orden\n";
}

void prueba_duplicados_y_limites() {
    motor::SequentialFile archivo(RUTA, true, 1.0);
    assert(archivo.insertar({7, "primero"}));
    assert(archivo.insertar({7, "segundo"}));
    assert(archivo.scan().size() == 2 && "como el heap, admite claves repetidas");
    assert(archivo.buscar(7).value().valor == "primero");
    assert(archivo.eliminar(7));
    assert(archivo.buscar(7).value().valor == "segundo");
    assert(archivo.eliminar(7));
    assert(!archivo.buscar(7).has_value());

    assert(!archivo.insertar({1, std::string(5000, 'x')}) && "no cabe en una pagina");
    assert(archivo.insertar({1, std::string(4000, 'x')}));
    assert(archivo.buscar(1).value().valor.size() == 4000);
    std::cout << "duplicados y limites: repetidos permitidos, registro de 4000 B ok\n";
}

// la búsqueda por clave en el principal cuesta ~log2(paginas) lecturas
void prueba_costo_busqueda() {
    motor::SequentialFile archivo(RUTA, true, 0.30);
    for (int clave = 1; clave <= 20000; ++clave) assert(archivo.insertar(reg(clave)));
    archivo.reorganizar();
    const std::uint32_t paginas = archivo.paginas_principal();
    const double cota = std::log2(static_cast<double>(paginas)) + 2.0;

    archivo.reiniciar_contadores();
    const int consultas = 200;
    std::mt19937 generador(17);
    std::uniform_int_distribution<int> sorteo(1, 20000);
    for (int i = 0; i < consultas; ++i) assert(archivo.buscar(sorteo(generador)).has_value());
    const double lecturas = static_cast<double>(archivo.paginas_leidas()) / consultas;
    assert(lecturas <= cota);
    std::cout << "costo busqueda: " << paginas << " paginas principales, "
              << lecturas << " lecturas por busqueda (cota " << cota << ")\n";
}

}  // namespace

int main() {
    std::filesystem::create_directories(".build");
    prueba_basica();
    prueba_insercion_ordenada();
    prueba_area_auxiliar();
    prueba_eliminacion_lazy();
    prueba_reorganizacion_automatica();
    prueba_factor_llenado();
    prueba_persistencia();
    prueba_rango();
    prueba_duplicados_y_limites();
    prueba_costo_busqueda();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
