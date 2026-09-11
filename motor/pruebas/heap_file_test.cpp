#include "../archivos/heap_file.h"
#include "../archivos/serializacion.h"

#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const std::string kRuta = ".build/prueba_heap.heap";

std::vector<std::byte> relleno(std::uint16_t largo, unsigned char semilla) {
    std::vector<std::byte> datos(largo);
    for (std::uint16_t indice = 0; indice < largo; ++indice) {
        datos[indice] = static_cast<std::byte>((semilla + indice) & 0xFF);
    }
    return datos;
}

motor::RecordId insertar(motor::HeapFile& archivo, const std::vector<std::byte>& datos) {
    return archivo.insertar_bytes(datos.data(), static_cast<std::uint16_t>(datos.size()));
}

void prueba_archivo_vacio() {
    motor::HeapFile archivo(kRuta, true);
    assert(archivo.num_paginas() == 0);
    assert(archivo.primera_pagina() == motor::PAGINA_INVALIDA);
    assert(archivo.ultima_pagina() == motor::PAGINA_INVALIDA);
    assert(archivo.tamano_en_disco() == motor::PAGE_SIZE);
    assert(archivo.verificar_cadena());
    std::cout << "archivo_vacio: paginas=0 tamano_en_disco=" << archivo.tamano_en_disco() << "\n";
}

void prueba_ida_y_vuelta() {
    motor::HeapFile archivo(kRuta, true);

    const std::vector<std::byte> uno = relleno(150, 1);
    const std::vector<std::byte> dos = relleno(37, 2);
    const std::vector<std::byte> vacio = relleno(0, 0);

    const motor::RecordId rid_uno = insertar(archivo, uno);
    const motor::RecordId rid_dos = insertar(archivo, dos);
    const motor::RecordId rid_vacio = insertar(archivo, vacio);

    assert(rid_uno.page_id == 0 && rid_uno.slot_id == 0);
    assert(rid_dos.slot_id == 1 && rid_vacio.slot_id == 2);
    assert(archivo.num_paginas() == 1);

    assert(archivo.obtener(rid_uno).value() == uno);
    assert(archivo.obtener(rid_dos).value() == dos);
    assert(archivo.obtener(rid_vacio).value().empty());
    assert(archivo.verificar_cadena());
    std::cout << "ida_y_vuelta: 3 registros en 1 pagina, contenido identico\n";
}

void prueba_varias_paginas() {
    motor::HeapFile archivo(kRuta, true);

    std::vector<motor::RecordId> rids;
    for (int indice = 0; indice < 200; ++indice) {
        rids.push_back(insertar(archivo, relleno(200, static_cast<unsigned char>(indice))));
    }

    assert(archivo.num_paginas() > 1);
    for (int indice = 0; indice < 200; ++indice) {
        const std::vector<std::byte> esperado = relleno(200, static_cast<unsigned char>(indice));
        assert(archivo.obtener(rids[indice]).value() == esperado);
    }
    assert(archivo.verificar_cadena());
    assert(archivo.tamano_en_disco() == (archivo.num_paginas() + 1) * motor::PAGE_SIZE);
    std::cout << "varias_paginas: 200 registros de 200 B en " << archivo.num_paginas()
              << " paginas, cadena coherente\n";
}

void prueba_rid_estable() {
    motor::HeapFile archivo(kRuta, true);

    const std::vector<std::byte> primero = relleno(120, 7);
    const motor::RecordId rid_primero = insertar(archivo, primero);

    for (int indice = 0; indice < 500; ++indice) {
        insertar(archivo, relleno(180, static_cast<unsigned char>(indice)));
    }

    assert(archivo.obtener(rid_primero).value() == primero);
    std::cout << "rid_estable: RID sigue resolviendo tras 500 inserciones y "
              << archivo.num_paginas() << " paginas\n";
}

