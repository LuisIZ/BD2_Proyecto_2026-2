#include "../consultas/catalogo.h"
#include "../consultas/ejecutor.h"
#include "../transacciones/gestor_locks.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using motor::sql::Catalogo;
using motor::sql::Ejecutor;
using motor::transacciones::EventoLock;
using motor::transacciones::GestorLocks;
using motor::transacciones::ModoLock;

namespace {

void esperar_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int contar(const GestorLocks& g, const std::string& accion, int txn) {
    int n = 0;
    for (const EventoLock& e : g.eventos()) if (e.accion == accion && e.txn == txn) ++n;
    return n;
}

void prueba_compartidos() {
    GestorLocks g;
    assert(g.adquirir(1, "cuentas", ModoLock::COMPARTIDO));
    assert(g.adquirir(2, "cuentas", ModoLock::COMPARTIDO));
    assert(contar(g, "espera", 2) == 0);
    g.liberar_todo(1);
    g.liberar_todo(2);
    std::cout << "locks: dos lectores comparten el recurso sin esperar\n";
}

void prueba_exclusivo() {
    GestorLocks g;
    assert(g.adquirir(1, "cuentas", ModoLock::EXCLUSIVO));
    std::atomic<bool> concedido{false};
    std::thread lector([&] {
        assert(g.adquirir(2, "cuentas", ModoLock::COMPARTIDO));
        concedido = true;
    });
    esperar_ms(50);
    assert(!concedido);
    g.liberar_todo(1);
    lector.join();
    assert(concedido && contar(g, "espera", 2) == 1);
    g.liberar_todo(2);
    std::cout << "locks: X bloquea a los demas hasta liberar (2PL estricto)\n";
}

void prueba_upgrade() {
    GestorLocks g;
    assert(g.adquirir(1, "cuentas", ModoLock::COMPARTIDO));
    assert(g.adquirir(1, "cuentas", ModoLock::EXCLUSIVO));
    assert(g.adquirir(1, "cuentas", ModoLock::COMPARTIDO));
    g.liberar_todo(1);
    std::cout << "locks: S sube a X si nadie mas lo tiene\n";
}

void prueba_deadlock_cruzado() {
    GestorLocks g;
    assert(g.adquirir(1, "a", ModoLock::EXCLUSIVO));
    assert(g.adquirir(2, "b", ModoLock::EXCLUSIVO));
    bool r1 = false, r2 = true;
    std::thread t1([&] { r1 = g.adquirir(1, "b", ModoLock::EXCLUSIVO); });
    esperar_ms(30);
    std::thread t2([&] {
        r2 = g.adquirir(2, "a", ModoLock::EXCLUSIVO);
        if (!r2) g.liberar_todo(2);
    });
    t2.join();
    t1.join();
    assert(r1 && !r2 && "la victima es T2, la mas joven");
    assert(contar(g, "deadlock", 2) == 1 && contar(g, "aborta", 2) == 1);
    g.liberar_todo(1);
    std::cout << "locks: deadlock T1 -> T2 -> T1 detectado, aborta T2\n";
}

void prueba_deadlock_upgrade() {
    GestorLocks g;
    assert(g.adquirir(1, "cuentas", ModoLock::COMPARTIDO));
    assert(g.adquirir(2, "cuentas", ModoLock::COMPARTIDO));
    bool r1 = false, r2 = true;
    std::thread t1([&] { r1 = g.adquirir(1, "cuentas", ModoLock::EXCLUSIVO); });
    esperar_ms(30);
    std::thread t2([&] {
        r2 = g.adquirir(2, "cuentas", ModoLock::EXCLUSIVO);
        if (!r2) g.liberar_todo(2);
    });
    t2.join();
    t1.join();
    assert(r1 && !r2);
    g.liberar_todo(1);
    assert(g.eventos_json().find("\"accion\":\"deadlock\"") != std::string::npos);
    std::cout << "locks: dos lectores que suben a X se bloquean; se detecta y aborta T2\n";
}

std::string DB = ".build/db_transacciones_test";

void limpiar_db() {
    std::error_code ec;
    std::filesystem::remove_all(DB, ec);
    if (std::filesystem::exists(DB)) DB += "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

bool falla(Ejecutor& e, const std::string& sql) {
    try {
        e.ejecutar(sql);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

long long contar_filas(Ejecutor& e, const std::string& tabla) {
    return e.ejecutar("SELECT COUNT(*) FROM " + tabla).filas[0][0].entero;
}

void prueba_ejecutor() {
    limpiar_db();
    Catalogo catalogo(DB);
    Ejecutor e(catalogo);
    for (const std::string org : {"HEAP", "SEQUENTIAL", "BPLUS"}) {
        const std::string t = "cuentas_" + org;
        e.ejecutar("CREATE TABLE " + t + " (nombre VARCHAR(10), id INT PRIMARY KEY, saldo INT) USING " + org);
        for (int i = 1; i <= 5; ++i) e.ejecutar("INSERT INTO " + t + " VALUES ('c" + std::to_string(i) + "', " + std::to_string(i) + ", 100)");

        assert(e.ejecutar("BEGIN TRANSACTION").tipo == "begin" && e.en_transaccion());
        e.ejecutar("INSERT INTO " + t + " VALUES ('nueva', 9, 50)");
        e.ejecutar("DELETE FROM " + t + " WHERE id <= 2");
        assert(contar_filas(e, t) == 4);
        assert(e.ejecutar("ROLLBACK").afectadas == 3 && !e.en_transaccion());
        assert(contar_filas(e, t) == 5);
        assert(e.ejecutar("SELECT nombre FROM " + t + " WHERE id = 2").filas[0][0].texto == "c2");
        assert(e.ejecutar("SELECT id FROM " + t + " WHERE id = 9").filas.empty());

        e.ejecutar("BEGIN");
        e.ejecutar("DELETE FROM " + t + " WHERE id = 5");
        e.ejecutar("INSERT INTO " + t + " VALUES ('otra', 6, 70)");
        assert(e.ejecutar("END TRANSACTION").tipo == "commit");
        assert(contar_filas(e, t) == 5 && e.ejecutar("SELECT saldo FROM " + t + " WHERE id = 6").filas[0][0].entero == 70);
    }

    assert(falla(e, "END") && falla(e, "ROLLBACK"));
    e.ejecutar("BEGIN");
    assert(falla(e, "BEGIN"));
    assert(falla(e, "CREATE TABLE otra (id INT)"));
    assert(falla(e, "DROP TABLE cuentas_HEAP"));
    e.ejecutar("COMMIT");
    assert(falla(e, "EXPLAIN ANALYZE ROLLBACK"));
    std::cout << "ejecutor: BEGIN/END/ROLLBACK deshacen INSERT y DELETE en las tres organizaciones\n";
}

void prueba_deadlock_ejecutor() {
    Catalogo c1(DB);
    Catalogo c2(DB);
    Ejecutor e1(c1);
    Ejecutor e2(c2);
    GestorLocks g;
    e1.usar_locks(&g, 1);
    e2.usar_locks(&g, 2);
    e1.ejecutar("BEGIN");
    e2.ejecutar("BEGIN");
    e1.ejecutar("INSERT INTO cuentas_HEAP VALUES ('t1', 20, 1)");
    e2.ejecutar("INSERT INTO cuentas_BPLUS VALUES ('t2', 20, 1)");
    bool fallo_t2 = false;
    std::thread h1([&] { e1.ejecutar("SELECT COUNT(*) FROM cuentas_BPLUS"); });
    esperar_ms(30);
    std::thread h2([&] {
        try {
            e2.ejecutar("SELECT COUNT(*) FROM cuentas_HEAP");
        } catch (const std::runtime_error& err) {
            fallo_t2 = std::string(err.what()).find("deadlock") != std::string::npos;
        }
    });
    h2.join();
    h1.join();
    assert(fallo_t2 && !e2.en_transaccion() && "T2 es la victima y su transaccion se deshace");
    e1.ejecutar("END");
    assert(e1.ejecutar("SELECT COUNT(*) FROM cuentas_BPLUS WHERE id = 20").filas[0][0].entero == 0);
    assert(e1.ejecutar("SELECT COUNT(*) FROM cuentas_HEAP WHERE id = 20").filas[0][0].entero == 1);
    std::cout << "ejecutor: deadlock entre dos transacciones, la victima se deshace y la otra confirma\n";
}

}  // namespace

int main() {
    prueba_compartidos();
    prueba_exclusivo();
    prueba_upgrade();
    prueba_deadlock_cruzado();
    prueba_deadlock_upgrade();
    prueba_ejecutor();
    prueba_deadlock_ejecutor();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
