// Demo de concurrencia: varios hilos depositan en la misma cuenta leyendo el saldo,
// esperando y escribiendo el saldo nuevo (DELETE + INSERT, el motor no tiene UPDATE).
//
//   transacciones_demo [--hilos 4] [--depositos 5] [--espera 5]
//
// Sin transacciones cada sentencia es atomica, pero entre leer y escribir otro hilo
// puede pisar el saldo: se pierden depositos (lost update). Con BEGIN ... END y 2PL
// estricto el saldo final es exacto; los deadlocks que aparecen al subir de S a X se
// detectan, la victima se deshace y reintenta.

#include "../consultas/catalogo.h"
#include "../consultas/ejecutor.h"
#include "../transacciones/gestor_locks.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using motor::sql::Catalogo;
using motor::sql::Ejecutor;
using motor::sql::Resultado;
using motor::transacciones::GestorLocks;

namespace {

std::string DB = ".build/db_demo_transacciones";
const std::string EVENTOS = "datos/resultados/transacciones_eventos.json";

struct Resumen {
    long long esperado = 0;
    long long obtenido = 0;
    int perdidos = 0;
    int deadlocks = 0;
};

void esperar_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void preparar() {
    std::error_code ec;
    std::filesystem::remove_all(DB, ec);
    if (std::filesystem::exists(DB)) DB += "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    e.ejecutar("CREATE TABLE cuentas (id INT PRIMARY KEY, saldo INT) USING HEAP");
    e.ejecutar("INSERT INTO cuentas VALUES (1, 0)");
}

bool leer_saldo(Ejecutor& e, long long& saldo) {
    const Resultado r = e.ejecutar("SELECT saldo FROM cuentas WHERE id = 1");
    if (r.filas.empty()) return false;
    saldo = r.filas[0][0].entero;
    return true;
}

Resumen correr(bool con_transacciones, int hilos, int depositos, int espera, GestorLocks& gestor) {
    preparar();
    std::atomic<int> siguiente_txn{1};
    std::atomic<int> perdidos{0};
    std::atomic<int> deadlocks{0};
    std::vector<std::thread> trabajadores;
    for (int h = 0; h < hilos; ++h) {
        trabajadores.emplace_back([&, h] {
            Catalogo catalogo(DB);
            Ejecutor e(catalogo);
            for (int i = 0; i < depositos; ++i) {
                while (true) {
                    e.usar_locks(&gestor, siguiente_txn++);
                    try {
                        if (con_transacciones) e.ejecutar("BEGIN");
                        long long saldo = 0;
                        while (!leer_saldo(e, saldo)) esperar_ms(1);
                        esperar_ms(espera);
                        e.ejecutar("DELETE FROM cuentas WHERE id = 1");
                        try {
                            e.ejecutar("INSERT INTO cuentas VALUES (1, " + std::to_string(saldo + 100) + ")");
                        } catch (const std::runtime_error& err) {
                            if (std::string(err.what()).find("repetida") == std::string::npos) throw;
                            ++perdidos;
                        }
                        if (con_transacciones) e.ejecutar("END");
                        break;
                    } catch (const std::runtime_error& err) {
                        if (std::string(err.what()).find("deadlock") == std::string::npos) throw;
                        ++deadlocks;
                        esperar_ms(espera * (h + 1));
                    }
                }
            }
        });
    }
    for (std::thread& t : trabajadores) t.join();

    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    Resumen r;
    r.esperado = 100LL * hilos * depositos;
    leer_saldo(e, r.obtenido);
    r.deadlocks = deadlocks;
    r.perdidos = static_cast<int>((r.esperado - r.obtenido) / 100);
    return r;
}

int leer_opcion(int argc, char** argv, const std::string& nombre, int defecto) {
    for (int i = 1; i + 1 < argc; ++i) if (argv[i] == nombre) return std::atoi(argv[i + 1]);
    return defecto;
}

}  // namespace

int main(int argc, char** argv) {
    const int hilos = leer_opcion(argc, argv, "--hilos", 4);
    const int depositos = leer_opcion(argc, argv, "--depositos", 5);
    const int espera = leer_opcion(argc, argv, "--espera", 5);
    std::cout << hilos << " hilos x " << depositos << " depositos de 100 sobre la misma cuenta, " << espera
              << " ms entre leer y escribir\n\n";

    GestorLocks sin_proteccion;
    const Resumen a = correr(false, hilos, depositos, espera, sin_proteccion);
    std::cout << "sin transacciones:   esperado " << a.esperado << ", obtenido " << a.obtenido << ", depositos perdidos "
              << a.perdidos << "\n";

    GestorLocks con_locks;
    const Resumen b = correr(true, hilos, depositos, espera, con_locks);
    std::cout << "con BEGIN/END y 2PL: esperado " << b.esperado << ", obtenido " << b.obtenido << ", deadlocks detectados "
              << b.deadlocks << " (cada victima se deshizo y reintento)\n";

    std::filesystem::create_directories("datos/resultados");
    std::ofstream(EVENTOS) << con_locks.eventos_json();
    std::cout << "\neventos de locks en " << EVENTOS << " (" << con_locks.eventos().size() << " eventos)\n";

    const bool ok = a.obtenido < a.esperado && b.obtenido == b.esperado;
    std::cout << (ok ? "demo correcta: la anomalia aparece sin locks y desaparece con ellos\n"
                     : "la demo no mostro la diferencia esperada\n");
    return ok ? 0 : 1;
}