void prueba_slot_no_reasignado() {
    motor::HeapFile archivo(kRuta, true);

    std::vector<motor::RecordId> rids;
    for (int indice = 0; indice < 10; ++indice) {
        rids.push_back(insertar(archivo, relleno(100, static_cast<unsigned char>(indice))));
    }

    const motor::RecordId rid_borrado = rids[5];
    assert(archivo.eliminar_rid(rid_borrado));
    assert(!archivo.obtener(rid_borrado).has_value());

    const motor::RecordId rid_nuevo = insertar(archivo, relleno(100, 99));
    assert(rid_nuevo != rid_borrado);
    assert(rid_nuevo.slot_id == 10);
    assert(!archivo.obtener(rid_borrado).has_value());

    for (int indice = 0; indice < 10; ++indice) {
        if (indice == 5) {
            continue;
        }
        const std::vector<std::byte> esperado = relleno(100, static_cast<unsigned char>(indice));
        assert(archivo.obtener(rids[indice]).value() == esperado);
    }
    std::cout << "slot_no_reasignado: la tumba conserva el slot 5, el nuevo registro usa el 10\n";
}

void prueba_doble_eliminar_rid() {
    motor::HeapFile archivo(kRuta, true);

    const motor::RecordId rid = insertar(archivo, relleno(64, 3));
    assert(archivo.eliminar_rid(rid));
    assert(!archivo.eliminar_rid(rid));
    assert(!archivo.obtener(rid).has_value());
    std::cout << "doble_eliminar_rid: segunda llamada devuelve false, sin corrupcion\n";
}

void prueba_rid_invalido() {
    motor::HeapFile archivo(kRuta, true);
    const motor::RecordId rid = insertar(archivo, relleno(50, 1));

    motor::RecordId pagina_fuera;
    pagina_fuera.page_id = 999;
    pagina_fuera.slot_id = 0;
    assert(!archivo.obtener(pagina_fuera).has_value());
    assert(!archivo.eliminar_rid(pagina_fuera));

    motor::RecordId slot_fuera;
    slot_fuera.page_id = rid.page_id;
    slot_fuera.slot_id = 999;
    assert(!archivo.obtener(slot_fuera).has_value());
    assert(!archivo.eliminar_rid(slot_fuera));

    const motor::RecordId por_defecto;
    assert(!archivo.obtener(por_defecto).has_value());
    assert(!archivo.eliminar_rid(por_defecto));

    assert(archivo.obtener(rid).has_value());
    std::cout << "rid_invalido: page_id fuera de rango, slot_id fuera de rango y RID por defecto\n";
}

void prueba_registro_mayor_que_pagina() {
    motor::HeapFile archivo(kRuta, true);

    const std::vector<std::byte> gigante = relleno(motor::CAPACIDAD_MAXIMA_REGISTRO + 1, 1);
    bool lanzo = false;
    try {
        insertar(archivo, gigante);
    } catch (const std::invalid_argument&) {
        lanzo = true;
    }
    assert(lanzo);
    assert(archivo.num_paginas() == 0);

    const std::vector<std::byte> maximo = relleno(motor::CAPACIDAD_MAXIMA_REGISTRO, 2);
    const motor::RecordId rid = insertar(archivo, maximo);
    assert(archivo.obtener(rid).value() == maximo);
    std::cout << "registro_mayor_que_pagina: " << motor::CAPACIDAD_MAXIMA_REGISTRO + 1
              << " lanza invalid_argument; el archivo no crecio\n";
}

