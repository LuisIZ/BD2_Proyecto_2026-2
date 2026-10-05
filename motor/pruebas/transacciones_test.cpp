#include "../transacciones/gestor_locks.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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

}  // namespace

int main() {
    prueba_compartidos();
    prueba_exclusivo();
    prueba_upgrade();
    prueba_deadlock_cruzado();
    prueba_deadlock_upgrade();
    std::cout << "Prueba completada correctamente.\n";
    return 0;
}
