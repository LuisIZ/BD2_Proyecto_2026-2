#include "../archivos/heap_file.h"
#include "cargador_csv.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

const std::string kCsv = "datos/organizations-100000.csv";
const std::string kHeap = ".build/escala.heap";
constexpr std::size_t kTotal = 100000;

using Reloj = std::chrono::steady_clock;

long long microsegundos(const Reloj::time_point& inicio, const Reloj::time_point& fin) {
    return std::chrono::duration_cast<std::chrono::microseconds>(fin - inicio).count();
}

void prueba_cargador_csv() {
    const std::string simple =
        "1,8cC6B5992C0309c,Acevedo LLC,https://www.donovan.com/,"
        "Holy See (Vatican City State),Multi-channeled bottom-line core,2019,"
        "Graphic Design / Web Design,7070";
    assert(motor::pruebas::contar_campos_csv(simple) == 9);
    assert(motor::pruebas::dividir_campos_csv(simple).size() == 9);

    const std::string entrecomillado =
        "4,8Dd7beDa37FbeD0,\"Byrd, Patterson and Knox\",https://www.james-velez.net/,"
        "Netherlands,Pre-emptive national function,1982,Furniture,3494";
    assert(motor::pruebas::contar_campos_csv(entrecomillado) == 9);
    const std::vector<std::string> campos = motor::pruebas::dividir_campos_csv(entrecomillado);
    assert(campos.size() == 9);
    assert(campos[2] == "Byrd, Patterson and Knox");

    const motor::pruebas::OpcionesCarga opciones_prefijo{5, false, 42};
    const std::vector<motor::Registro> primeros =
        motor::pruebas::cargar_csv(kCsv, opciones_prefijo);
    assert(primeros.size() == 5);
    for (std::size_t indice = 0; indice < primeros.size(); ++indice) {
        assert(primeros[indice].clave == static_cast<int>(indice) + 1);
        assert(!primeros[indice].valor.empty());
    }

    const motor::pruebas::OpcionesCarga opciones_barajado{100, true, 42};
    const std::vector<motor::Registro> barajados =
        motor::pruebas::cargar_csv(kCsv, opciones_barajado);
    assert(barajados.size() == 100);
    bool desordenado = false;
    for (std::size_t indice = 1; indice < barajados.size(); ++indice) {
        if (barajados[indice].clave < barajados[indice - 1].clave) {
            desordenado = true;
            break;
        }
    }
    assert(desordenado);

    std::cout << "cargador_csv: comillas respetadas (un split ingenuo daria 10 campos),"
              << " prefijo por N y barajado reproducible\n";
}

}  // namespace