void prueba_reapertura() {
    std::vector<motor::RecordId> rids;
    std::uint32_t paginas_previas = 0;
    std::size_t tamano_previo = 0;

    {
        motor::HeapFile archivo(kRuta, true);
        for (int indice = 0; indice < 150; ++indice) {
            rids.push_back(insertar(archivo, relleno(160, static_cast<unsigned char>(indice))));
        }
        assert(archivo.eliminar_rid(rids[10]));
        assert(archivo.eliminar_rid(rids[20]));
        paginas_previas = archivo.num_paginas();
        tamano_previo = archivo.tamano_en_disco();
    }

    motor::HeapFile archivo(kRuta, false);
    assert(archivo.num_paginas() == paginas_previas);
    assert(archivo.tamano_en_disco() == tamano_previo);
    assert(archivo.primera_pagina() == 0);
    assert(archivo.ultima_pagina() == paginas_previas - 1);
    assert(archivo.verificar_cadena());
    assert(archivo.verificar_mapa());

    for (int indice = 0; indice < 150; ++indice) {
        if (indice == 10 || indice == 20) {
            assert(!archivo.obtener(rids[indice]).has_value());
            continue;
        }
        const std::vector<std::byte> esperado = relleno(160, static_cast<unsigned char>(indice));
        assert(archivo.obtener(rids[indice]).value() == esperado);
    }

    const motor::RecordId rid_nuevo = insertar(archivo, relleno(160, 200));
    assert(archivo.obtener(rid_nuevo).has_value());
    std::cout << "reapertura: " << paginas_previas << " paginas, " << tamano_previo
              << " bytes; tumbas y datos preservados\n";
}

void prueba_verificar_mapa() {
    motor::HeapFile archivo(kRuta, true);

    std::vector<motor::RecordId> rids;
    for (int indice = 0; indice < 300; ++indice) {
        rids.push_back(insertar(archivo, relleno(static_cast<std::uint16_t>(50 + indice % 400),
                                                 static_cast<unsigned char>(indice))));
    }
    assert(archivo.verificar_mapa());

    for (std::size_t indice = 0; indice < rids.size(); indice += 3) {
        assert(archivo.eliminar_rid(rids[indice]));
    }
    assert(archivo.verificar_mapa());

    for (int indice = 0; indice < 50; ++indice) {
        insertar(archivo, relleno(120, static_cast<unsigned char>(indice)));
    }
    assert(archivo.verificar_mapa());
    std::cout << "verificar_mapa: mapa en memoria coincide con el disco tras insertar,"
              << " borrar y compactar\n";
}

void prueba_reutilizacion_demostrable() {
    const std::uint16_t largo_inicial = 200;
    const std::uint16_t largo_reinsertado = 180;
    assert(largo_reinsertado + motor::SLOT_SIZE <= largo_inicial);

    motor::HeapFile archivo(kRuta, true);

    std::vector<motor::RecordId> rids;
    for (int indice = 0; indice < 1000; ++indice) {
        rids.push_back(
            insertar(archivo, relleno(largo_inicial, static_cast<unsigned char>(indice))));
    }

    const motor::EstadisticasHeap inicial = archivo.stats_heap();
    std::cout << "reutilizacion_demostrable\n  tras 1000 inserciones: " << inicial << "\n";
    assert(inicial.num_paginas == 50);
    assert(inicial.registros_vivos == 1000);
    assert(inicial.tumbas == 0);

    for (std::size_t indice = 1; indice < rids.size(); indice += 2) {
        assert(archivo.eliminar_rid(rids[indice]));
    }

    const motor::EstadisticasHeap tras_borrado = archivo.stats_heap();
    std::cout << "  tras borrar 500:       " << tras_borrado << "\n";
    assert(tras_borrado.tumbas == 500);
    assert(tras_borrado.registros_vivos == 500);
    assert(tras_borrado.bytes_desperdiciados == 500u * largo_inicial);
    assert(tras_borrado.num_paginas == inicial.num_paginas);
    assert(tras_borrado.tamano_archivo_bytes == inicial.tamano_archivo_bytes);

    for (int indice = 0; indice < 500; ++indice) {
        insertar(archivo, relleno(largo_reinsertado, static_cast<unsigned char>(indice)));
    }

    const motor::EstadisticasHeap finales = archivo.stats_heap();
    std::cout << "  tras reinsertar 500:   " << finales << "\n";
    assert(finales.num_paginas == inicial.num_paginas);
    assert(finales.tamano_archivo_bytes == inicial.tamano_archivo_bytes);
    assert(finales.registros_vivos == 1000);
    assert(finales.tumbas == 500);
    assert(finales.bytes_desperdiciados == 0);
    assert(archivo.verificar_mapa());

    for (std::size_t indice = 0; indice < rids.size(); indice += 2) {
        const std::vector<std::byte> esperado =
            relleno(largo_inicial, static_cast<unsigned char>(indice));
        assert(archivo.obtener(rids[indice]).value() == esperado);
    }
    std::cout << "  paginas y bytes en disco IDENTICOS; supervivientes intactos tras compactar\n";
}

