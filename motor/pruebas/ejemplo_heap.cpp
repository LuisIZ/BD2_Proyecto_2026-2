#include "../archivos/heap_file.h"
#include "../indices/bplus_no_agrupado.cpp"
#include <iostream>

using namespace std;

int main() {
    motor::HeapFile heap(".build/ejemplo_heap.heap", true);
    BPlusNoAgrupado indice;

    motor::Registro filas[3] = {{2019, "Acevedo LLC"}, {2021, "Blanco SAC"}, {2019, "Castro EIRL"}};

    // insertar: el heap guarda la fila y devuelve su rid. el indice guarda (año, rid)
    for (motor::Registro fila : filas) {
        vector<std::byte> bytes = motor::codificar_registro(fila);
        motor::RecordId rid = heap.insertar_bytes(bytes.data(), bytes.size());
        indice.insertar(fila.clave, (long long)rid.page_id * 65536 + rid.slot_id);
    }

    // buscar: el indice da la posicion de cada fila y el heap la lee
    for (long long pos : indice.buscar(2019)) {
        motor::RecordId rid;
        rid.page_id = pos / 65536;
        rid.slot_id = pos % 65536;

        vector<std::byte> bytes = heap.obtener(rid).value();
        motor::Registro fila;
        motor::decodificar_registro(bytes.data(), bytes.size(), fila);
        cout << fila.clave << " " << fila.valor << "  (pagina " << rid.page_id << ", slot " << rid.slot_id << ")\n";
    }
    return 0;
}
