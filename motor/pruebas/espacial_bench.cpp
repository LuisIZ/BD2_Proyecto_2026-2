// Comparación experimental de la Parte 2 (sección 2.2.4): búsqueda secuencial frente
// al R-Tree, a través del motor SQL. Para cada n se carga una tabla HEAP con una columna
// POINT, se miden 100 consultas por caso sin índice y luego con CREATE INDEX ... USING RTREE.
//
//   espacial_bench [--salida datos/resultados/espacial_bench.csv] [--consultas 100]
//                  [--metrica euclidiana|haversine]
//
// Los puntos salen de un generador congruencial con semilla 42, el mismo que usa
// datos/resultados/gist_bench.sql, así PostgreSQL mide exactamente los mismos puntos.
// Las consultas usan la métrica euclidiana porque es la que calcula GiST con el tipo point.

#include "../consultas/catalogo.h"
#include "../consultas/ejecutor.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using motor::sql::Catalogo;
using motor::sql::Ejecutor;
using motor::sql::Resultado;
using motor::sql::texto_microgrados;

namespace {

struct PuntoBench {
    long long lat_e6;
    long long lon_e6;
};

std::vector<PuntoBench> generar(int n) {
    std::uint64_t x = 42;
    auto siguiente = [&x]() {
        x = (x * 1103515245ULL + 12345ULL) % 2147483648ULL;
        return static_cast<long long>(x);
    };
    std::vector<PuntoBench> puntos(n);
    for (PuntoBench& p : puntos) {
        p.lat_e6 = -12300000 + siguiente() % 500000;
        p.lon_e6 = -77200000 + siguiente() % 400000;
    }
    return puntos;
}

std::string punto_sql(const PuntoBench& p) {
    return "POINT(" + texto_microgrados(p.lat_e6) + ", " + texto_microgrados(p.lon_e6) + ")";
}

long long paginas_de(const Resultado& r) {
    for (const auto& paso : r.plan) {
        if (paso.paginas_leidas >= 0 && (paso.operacion == "scan_completo" || paso.operacion.find("espacial") != std::string::npos)) {
            return paso.paginas_leidas;
        }
    }
    return -1;
}

struct Medida {
    double ms = 0;
    double filas = 0;
    double paginas = 0;
};

Medida medir(Ejecutor& e, const std::vector<std::string>& consultas) {
    Medida m;
    for (const std::string& sql : consultas) {
        const Resultado r = e.ejecutar(sql);
        m.ms += r.tiempo_ms;
        m.filas += static_cast<double>(r.filas.size());
        m.paginas += static_cast<double>(paginas_de(r));
    }
    const double q = static_cast<double>(consultas.size());
    return {m.ms / q, m.filas / q, m.paginas / q};
}

}  // namespace

int main(int argc, char** argv) {
    std::string salida = "datos/resultados/espacial_bench.csv";
    std::string metrica = "euclidiana";
    int num_consultas = 100;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--salida") salida = argv[++i];
        else if (a == "--consultas") num_consultas = std::atoi(argv[++i]);
        else if (a == "--metrica") metrica = argv[++i];
    }
    if (metrica != "euclidiana" && metrica != "haversine") {
        throw std::invalid_argument("--metrica debe ser euclidiana o haversine");
    }
    if (num_consultas <= 0) throw std::invalid_argument("--consultas debe ser mayor que cero");

    std::ofstream csv(salida);
    if (!csv) throw std::runtime_error("no se pudo abrir el archivo de salida: " + salida);
    csv << "estructura,n,operacion,parametro,tiempo_ms,filas,paginas,bytes\n";

    for (const int n : {1000, 10000, 100000}) {
        const std::vector<PuntoBench> puntos = generar(n);
        long long suma_lat = 0, suma_lon = 0;
        for (const PuntoBench& p : puntos) {
            suma_lat += p.lat_e6;
            suma_lon += p.lon_e6;
        }

        const std::string db = ".build/db_espacial_bench";
        std::error_code ec;
        std::filesystem::remove_all(db, ec);
        const std::string archivo = ".build/espacial_bench_puntos.csv";
        {
            std::ofstream f(archivo);
            f << "id,p\n";
            for (int i = 0; i < n; ++i) {
                f << i + 1 << ',' << texto_microgrados(puntos[i].lat_e6) << ' ' << texto_microgrados(puntos[i].lon_e6) << '\n';
            }
        }
        Catalogo catalogo(db);
        Ejecutor e(catalogo);
        e.ejecutar("CREATE TABLE puntos (id INT PRIMARY KEY, p POINT) USING HEAP");
        e.ejecutar("COPY puntos FROM FILE '" + archivo + "'");

        std::vector<PuntoBench> centros;
        for (int j = 0; j < num_consultas; ++j) centros.push_back(puntos[(static_cast<long long>(j) * 7919) % n]);

        std::vector<std::pair<std::string, std::vector<std::string>>> casos;
        for (const int radio : {1000, 5000, 10000}) {
            std::vector<std::string> sql;
            for (const PuntoBench& c : centros) {
                sql.push_back("SELECT id FROM puntos WHERE distancia(p, " + punto_sql(c) + ", '" + metrica + "') < " + std::to_string(radio));
            }
            casos.push_back({"rango," + std::to_string(radio), sql});
        }
        for (const int k : {10, 50, 100}) {
            std::vector<std::string> sql;
            for (const PuntoBench& c : centros) {
                sql.push_back("SELECT id FROM puntos ORDER BY distancia(p, " + punto_sql(c) + ", '" + metrica + "') LIMIT " + std::to_string(k));
            }
            casos.push_back({"knn," + std::to_string(k), sql});
        }

        std::vector<double> filas_secuencial;
        for (const auto& [caso, sql] : casos) {
            const Medida m = medir(e, sql);
            filas_secuencial.push_back(m.filas);
            csv << "secuencial," << n << ',' << caso << ',' << m.ms << ',' << m.filas << ',' << m.paginas << ",0\n";
        }

        const Resultado indice = e.ejecutar("CREATE INDEX idx_p ON puntos (p) USING RTREE");
        const auto bytes = std::filesystem::file_size(catalogo.tabla("puntos").indices.front().archivo);
        csv << "rtree," << n << ",construccion,0," << indice.tiempo_ms << ',' << n << ",0," << bytes << '\n';

        for (std::size_t c = 0; c < casos.size(); ++c) {
            const Medida m = medir(e, casos[c].second);
            if (m.filas != filas_secuencial[c]) throw std::runtime_error("el R-Tree devolvio otras filas en " + casos[c].first);
            csv << "rtree," << n << ',' << casos[c].first << ',' << m.ms << ',' << m.filas << ',' << m.paginas << ',' << bytes << '\n';
        }
        std::cout << "n = " << n << ": suma de lat " << suma_lat << ", suma de lon " << suma_lon << "; R-Tree de "
                  << bytes / 1024 << " KB construido en " << indice.tiempo_ms << " ms\n";
    }
    std::cout << "resultados en " << salida << "\n";
    return 0;
}