void prueba_reutilizacion_misma_longitud() {
    const std::uint16_t largo = 200;
    motor::HeapFile archivo(kRuta, true);

    std::vector<motor::RecordId> rids;
    for (int indice = 0; indice < 1000; ++indice) {
        rids.push_back(insertar(archivo, relleno(largo, static_cast<unsigned char>(indice))));
    }
    const motor::EstadisticasHeap inicial = archivo.stats_heap();
    assert(inicial.num_paginas == 50);

    for (std::size_t indice = 1; indice < rids.size(); indice += 2) {
        assert(archivo.eliminar_rid(rids[indice]));
    }
    for (int indice = 0; indice < 500; ++indice) {
        insertar(archivo, relleno(largo, static_cast<unsigned char>(indice)));
    }

    const motor::EstadisticasHeap finales = archivo.stats_heap();
    const std::size_t sin_reutilizacion = inicial.num_paginas + 25;

    std::cout << "reutilizacion_misma_longitud\n  " << inicial.num_paginas << " -> "
              << finales.num_paginas << " paginas (sin reutilizacion serian "
              << sin_reutilizacion << ")\n";
    assert(finales.num_paginas > inicial.num_paginas);
    assert(finales.num_paginas <= inicial.num_paginas + 3);
    assert(finales.registros_vivos == 1000);
    assert(archivo.verificar_mapa());
    std::cout << "  crece 3 paginas y no 25: el deficit es el directorio de las 500 tumbas"
              << " (500 x 4 = 2000 B)\n";
}

void prueba_codec() {
    const motor::Registro original{42, "Acevedo LLC,https://www.donovan.com/,Holy See"};
    const std::vector<std::byte> bytes = motor::codificar_registro(original);
    assert(bytes.size() == motor::CABECERA_REGISTRO + original.valor.size());

    int clave = 0;
    assert(motor::leer_clave(bytes.data(), static_cast<std::uint16_t>(bytes.size()), clave));
    assert(clave == 42);

    motor::Registro copia;
    assert(motor::decodificar_registro(bytes.data(), static_cast<std::uint16_t>(bytes.size()),
                                       copia));
    assert(copia.clave == original.clave && copia.valor == original.valor);

    const motor::Registro vacio{7, ""};
    const std::vector<std::byte> bytes_vacio = motor::codificar_registro(vacio);
    assert(bytes_vacio.size() == motor::CABECERA_REGISTRO);
    motor::Registro copia_vacio;
    assert(motor::decodificar_registro(bytes_vacio.data(), motor::CABECERA_REGISTRO, copia_vacio));
    assert(copia_vacio.clave == 7 && copia_vacio.valor.empty());

    assert(!motor::leer_clave(bytes.data(), 4, clave));
    assert(!motor::decodificar_registro(bytes.data(), 4, copia));
    assert(!motor::decodificar_registro(bytes.data(), static_cast<std::uint16_t>(bytes.size() - 1),
                                        copia));
    std::cout << "codec: ida y vuelta, valor vacio, y rechazo de longitudes inconsistentes\n";
}