int main() {
    prueba_cargador_csv();

    const Reloj::time_point inicio_carga = Reloj::now();
    const motor::pruebas::OpcionesCarga opciones{kTotal, false, 42};
    const std::vector<motor::Registro> registros = motor::pruebas::cargar_csv(kCsv, opciones);
    const long long tiempo_carga = microsegundos(inicio_carga, Reloj::now());

    assert(registros.size() == kTotal);
    std::size_t bytes_valor = 0;
    for (std::size_t indice = 0; indice < registros.size(); ++indice) {
        assert(registros[indice].clave == static_cast<int>(indice) + 1);
        bytes_valor += registros[indice].valor.size();
    }
    std::cout << "carga: " << registros.size() << " registros en " << tiempo_carga / 1000
              << " ms, valor medio " << bytes_valor / registros.size() << " B\n";

    motor::HeapFile archivo(kHeap, true);
    archivo.reiniciar_contadores();

    std::vector<motor::RecordId> rids;
    rids.reserve(kTotal);
    const Reloj::time_point inicio_insercion = Reloj::now();
    for (const motor::Registro& registro : registros) {
        const std::vector<std::byte> bytes = motor::codificar_registro(registro);
        rids.push_back(
            archivo.insertar_bytes(bytes.data(), static_cast<std::uint16_t>(bytes.size())));
    }
    const long long tiempo_insercion = microsegundos(inicio_insercion, Reloj::now());

    const motor::EstadisticasHeap tras_insercion = archivo.stats_heap();
    std::cout << "insercion: " << tiempo_insercion / 1000 << " ms | " << tras_insercion << "\n";
    std::cout << "           paginas_leidas=" << archivo.paginas_leidas()
              << " paginas_escritas=" << archivo.paginas_escritas() << "\n";

    assert(tras_insercion.registros_vivos == kTotal);
    assert(tras_insercion.tumbas == 0);
    assert(tras_insercion.tamano_archivo_bytes ==
           (tras_insercion.num_paginas + 1) * motor::PAGE_SIZE);

    const std::size_t estimadas = (kTotal + 27) / 28;
    const double desvio =
        static_cast<double>(tras_insercion.num_paginas) / static_cast<double>(estimadas);
    std::cout << "           paginas=" << tras_insercion.num_paginas << " estimadas=" << estimadas
              << " (" << desvio * 100.0 << "% de la estimacion)\n";
    assert(desvio > 0.90 && desvio < 1.10);

    archivo.reiniciar_contadores();
    const Reloj::time_point inicio_scan = Reloj::now();
    std::size_t vistos = 0;
    archivo.recorrer([&](const motor::RecordId&, const std::byte*, std::uint16_t) {
        ++vistos;
        return true;
    });
    const long long tiempo_scan = microsegundos(inicio_scan, Reloj::now());
    assert(vistos == kTotal);
    assert(archivo.paginas_leidas() == tras_insercion.num_paginas);
    std::cout << "scan completo: " << vistos << " registros en " << tiempo_scan / 1000 << " ms, "
              << archivo.paginas_leidas() << " lecturas de pagina\n";

    std::mt19937 generador(2026);
    std::uniform_int_distribution<std::size_t> sorteo(0, kTotal - 1);
    const Reloj::time_point inicio_obtener = Reloj::now();
    for (int intento = 0; intento < 1000; ++intento) {
        const std::size_t indice = sorteo(generador);
        const std::optional<std::vector<std::byte>> bytes = archivo.obtener(rids[indice]);
        assert(bytes.has_value());
        motor::Registro recuperado;
        assert(motor::decodificar_registro(bytes->data(),
                                           static_cast<std::uint16_t>(bytes->size()),
                                           recuperado));
        assert(recuperado.clave == registros[indice].clave);
        assert(recuperado.valor == registros[indice].valor);
    }
    const long long tiempo_obtener = microsegundos(inicio_obtener, Reloj::now());
    std::cout << "obtener aleatorio: 1000 accesos en " << tiempo_obtener << " us ("
              << tiempo_obtener / 1000.0 << " us por acceso)\n";

    archivo.reiniciar_contadores();
    const Reloj::time_point inicio_busqueda = Reloj::now();
    for (int intento = 0; intento < 20; ++intento) {
        const int clave = static_cast<int>(sorteo(generador)) + 1;
        const std::optional<motor::Registro> hallado = archivo.buscar(clave);
        assert(hallado.has_value());
        assert(hallado->clave == clave);
    }
    const long long tiempo_busqueda = microsegundos(inicio_busqueda, Reloj::now());
    std::cout << "busqueda por clave: 20 claves en " << tiempo_busqueda / 1000 << " ms ("
              << tiempo_busqueda / 20 << " us por clave, " << archivo.paginas_leidas() / 20
              << " paginas leidas por clave)\n";

    std::size_t borrados = 0;
    const Reloj::time_point inicio_borrado = Reloj::now();
    for (std::size_t indice = 0; indice < kTotal; indice += 3) {
        assert(archivo.eliminar_rid(rids[indice]));
        ++borrados;
    }
    const long long tiempo_borrado = microsegundos(inicio_borrado, Reloj::now());

    const motor::EstadisticasHeap tras_borrado = archivo.stats_heap();
    std::cout << "borrado masivo: " << borrados << " por RID en " << tiempo_borrado / 1000
              << " ms | " << tras_borrado << "\n";
    assert(tras_borrado.tumbas == borrados);
    assert(tras_borrado.registros_vivos == kTotal - borrados);
    assert(tras_borrado.num_paginas == tras_insercion.num_paginas);

    vistos = 0;
    archivo.recorrer([&](const motor::RecordId&, const std::byte*, std::uint16_t) {
        ++vistos;
        return true;
    });
    assert(vistos == kTotal - borrados);
    std::cout << "scan tras borrado: " << vistos << " vivos, ninguna tumba emitida\n";

    const std::size_t paginas_previas = tras_borrado.num_paginas;
    const std::size_t tamano_previo = tras_borrado.tamano_archivo_bytes;

    {
        motor::HeapFile reabierto(kHeap, false);
        const motor::EstadisticasHeap tras_reapertura = reabierto.stats_heap();
        assert(tras_reapertura.num_paginas == paginas_previas);
        assert(tras_reapertura.tamano_archivo_bytes == tamano_previo);
        assert(tras_reapertura.registros_vivos == kTotal - borrados);
        assert(tras_reapertura.tumbas == borrados);
        assert(tras_reapertura.bytes_desperdiciados == tras_borrado.bytes_desperdiciados);

        const Reloj::time_point inicio_verificacion = Reloj::now();
        assert(reabierto.verificar_mapa());
        const long long tiempo_verificacion = microsegundos(inicio_verificacion, Reloj::now());
        assert(reabierto.verificar_cadena());

        assert(!reabierto.obtener(rids[0]).has_value());
        assert(reabierto.obtener(rids[1]).has_value());
        std::cout << "reapertura: mapa reconstruido y verificado en " << tiempo_verificacion / 1000
                  << " ms sobre " << paginas_previas << " paginas\n";
    }

    std::cout << "Prueba completada correctamente.\n";
}
