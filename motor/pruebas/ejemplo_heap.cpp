#include "../archivos/heap_file.h"
#include "../indices/bplus_no_agrupado.h"

#include <filesystem>
#include <iostream>
#include <vector>

using namespace std;

int main() {
    filesystem::create_directories(".build");
    const string ruta_heap = ".build/ejemplo_heap.heap";
    const string ruta_indice = ".build/ejemplo_indice.bplus";

    //1 ejecucion: se crean el heap y el indice desde cero
    {
        motor::HeapFile heap(ruta_heap, true);
        BPlusNoAgrupado indice(ruta_indice, true);

        motor::Registro filas[3] = {
            {2019, "Acevedo LLC"},
            {2021, "Blanco SAC"},
            {2019, "Castro EIRL"}
        };

        for (const motor::Registro& fila : filas) {
            vector<std::byte> bytes = motor::codificar_registro(fila);
            motor::RecordId rid = heap.insertar_bytes(bytes.data(), bytes.size());
            long long pos = static_cast<long long>(rid.page_id) * 65536 + rid.slot_id;
            indice.insertar(fila.clave, pos);
        }

        indice.sincronizar();
    }

    //2 ejecucion: se abren los mismos archivos sin borrarlos
    {
        motor::HeapFile heap(ruta_heap);
        BPlusNoAgrupado indice(ruta_indice);

        for (long long pos : indice.buscar(2019)) {
            motor::RecordId rid;
            rid.page_id = static_cast<uint32_t>(pos / 65536);
            rid.slot_id = static_cast<uint16_t>(pos % 65536);

            vector<std::byte> bytes = heap.obtener(rid).value();
            motor::Registro fila;
            motor::decodificar_registro(bytes.data(), bytes.size(), fila);

            cout << fila.clave << " " << fila.valor
                 << "  (pagina " << rid.page_id
                 << ", slot " << rid.slot_id << ")\n";
        }

        cout << "Paginas del indice en cache: " << indice.paginas_en_cache() << '\n';
    }

    return 0;
}