void prueba_recorrer_sin_tumbas() {
    motor::HeapFile archivo(kRuta, true);

    std::vector<motor::RecordId> rids;
    for (int indice = 0; indice < 300; ++indice) {
        rids.push_back(insertar(archivo, relleno(150, static_cast<unsigned char>(indice))));
    }
    for (std::size_t indice = 0; indice < rids.size(); indice += 2) {
        assert(archivo.eliminar_rid(rids[indice]));
    }

    std::size_t vistos = 0;
    archivo.recorrer([&](const motor::RecordId&, const std::byte*, std::uint16_t largo) {
        assert(largo == 150);
        ++vistos;
        return true;
    });
    assert(vistos == 150);
    assert(vistos == archivo.stats_heap().registros_vivos);
    std::cout << "recorrer_sin_tumbas: 300 insertados, 150 borrados, recorrido ve " << vistos
              << "\n";
}

void prueba_corte_temprano() {
    motor::HeapFile archivo(kRuta, true);
    for (int indice = 0; indice < 500; ++indice) {
        insertar(archivo, relleno(200, static_cast<unsigned char>(indice)));
    }

    archivo.reiniciar_contadores();
    std::size_t vistos = 0;
    archivo.recorrer([&](const motor::RecordId&, const std::byte*, std::uint16_t) {
        ++vistos;
        return vistos < 3;
    });
    assert(vistos == 3);
    assert(archivo.paginas_leidas() == 1);
    std::cout << "corte_temprano: visitante corta en 3 registros y solo se leyo "
              << archivo.paginas_leidas() << " pagina de " << archivo.num_paginas() << "\n";
}

void prueba_adaptador_insertar_scan() {
    motor::HeapFile archivo(kRuta, true);

    for (int clave = 1; clave <= 500; ++clave) {
        assert(archivo.insertar({clave, "valor-" + std::to_string(clave)}));
    }

    const std::vector<motor::Registro> registros = archivo.scan();
    assert(registros.size() == 500);
    for (std::size_t indice = 0; indice < registros.size(); ++indice) {
        const int esperado = static_cast<int>(indice) + 1;
        assert(registros[indice].clave == esperado);
        assert(registros[indice].valor == "valor-" + std::to_string(esperado));
    }

    const motor::Registro enorme{999, std::string(motor::CAPACIDAD_MAXIMA_VALOR + 1, 'x')};
    assert(!archivo.insertar(enorme));
    assert(archivo.scan().size() == 500);
    std::cout << "adaptador_insertar_scan: 500 registros en orden de llegada; valor de "
              << motor::CAPACIDAD_MAXIMA_VALOR + 1 << " B rechazado\n";
}

void prueba_adaptador_eliminar_y_buscar() {
    motor::HeapFile archivo(kRuta, true);
    for (int clave = 1; clave <= 300; ++clave) {
        assert(archivo.insertar({clave, "valor-" + std::to_string(clave)}));
    }

    const std::optional<motor::Registro> hallado = archivo.buscar(150);
    assert(hallado.has_value());
    assert(hallado->clave == 150 && hallado->valor == "valor-150");
    assert(!archivo.buscar(9999).has_value());

    assert(archivo.eliminar(150));
    assert(!archivo.buscar(150).has_value());
    assert(!archivo.eliminar(150));
    assert(!archivo.eliminar(9999));

    const std::vector<motor::Registro> registros = archivo.scan();
    assert(registros.size() == 299);
    for (const motor::Registro& registro : registros) {
        assert(registro.clave != 150);
    }
    std::cout << "adaptador_eliminar_y_buscar: buscar por clave, borrado por clave e idempotencia"
              << "\n";
}

