# motor/archivos

Organizaciones de archivos del motor. Todas implementan `IFileOrganization`, declarada en
`motor/comun/archivo.h`, de modo que son intercambiables y comparables con la misma bateria de
pruebas.

| Archivo | Que es |
|---|---|
| `sequential_file.{h,cpp}` | Archivo Secuencial Paginado. Ver `docs/explicativo.md`. |
| `serializacion.h` | `escribir_campo` / `leer_campo` con `memcpy`. Sin dependencias. |
| `pagina_slotted.{h,cpp}` | Pagina slotted sobre un buffer prestado. **Cero I/O.** Aloja `RecordId`, `PAGE_SIZE` y las constantes del layout. |
| `heap_file.{h,cpp}` | Heap File persistido: cabecera de archivo, mapa de espacio libre, compactacion y capa adaptadora. |

## Heap File

La documentacion completa esta en **[`docs/heap_file.md`](../../docs/heap_file.md)**: layout de
bytes, codificacion de la tumba, estabilidad del `RecordId`, mapa de espacio libre, analisis de
fragmentacion, costes medidos por operacion y divergencias con el Sequential File.

Expone **dos capas** en la misma clase:

- **API nativa** por RID sobre bytes crudos (`insertar_bytes`, `obtener`, `eliminar_rid`,
  `recorrer`, `compactar_pagina`, `stats_heap`), que es la que cumple el issue #1.
- **Capa adaptadora** que implementa `IFileOrganization` codificando `Registro` a bytes, que es la
  que lo hace comparable contra el Sequential File.

```powershell
.\motor\construir_heap.ps1                 # compila y corre las pruebas rapidas
.\motor\construir_heap.ps1 -ConEscala      # anade el test de 100 000 registros
.\motor\construir_heap.ps1 -ConBench       # anade las 6 corridas de benchmark
```

`RecordId`, `PAGE_SIZE` y las constantes del layout viven en `pagina_slotted.h`, en un bloque
contiguo marcado con `// --- candidatos a motor/comun/pagina.h ---`. Estan ahi y no en
`motor/comun/` para no tocar zona compartida; cuando lleguen los indices B+ —que necesitan guardar
RIDs— promoverlos es mover ese bloque y anadir un `#include`. **El nombre `PAGE_SIZE` queda
reservado en `namespace motor`** mientras viva aqui: si alguien define otro en un futuro
`comun/pagina.h`, cualquier unidad de traduccion que incluya ambos falla por redefinicion.
