#pragma once

#include "catalogo.h"
#include "parser_sql.h"
#include "../transacciones/gestor_locks.h"

#include <string>
#include <utility>
#include <vector>

namespace motor {
namespace sql {

// Un nodo del plan. Los dos primeros campos son los de siempre; el resto da el
// formato de PostgreSQL: nombre del nodo, sobre qué relación e índice trabaja,
// qué columna filtra, y los costos estimados frente a los reales.
//
// El plan se arma en orden de ejecución y al final se invierte, así que `nivel`
// crece hacia abajo: el nodo 0 es la raíz (lo último que corre) y el más
// profundo es el acceso a disco, igual que en EXPLAIN.
struct PasoPlan {
    PasoPlan() = default;
    PasoPlan(std::string operacion_, std::vector<std::pair<std::string, std::string>> detalles_ = {})
        : operacion(std::move(operacion_)), detalles(std::move(detalles_)) {}

    std::string operacion;  // id interno: scan_completo, busqueda_por_indice, filtro...
    std::vector<std::pair<std::string, std::string>> detalles;

    std::string nodo;       // etiqueta visible: "Seq Scan", "Index Scan", "Sort"...
    std::string relacion;   // tabla sobre la que opera
    std::string indice;     // índice usado, vacío si no hay
    std::string columna;    // columna sobre la que se aplica el índice o el filtro
    std::string condicion;  // "Index = 500"
    int nivel = 0;

    // estimaciones del planificador (-1 = no aplica)
    double costo = -1;               // páginas que espera leer
    long long filas_estimadas = -1;

    // medidas reales, solo con EXPLAIN ANALYZE o al ejecutar de verdad
    long long filas_reales = -1;
    double tiempo_ms = -1;
    long long paginas_leidas = -1;
    long long paginas_escritas = -1;
};

struct Resultado {
    std::string tipo;  // select | insert | delete | create_table | create_index | drop_table | show_tables | describe | explain
    std::vector<std::string> columnas;
    std::vector<Fila> filas;
    std::vector<PasoPlan> plan;
    std::string mensaje;
    std::size_t afectadas = 0;
    double tiempo_ms = 0.0;
    double planificacion_ms = 0.0;  // EXPLAIN: tiempo de planificar, aparte del de ejecutar
    double ejecucion_ms = 0.0;
    bool analizado = false;         // el plan trae medidas reales
};

// Ejecuta sentencias contra las tablas del catálogo. Abre los archivos de la
// tabla al empezar cada sentencia y los cierra al terminar, así todo queda en
// disco entre llamadas. Los errores se lanzan como std::runtime_error.
class Ejecutor {
public:
    explicit Ejecutor(Catalogo& catalogo) : catalogo_(catalogo) {}

    Resultado ejecutar(const std::string& sql);
    Resultado ejecutar(const Sentencia& sentencia);

    // comparte un gestor de locks entre varios ejecutores (uno por hilo)
    void usar_locks(transacciones::GestorLocks* gestor, int txn);
    bool en_transaccion() const { return en_transaccion_; }

private:
    struct Deshacer {
        std::string tabla;
        bool fue_insert = false;
        Fila fila;
    };

    Resultado ejecutar_sentencia(const Sentencia& s);
    Resultado iniciar_transaccion();
    Resultado terminar_transaccion(bool confirmar);
    void deshacer_cambios();
    Resultado explicar(const Sentencia& s);
    Resultado crear_tabla(const Sentencia& s);
    Resultado crear_tabla_desde_csv(const Sentencia& s);
    Resultado copiar_desde_csv(const Sentencia& s);
    Resultado crear_indice(const Sentencia& s);
    Resultado borrar_tabla(const Sentencia& s);
    Resultado insertar(const Sentencia& s);
    Resultado eliminar(const Sentencia& s);
    Resultado seleccionar(const Sentencia& s);
    Resultado mostrar_tablas();
    Resultado describir(const Sentencia& s);

    Catalogo& catalogo_;
    double parseo_ms_ = 0.0;

    transacciones::GestorLocks locks_propios_;
    transacciones::GestorLocks* locks_ = &locks_propios_;
    int txn_ = 1;
    bool en_transaccion_ = false;
    int profundidad_ = 0;
    std::vector<Deshacer> deshacer_;
};

}  // namespace sql
}  // namespace motor