void prueba_adaptador_stats() {
    motor::HeapFile archivo(kRuta, true);
    for (int clave = 1; clave <= 100; ++clave) {
        assert(archivo.insertar({clave, "valor-" + std::to_string(clave)}));
    }
    for (int clave = 1; clave <= 25; ++clave) {
        assert(archivo.eliminar(clave));
    }

    const motor::EstadisticasArchivo estadisticas = archivo.stats();
    const motor::EstadisticasHeap heap = archivo.stats_heap();

    assert(estadisticas.registros == 75);
    assert(estadisticas.tumbas == 25);
    assert(estadisticas.paginas == heap.num_paginas);
    assert(estadisticas.registros_auxiliares == 0);
    assert(estadisticas.porcentaje_desperdicio > 0.24 &&
           estadisticas.porcentaje_desperdicio < 0.26);
    assert(heap.bytes_desperdiciados > 0);

    std::cout << "adaptador_stats: registros=" << estadisticas.registros
              << " tumbas=" << estadisticas.tumbas
              << " desperdicio=" << estadisticas.porcentaje_desperdicio * 100.0
              << "% (razon de registros) | bytes_desperdiciados=" << heap.bytes_desperdiciados
              << " (razon de bytes)\n";
}

void prueba_reorganizar() {
    motor::HeapFile archivo(kRuta, true);

    std::vector<motor::RecordId> rids;
    for (int indice = 0; indice < 1000; ++indice) {
        rids.push_back(insertar(archivo, relleno(200, static_cast<unsigned char>(indice))));
    }
    for (std::size_t indice = 1; indice < rids.size(); indice += 2) {
        assert(archivo.eliminar_rid(rids[indice]));
    }

    const motor::EstadisticasHeap antes = archivo.stats_heap();
    assert(antes.bytes_desperdiciados == 100000);

    archivo.reorganizar();

    const motor::EstadisticasHeap despues = archivo.stats_heap();
    assert(despues.bytes_desperdiciados == 0);
    assert(despues.num_paginas == antes.num_paginas);
    assert(despues.registros_vivos == antes.registros_vivos);
    assert(despues.tumbas == antes.tumbas);
    assert(despues.tamano_archivo_bytes == antes.tamano_archivo_bytes);
    assert(archivo.verificar_mapa());

    for (std::size_t indice = 0; indice < rids.size(); indice += 2) {
        const std::vector<std::byte> esperado = relleno(200, static_cast<unsigned char>(indice));
        assert(archivo.obtener(rids[indice]).value() == esperado);
    }

    const motor::EstadisticasArchivo estadisticas = archivo.stats();
    std::cout << "reorganizar: " << antes.bytes_desperdiciados << " B recuperados en "
              << estadisticas.ultima_reorganizacion_us << " us; " << despues.num_paginas
              << " paginas intactas y los 500 RIDs vivos siguen resolviendo\n";
}

void prueba_tamanos_variados() {
    motor::HeapFile archivo(kRuta, true);
    std::mt19937 generador(1234);
    std::uniform_int_distribution<int> distribucion(10, 2000);

    std::vector<motor::RecordId> rids;
    std::vector<std::uint16_t> largos;
    for (int indice = 0; indice < 2000; ++indice) {
        const std::uint16_t largo = static_cast<std::uint16_t>(distribucion(generador));
        rids.push_back(insertar(archivo, relleno(largo, static_cast<unsigned char>(indice))));
        largos.push_back(largo);
    }

    assert(archivo.num_paginas() > 1);
    for (int indice = 0; indice < 2000; ++indice) {
        const std::optional<std::vector<std::byte>> obtenido = archivo.obtener(rids[indice]);
        assert(obtenido.has_value());
        assert(obtenido->size() == largos[indice]);
        assert(*obtenido == relleno(largos[indice], static_cast<unsigned char>(indice)));
    }
    assert(archivo.verificar_mapa());
    assert(archivo.verificar_cadena());
    std::cout << "tamanos_variados: 2000 registros de 10 a 2000 B en " << archivo.num_paginas()
              << " paginas, todos recuperables\n";
}

