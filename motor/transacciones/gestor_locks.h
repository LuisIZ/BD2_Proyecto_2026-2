#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace motor {
namespace transacciones {

enum class ModoLock { COMPARTIDO, EXCLUSIVO };

struct EventoLock {
    int txn = 0;
    std::string recurso;
    std::string accion;  // pide, concede, espera, deadlock, aborta, libera
    std::string modo;    // S | X
    std::string detalle;
    double ms = 0.0;
};

// locks S/X por recurso con 2PL estricto: se liberan todos juntos al terminar la transaccion
class GestorLocks {
public:
    GestorLocks() : inicio_(std::chrono::steady_clock::now()) {}

    // bloquea hasta conceder el lock; devuelve false si la transaccion fue elegida victima de un deadlock
    bool adquirir(int txn, const std::string& recurso, ModoLock modo) {
        std::unique_lock<std::mutex> guardia(m_);
        if (victimas_.count(txn)) return false;
        Recurso& r = recursos_[recurso];
        if (ya_tiene(r, txn, modo)) return true;
        registrar(txn, recurso, "pide", modo, "");

        bool en_espera = false;
        while (!compatible(r, txn, modo)) {
            esperando_[txn] = {recurso, modo};
            if (!en_espera) {
                registrar(txn, recurso, "espera", modo, "bloqueada por " + lista(bloqueadores(r, txn, modo)));
                en_espera = true;
            }
            std::string ciclo;
            const int victima = buscar_deadlock(txn, ciclo);
            if (victima >= 0) {
                victimas_.insert(victima);
                registrar(victima, esperando_[victima].first, "deadlock", esperando_[victima].second,
                          "ciclo " + ciclo + "; victima T" + std::to_string(victima) + " por ser la mas joven");
                cv_.notify_all();
            }
            if (victimas_.count(txn)) {
                esperando_.erase(txn);
                registrar(txn, recurso, "aborta", modo, "elegida como victima del deadlock");
                return false;
            }
            cv_.wait(guardia);
            if (victimas_.count(txn)) {
                esperando_.erase(txn);
                registrar(txn, recurso, "aborta", modo, "elegida como victima del deadlock");
                return false;
            }
        }
        esperando_.erase(txn);
        if (modo == ModoLock::EXCLUSIVO) {
            r.compartidos.erase(txn);
            r.exclusivo = txn;
        } else {
            r.compartidos.insert(txn);
        }
        registrar(txn, recurso, "concede", modo, en_espera ? "tras esperar" : "");
        return true;
    }

    void liberar_todo(int txn) {
        std::lock_guard<std::mutex> guardia(m_);
        for (auto& [nombre, r] : recursos_) {
            if (r.exclusivo == txn || r.compartidos.count(txn)) {
                registrar(txn, nombre, "libera", r.exclusivo == txn ? ModoLock::EXCLUSIVO : ModoLock::COMPARTIDO, "");
            }
            if (r.exclusivo == txn) r.exclusivo = -1;
            r.compartidos.erase(txn);
        }
        esperando_.erase(txn);
        victimas_.erase(txn);
        cv_.notify_all();
    }

    std::mutex& latch() { return latch_; }

    std::vector<EventoLock> eventos() const {
        std::lock_guard<std::mutex> guardia(m_);
        return eventos_;
    }

    std::string eventos_json() const {
        std::lock_guard<std::mutex> guardia(m_);
        std::string s = "[";
        for (std::size_t i = 0; i < eventos_.size(); ++i) {
            const EventoLock& e = eventos_[i];
            char ms[32];
            std::snprintf(ms, sizeof(ms), "%.3f", e.ms);
            s += std::string(i ? ",\n " : "\n ") + "{\"txn\":" + std::to_string(e.txn) + ",\"recurso\":\"" + e.recurso +
                 "\",\"accion\":\"" + e.accion + "\",\"modo\":\"" + e.modo + "\",\"detalle\":\"" + e.detalle +
                 "\",\"ms\":" + ms + "}";
        }
        return s + "\n]\n";
    }

private:
    struct Recurso {
        std::set<int> compartidos;
        int exclusivo = -1;
    };

    static bool ya_tiene(const Recurso& r, int txn, ModoLock modo) {
        if (r.exclusivo == txn) return true;
        return modo == ModoLock::COMPARTIDO && r.compartidos.count(txn);
    }

    static bool compatible(const Recurso& r, int txn, ModoLock modo) {
        if (r.exclusivo != -1 && r.exclusivo != txn) return false;
        if (modo == ModoLock::COMPARTIDO) return true;
        for (int otra : r.compartidos) if (otra != txn) return false;
        return true;
    }

    static std::set<int> bloqueadores(const Recurso& r, int txn, ModoLock modo) {
        std::set<int> salida;
        if (r.exclusivo != -1 && r.exclusivo != txn) salida.insert(r.exclusivo);
        if (modo == ModoLock::EXCLUSIVO) {
            for (int otra : r.compartidos) if (otra != txn) salida.insert(otra);
        }
        return salida;
    }

    static std::string lista(const std::set<int>& txns) {
        std::string s;
        for (int t : txns) s += (s.empty() ? "T" : ", T") + std::to_string(t);
        return s;
    }

    // busca un ciclo en el grafo de espera que pase por inicio; devuelve la victima o -1
    int buscar_deadlock(int inicio, std::string& ciclo) const {
        std::vector<int> camino;
        std::set<int> visitadas;
        std::function<bool(int)> dfs = [&](int txn) -> bool {
            camino.push_back(txn);
            const auto espera = esperando_.find(txn);
            if (espera != esperando_.end()) {
                const auto recurso = recursos_.find(espera->second.first);
                for (int siguiente : bloqueadores(recurso->second, txn, espera->second.second)) {
                    if (siguiente == inicio) return true;
                    if (visitadas.insert(siguiente).second && dfs(siguiente)) return true;
                }
            }
            camino.pop_back();
            return false;
        };
        if (!dfs(inicio)) return -1;
        int victima = inicio;
        for (int t : camino) {
            victima = std::max(victima, t);
            ciclo += "T" + std::to_string(t) + " -> ";
        }
        ciclo += "T" + std::to_string(inicio);
        return victima;
    }

    void registrar(int txn, const std::string& recurso, const std::string& accion, ModoLock modo, const std::string& detalle) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio_).count();
        eventos_.push_back({txn, recurso, accion, modo == ModoLock::EXCLUSIVO ? "X" : "S", detalle, ms});
    }

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::mutex latch_;
    std::map<std::string, Recurso> recursos_;
    std::map<int, std::pair<std::string, ModoLock>> esperando_;
    std::set<int> victimas_;
    std::vector<EventoLock> eventos_;
    std::chrono::steady_clock::time_point inicio_;
};

}  // namespace transacciones
}  // namespace motor