void prueba_pagina_llena_fuerza_nueva() {
    motor::HeapFile archivo(kRuta, true);

    for (int indice = 0; indice < 39; ++indice) {
        insertar(archivo, relleno(100, static_cast<unsigned char>(indice)));
    }
    const motor::RecordId ultimo_de_la_pagina = insertar(archivo, relleno(24, 5));
    assert(ultimo_de_la_pagina.page_id == 0);
    assert(archivo.num_paginas() == 1);
    assert(archivo.stats_heap().bytes_libres == 0);

    const motor::RecordId desborde = insertar(archivo, relleno(1, 7));
    assert(desborde.page_id == 1);
    assert(archivo.num_paginas() == 2);
    assert(archivo.verificar_cadena());
    assert(archivo.verificar_mapa());
    std::cout << "pagina_llena_fuerza_nueva: pagina 0 con 0 B libres; el registro de 1 B abre"
              << " la pagina 1\n";
}

void escribir_cabecera_corrupta(const std::string& ruta, std::uint32_t magic,
                                std::uint32_t page_size) {
    std::array<std::byte, motor::PAGE_SIZE> cabecera{};
    motor::escribir_campo<std::uint32_t>(cabecera.data(), 0, magic);
    motor::escribir_campo<std::uint16_t>(cabecera.data(), 4, motor::VERSION_HEAP);
    motor::escribir_campo<std::uint32_t>(cabecera.data(), 8, page_size);
    motor::escribir_campo<std::uint32_t>(cabecera.data(), 12, 0);
    motor::escribir_campo<std::uint32_t>(cabecera.data(), 16, motor::PAGINA_INVALIDA);
    motor::escribir_campo<std::uint32_t>(cabecera.data(), 20, motor::PAGINA_INVALIDA);

    std::ofstream salida(ruta, std::ios::binary | std::ios::trunc);
    salida.write(reinterpret_cast<const char*>(cabecera.data()), motor::PAGE_SIZE);
}

void prueba_cabecera_invalida() {
    const std::string ruta_magic = ".build/prueba_magic.heap";
    escribir_cabecera_corrupta(ruta_magic, 0xDEADBEEFu,
                               static_cast<std::uint32_t>(motor::PAGE_SIZE));
    bool lanzo_magic = false;
    try {
        motor::HeapFile archivo(ruta_magic, false);
    } catch (const std::invalid_argument&) {
        lanzo_magic = true;
    }
    assert(lanzo_magic);

    const std::string ruta_tamano = ".build/prueba_tamano.heap";
    escribir_cabecera_corrupta(ruta_tamano, motor::MAGIC_HEAP, 512);
    bool lanzo_tamano = false;
    try {
        motor::HeapFile archivo(ruta_tamano, false);
    } catch (const std::invalid_argument&) {
        lanzo_tamano = true;
    }
    assert(lanzo_tamano);

    std::cout << "cabecera_invalida: magic ajeno y page_size=512 rechazados al abrir\n";
}

}  // namespace

int main() {
    prueba_archivo_vacio();
    prueba_ida_y_vuelta();
    prueba_varias_paginas();
    prueba_rid_estable();
    prueba_slot_no_reasignado();
    prueba_doble_eliminar_rid();
    prueba_rid_invalido();
    prueba_registro_mayor_que_pagina();
    prueba_reapertura();
    prueba_cabecera_invalida();
    prueba_verificar_mapa();
    prueba_reutilizacion_demostrable();
    prueba_reutilizacion_misma_longitud();
    prueba_codec();
    prueba_recorrer_sin_tumbas();
    prueba_corte_temprano();
    prueba_adaptador_insertar_scan();
    prueba_adaptador_eliminar_y_buscar();
    prueba_adaptador_stats();
    prueba_reorganizar();
    prueba_tamanos_variados();
    prueba_pagina_llena_fuerza_nueva();
    std::cout << "Prueba completada correctamente.\n";
}
