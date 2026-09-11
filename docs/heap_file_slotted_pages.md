# Heap File con paginas slotted

Organizacion de archivos que almacena registros **en orden de llegada** sobre paginas de tamano
fijo persistidas en disco, con borrado logico y reutilizacion del espacio liberado. Corresponde
a la seccion 2.1.1 del enunciado (issue #1).

Codigo:

- `motor/archivos/serializacion.h` — lectura y escritura de campos con `memcpy`.
- `motor/archivos/pagina_slotted.{h,cpp}` — la pagina slotted, sin nada de I/O.
- `motor/archivos/heap_file.{h,cpp}` — persistencia, mapa de espacio libre y capa adaptadora.
- `motor/pruebas/` — cuatro ejecutables: pruebas de pagina, funcionales, escala y benchmark.

Los numeros de este documento son **medidos**, no estimados, sobre
`datos/organizations-100000.csv` (100 000 filas, valor medio de 132 B).

---

## 1. Arquitectura en dos capas

`motor/comun/archivo.h` es zona compartida del equipo y **no se modifico ni una linea**. Pero la
interfaz que define direcciona registros por `int clave` y los devuelve por valor:

```cpp
virtual bool insertar(const Registro& registro) = 0;
virtual bool eliminar(int clave) = 0;
virtual std::vector<Registro> scan() const = 0;
virtual EstadisticasArchivo stats() const = 0;
virtual void reorganizar() = 0;
```

El issue, en cambio, exige `insert` → `RecordId` estable, `get(rid)` y `remove(rid)` sobre bytes.
Esa API no cabe en la interfaz anterior. La solucion fue **dos capas en la misma clase**:

- **API nativa**, por RID y sobre bytes crudos: `insertar_bytes`, `obtener`, `eliminar_rid`,
  `recorrer`, `compactar_pagina`, `stats_heap`. Es la que cumple el issue.
- **Capa adaptadora**, que implementa `IFileOrganization` sobre la anterior codificando
  `Registro` a bytes. Es la que hace al heap comparable contra el Sequential File con la misma
  bateria de pruebas.

`RecordId`, `PAGE_SIZE` y las constantes del layout viven en `motor/archivos/pagina_slotted.h`,
en un bloque contiguo marcado con `// --- candidatos a motor/comun/pagina.h ---` para que
promoverlos cuando lleguen los indices B+ sea mover diez lineas y anadir un `#include`.

---

## 2. Layout de bytes

`PAGE_SIZE = 4096`. Con registros codificados de 99 a 224 B (medio 140 B, coste 144 B con su
entrada de directorio) entran unos 28 por pagina: 100 000 registros ocupan 3 575 paginas y
14 647 296 B, un **4.5 % de overhead** sobre los 14 017 264 B del CSV original. Frente a 512 B el
mapa en RAM crece a 130 KB y se multiplican las I/O; frente a 8192 B aparece amplificacion de
lectura (8 KB de I/O para leer 140 B). Ademas 4096 coincide con el cluster de NTFS.

### 2.1 El archivo

La pagina 0 se reserva **completa** para la cabecera aunque solo use 24 bytes. Asi el offset de
la pagina `i` es una multiplicacion limpia y desaparece toda una familia de errores off-by-one:

```
offset_de(page_id) = (page_id + 1) * PAGE_SIZE

offset 0            +----------------------------+
                    |  CABECERA DE ARCHIVO       |  PAGE_SIZE, 24 B usados
offset 1*PAGE_SIZE  +----------------------------+
                    |  pagina de datos 0         |
offset 2*PAGE_SIZE  +----------------------------+
                    |  pagina de datos 1         |
                    |            ...             |
                    +----------------------------+
```

| off | tipo | campo | nota |
|---|---|---|---|
| 0 | `uint32` | `magic` | `0x48454150` ("HEAP") |
| 4 | `uint16` | `version` | 1 |
| 6 | `uint16` | `reservado` | 0 |
| 8 | `uint32` | `page_size` | validado contra la constante compilada al abrir |
| 12 | `uint32` | `num_pages` | paginas de datos, sin contar la cabecera |
| 16 | `uint32` | `first_page` | o `PAGINA_INVALIDA` |
| 20 | `uint32` | `last_page` | cola de la cadena: encadenar es O(1), no O(P) |

Validar `magic` y `page_size` al abrir no es ceremonia: sin esa comprobacion, abrir un archivo
generado con otro `PAGE_SIZE` produce basura silenciosa en vez de un error. La cabecera se
persiste **en cada creacion de pagina**, no al cerrar; si se escribiera solo en el destructor, un
`assert` que aborte dejaria el archivo declarando menos paginas de las que tiene y la reapertura
perderia datos sin avisar.

### 2.2 La pagina

Dos zonas que crecen en direcciones opuestas hacia el mismo hueco, lo que mantiene el espacio
libre **siempre contiguo por construccion**:

```
 0                                                          PAGE_SIZE
 +--------+-----------------+--------------+---------------------+
 | HEADER | DIRECTORIO      |    LIBRE     |       DATOS         |
 |  12 B  | crece -->       |  (contiguo)  |       <-- crece     |
 +--------+-----------------+--------------+---------------------+
          ^                 ^              ^
     HEADER_SIZE   12 + slot_count*4     free_ptr
```

| off | tipo | campo |
|---|---|---|
| 0 | `uint16` | `slot_count` — entradas del directorio, **incluye tumbas** |
| 2 | `uint16` | `free_ptr` — primer byte ocupado; pagina vacia ⇒ `PAGE_SIZE` |
| 4 | `uint32` | `next_page` |
| 8 | `uint16` | `live_count` |
| 10 | `uint16` | `dead_bytes` |

`live_count` y `dead_bytes` son derivables recorriendo el directorio, pero tenerlos en la cabecera
hace que reconstruir el mapa al abrir sea **una lectura de 12 bytes por pagina** en vez de leer
paginas completas: 29 ms para 3 575 paginas.

Una pagina vacia arranca con `free_ptr = PAGE_SIZE`, **no 0**. Los datos crecen hacia abajo desde
el final; inicializarlo en 0 hace que el primer registro se escriba encima de la cabecera.

### 2.3 Directorio de slots y la tumba

Cada entrada ocupa 4 B en `HEADER_SIZE + slot_id * SLOT_SIZE`: `uint16 offset`, `uint16 length`.

**Una tumba es `length == SLOT_TUMBA == 0xFFFF`.** No `length == 0`: un registro de longitud cero
es legalmente representable —la prueba `prueba_longitud_cero` lo ejercita— y seria indistinguible
de una tumba. El valor 65535 es inalcanzable porque ningun registro valido supera los 4080 B. El
`offset` de la tumba se conserva intacto, como traza forense y a coste cero.

### 2.4 La condicion de capacidad

```cpp
uint16_t espacio_contiguo()    const { return free_ptr_ - (HEADER_SIZE + slot_count_*SLOT_SIZE); }
uint16_t espacio_recuperable() const { return espacio_contiguo() + dead_bytes_; }
bool     cabe(uint16_t L)      const { return espacio_contiguo() >= uint32_t(L) + SLOT_SIZE; }
```

El `+ SLOT_SIZE` **no es opcional**: insertar consume `L` bytes de datos *y* 4 bytes de directorio,
y ambos crecen hacia el mismo hueco. Olvidarlo produce un bug precioso — la pagina parece tener
espacio, se escribe el registro, y el directorio pisa el primer byte del ultimo dato insertado;
la corrupcion aparece mucho despues, al leer. La prueba `prueba_falta_espacio_para_slot` lo aisla:
con 28 B libres, un registro de 25 B "cabria" pero 25+4 no, y verifica ademas que el fallo deja la
pagina **identica byte a byte**.

La suma va en `uint32_t` para que `L` cerca de 65535 no desborde.

### 2.5 Serializacion

Campo a campo con `memcpy`, nunca casteando el buffer a un `struct` ni con `#pragma pack`: el
padding que inserte el compilador cambiaria el layout y el archivo dejaria de ser legible al
cambiar de compilador. `memcpy` se optimiza a un `mov` en `-O2`, asi que el coste real es cero.

**Endianness nativo (little-endian).** Es una limitacion conocida, no un descuido: los archivos no
son portables entre arquitecturas de distinto orden de bytes. Todo el equipo corre x86.

### 2.6 Codificacion de `Registro` (capa adaptadora)

```
+0  int32   clave
+4  uint32  longitud_valor
+8  bytes del valor
```

Total `8 + valor.size()`, con `valor` de hasta 4072 B. **La clave esta en el offset 0 a
proposito**: `buscar(clave)` lee un `int32` y descarta el registro sin construir el `std::string`,
que es lo que hace medible un scan con predicado sobre 100 000 registros.
`decodificar_registro` valida `longitud >= 8` y `8 + longitud_valor == longitud` antes de tocar
bytes, y devuelve `false` ante inconsistencias en vez de leer fuera de rango.

---

## 3. Estabilidad del RID

`RecordId` es `(page_id, slot_id)`. El contrato es que **el RID devuelto por `insertar_bytes`
sigue siendo valido mientras el registro este vivo**. De ahi salen tres reglas:

1. **`insertar` siempre anade un slot nuevo al final** (`slot_id = slot_count`) y **nunca reutiliza
   el slot de una tumba**. Si el slot 7 de una tumba se reasignara, un RID viejo que apuntaba ahi
   resolveria a un registro *distinto* en silencio. La tumba conserva su entrada de directorio
   precisamente para que ese `slot_id` no se reasigne jamas.
2. **El borrado es logico.** `dead_bytes += length` **antes** de sobrescribir `length = SLOT_TUMBA`;
   `live_count--`; `slot_count` y `free_ptr` **no cambian**. Los bytes del registro siguen
   fisicamente ahi, inaccesibles y contabilizados. Eso *es* la fragmentacion interna.
3. **La compactacion mueve bytes pero no slots.** `compactar()` reescribe los vivos contra el final
   de la pagina sobre un buffer temporal y actualiza solo el **offset** de cada slot vivo. El
   `slot_id` no cambia, asi que los RIDs externos siguen resolviendo. Es la unica operacion que
   mueve datos, y lo que la hace segura es la indireccion del directorio: el mundo exterior habla
   en `slot_id`, no en offsets.

`reorganizar()` **nunca reduce `num_pages`**: liberar una pagina invalidaria los RIDs que apunten
a ella. La prueba `prueba_reorganizar` verifica que tras recuperar 100 000 bytes muertos las
50 paginas siguen ahi y los 500 RIDs vivos resuelven al mismo contenido.

Esta propiedad es la razon por la que los indices B+ del proyecto van a poder guardar RIDs.

---

## 4. Mapa de espacio libre

El issue lo exige explicitamente: *"la busqueda de pagina con espacio no debe recorrer todo el
archivo: mapa de espacio libre en memoria reconstruido al abrir"*.

```cpp
std::vector<uint16_t> espacio_contiguo_;   // indexado por page_id
std::vector<uint16_t> bytes_muertos_;      // indexado por page_id
size_t registros_vivos_, tumbas_, bytes_muertos_totales_;   // agregados
uint32_t cursor_;
```

4 bytes de RAM por pagina: ~14 KB para 100 000 registros. Se descarto un `multimap` de *best fit*
porque mantenerlo (borrar y reinsertar en cada operacion) cuesta mas que barrer un vector de
3 575 `uint16_t` que cabe entero en L2 — y el beneficio seria marginal, ya que dentro de una
pagina slotted el espacio libre es siempre contiguo: la unica fragmentacion es intra-pagina y se
resuelve compactando, no eligiendo mejor.

Los **tres agregados** son la razon de que `stats_heap()` sea O(1) y no toque disco. Sin ellos,
`registros_vivos` y `tumbas` exigirian leer la cabecera de cada pagina.

**Reconstruccion al abrir:** un barrido leyendo solo los 12 B de cabecera de cada pagina. O(P)
I/Os **una sola vez al abrir**, nunca por insercion. Reconstruirlo en cada `insert` pasa los tests
chicos y muere con 100 000 registros. `verificar_mapa()` reconstruye en estructuras temporales y
compara contra las vivas; se llama en los tests tras insertar, borrar, compactar y reabrir.

### 4.1 Seleccion de pagina: una sola pasada

```
seleccionar_pagina(necesario):            // necesario = L + SLOT_SIZE
    mejor_compactable = PAGINA_INVALIDA;  mejor_muertos = 0
    para paso en [0, num_paginas):
        p = (cursor + paso) % num_paginas
        si espacio_contiguo[p] >= necesario:
            cursor = p;  return p                             // FIRST FIT
        si espacio_contiguo[p] + bytes_muertos[p] >= necesario
           y bytes_muertos[p] > mejor_muertos:
            mejor_muertos = bytes_muertos[p];  mejor_compactable = p
    si mejor_compactable != PAGINA_INVALIDA:
        compactar(mejor_compactable)                          // 1 lectura + 1 escritura
        cursor = mejor_compactable;  return mejor_compactable
    return PAGINA_INVALIDA                                    // -> crear_pagina()
```

El candidato compactable se busca **durante el mismo recorrido**, no en una segunda vuelta: todo
en RAM, cero I/O hasta decidir. Entre las compactables se elige la de **mas bytes muertos**, no la
primera, porque una compactacion cuesta 1 lectura + 1 escritura fijas y conviene que compre el
maximo espacio posible.

### 4.2 Las tres mitades del cursor

El cursor evita rebarrer el vector desde 0 en cada insercion, pero solo funciona si se mueve en
las tres direcciones:

| Donde | Que hace | Por que |
|---|---|---|
| Acierto de first fit | `cursor_ = p` | Las inserciones siguientes aciertan en `paso = 0` mientras esa pagina tenga hueco → O(1) amortizado en carga masiva |
| `eliminar_rid`, `compactar_pagina` | `cursor_ = min(cursor_, page_id)` | **Sin esto el criterio de reutilizacion falla.** Tras insertar 1000 el cursor queda cerca de la pagina 35; los borrados liberan espacio en 0–34, y una busqueda que solo mira hacia adelante crearia paginas nuevas — justo lo que el mecanismo existe para evitar |
| `crear_pagina` | `cursor_ = nueva` | La rotacion la encontraria igual, pero apuntarla ahi ahorra un barrido completo en el caso comun de carga masiva |

El barrido del vector es **en memoria**, no I/O, que es lo que el issue prohibe.

---

## 5. Reutilizacion de espacio: el criterio del issue

El criterio es "insertar 1000, borrar 500, insertar 500 → el archivo no crece". Hay una trampa
aritmetica que conviene enfrentar en vez de esperar que el test pase por suerte:

**las tumbas liberan bytes de datos pero NO su entrada de directorio** (4 B cada una). Es el precio
estructural de la estabilidad del RID. Reinsertar 500 registros del *mismo* tamano necesita
`500 x 4 = 2000` bytes que la fase de borrado no devolvio.

Con `L = 200` B (coste 204): 1000 registros dan 20 por pagina en 50 paginas, sobrando 4 B por
pagina. Borrar uno de cada dos deja `dead_bytes = 2000` por pagina, y tras compactar hay 2004 B
contiguos:

| Reinsercion | Cabe por pagina | Resultado |
|---|---|---|
| `L2 = 200` (coste 204) | `floor(2004/204) = 9` de 10 | 50 → **53 paginas** |
| `L2 = 180` (coste 184) | `floor(2004/184) = 10` de 10 | 50 → **50 paginas** |

**Condicion general demostrable: `L2 + SLOT_SIZE <= L1`.** Entonces
`k*(L2+4) <= k*L1 <= dead_bytes_pagina` y la compactacion siempre alcanza, para cualquier tamano y
sin depender del sobrante de la pagina.

Por eso el modulo lleva **dos** pruebas de reutilizacion, y la distincion es el punto:

- `prueba_reutilizacion_demostrable` (L1=200, L2=180) — **assert duro**: paginas y bytes en disco
  identicos. Pasa por construccion, no por casualidad. Salida real:

  ```
  tras 1000 inserciones: paginas=50, vivos=1000, tumbas=0,   desperdiciados=0,      libres=200,  archivo=208896 B
  tras borrar 500:       paginas=50, vivos=500,  tumbas=500, desperdiciados=100000, libres=200,  archivo=208896 B
  tras reinsertar 500:   paginas=50, vivos=1000, tumbas=500, desperdiciados=0,      libres=8200, archivo=208896 B
  ```

- `prueba_reutilizacion_misma_longitud` (L1=L2=200) — **assert acotado**: `50 -> 53` paginas,
  cuando sin reutilizacion serian **75**. Crecer 3 y no 25 es lo que demuestra que el mecanismo
  opera, y documenta honestamente el coste del directorio persistente.

Con registros de **longitud variable** el criterio no es estrictamente alcanzable ni en teoria: un
registro borrado de 99 B puede reemplazarse por uno de 224 B. "El archivo no crece" es una
afirmacion limpia solo sobre registros uniformes.

**El patron de borrado tambien importa.** Se borra alternado, no un prefijo contiguo: borrar los
primeros 500 dejaria unas paginas enteras vacias y otras enteras llenas, y forzaria una pagina
nueva. Es material de analisis, no un defecto.

---

## 6. Fragmentacion

Hay dos cantidades distintas y confundirlas es el error clasico:

- **`espacio_contiguo`** — el hueco entre el final del directorio y `free_ptr`. Es el unico
  utilizable sin compactar.
- **`dead_bytes`** — bytes de registros borrados que siguen ocupando su sitio. Existen pero no
  estan en el bloque contiguo, asi que **no se pueden usar sin compactar**.

`bytes_desperdiciados` de `stats_heap()` es la suma de `dead_bytes` sobre todas las paginas, y es
el criterio para decidir cuando compactar. Medido con 100 000 registros y 33 334 borrados:
**4 675 848 B muertos**, es decir 140.3 B por registro — exactamente `8 + 132.3` de la codificacion.

Hay una tercera fuente, mas silenciosa: el **sobrante de fin de pagina**, lo que no alcanza para el
siguiente registro. Medido, `bytes_libres = 172 024` sobre 3 575 paginas dan **48 B libres por
pagina** en promedio. Es pequeno, pero es justo el margen del que dependia el criterio de
reutilizacion con longitud variable.

La compactacion se dispara **por demanda**, dentro de `seleccionar_pagina`, exactamente cuando hace
falta el espacio — no por umbral periodico. Una compactacion solo ocurre cuando compra espacio que
se necesita ahora mismo.

---

## 7. Costes por operacion

Numeros medidos sobre 100 000 registros del dataset real (3 575 paginas):

| Operacion | I/Os | Medido |
|---|---|---|
| `insertar_bytes` | 1 lectura + ~1.07 escrituras | 16.6 us por registro; 103 574 lecturas y 107 149 escrituras para 100k |
| `obtener(rid)` | **1 lectura** | **11.4 us** |
| `eliminar_rid(rid)` | 1 lectura + 1 escritura | 14 us |
| `recorrer()` / `scan()` | **P lecturas** | 3 575 lecturas, 26 ms |
| `buscar(clave)` | **~P/2 lecturas** (corte temprano) | 1 598 paginas, **13 780 us** |
| `compactar_pagina` | 1 lectura + 1 escritura | — |
| `reorganizar()` | 1 lectura + 1 escritura por pagina con muertos | 18 072 us, 233 B recuperados por us |
| abrir el archivo | P lecturas de 12 B | 29 ms |

La contabilidad de la insercion salio exacta: `100000 + 3574 = 103574` lecturas (una por insercion,
mas una por pagina nueva al actualizar el `next_page` de la anterior) y
`100000 + 3575 + 3574 = 107149` escrituras (una por insercion, mas la inicializacion de cada pagina
nueva, mas el reencadenado).

**El contraste que importa:** `obtener(rid)` cuesta 11.4 us y una pagina; `buscar(clave)` cuesta
13 780 us y 1 598 paginas. Un factor de **1 668x**. Acceso directo O(1) contra busqueda por clave
O(P): esa es la debilidad estructural del heap file, y es exactamente lo que la comparacion
experimental con el Archivo Secuencial Paginado debe evidenciar.

### 7.1 Escalamiento medido

| N | Insercion **por registro** | Busqueda por clave | Paginas por busqueda | Paginas | Bytes en disco |
|---|---|---|---|---|---|
| 1 000 | 25.3 us | 77 us | 16 | 36 | 151 552 |
| 10 000 | 16.7 us | 1 254 us | 158 | 358 | 1 470 464 |
| 100 000 | 16.6 us | 13 780 us | 1 598 | 3 575 | 14 647 296 |

La insercion por registro es **plana**; la busqueda por clave **crece 10x por decada**. Las paginas
por busqueda son ≈ P/2 en las tres escalas, lo que confirma el corte temprano sobre un registro en
posicion uniforme.

**El heap es insensible al orden de insercion**: ordenado contra barajado da 36/36, 358/358 y
3575/3576 paginas. Contrasta con un archivo ordenado, para el que insertar claves ya ordenadas es
su mejor caso absoluto — razon por la que el harness baraja.

---

## 8. Compilar y ejecutar

```powershell
.\motor\construir_heap.ps1                 # compila y corre las pruebas rapidas
.\motor\construir_heap.ps1 -ConEscala      # anade el test de 100 000 registros
.\motor\construir_heap.ps1 -ConBench       # anade las 6 corridas de benchmark
```

O a mano, siguiendo el estilo de `docs/explicativo.md`:

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic -g -DHEAP_DEBUG -D_GLIBCXX_ASSERTIONS `
  motor/archivos/pagina_slotted.cpp motor/archivos/heap_file.cpp `
  motor/pruebas/heap_file_test.cpp -o .build/heap_file_test.exe
.\.build\heap_file_test.exe
```

**`-DHEAP_DEBUG` verifica los invariantes de pagina tras cada mutacion** (directorio que no invade
datos, rangos de vivos sin solape, `live_count + tumbas == slot_count`, y
`PAGE_SIZE - free_ptr == suma de largos vivos + dead_bytes`). **Nunca combinarlo con escala o
benchmark**: la comprobacion de solape es O(slots^2) por operacion. Se compila **sin** `-DNDEBUG`,
asi que los `assert` son la red de seguridad real.

El benchmark emite una fila por corrida a `datos/resultados/heap_bench.csv` en modo append. Una
fila mezcla tres momentos y esta documentado en la cabecera del `.cpp`:
`paginas`/`registros_vivos`/`bytes_en_disco` son el estado tras la insercion,
`tumbas`/`bytes_desperdiciados` tras el borrado y antes de reorganizar, y
`paginas_leidas`/`paginas_escritas` solo la fase de insercion. La primera columna,
`implementacion`, existe para que el Sequential File anada sus filas al mismo archivo.

Las corridas del CSV son **unicas, sin repeticion**: para el informe conviene promediar varias
variando `--semilla`.

---

## 9. Divergencias con el Sequential File

Ambos implementan `IFileOrganization` y son intercambiables, pero no hacen el mismo trabajo. Hay
que declararlo antes de comparar tiempos:

| | Heap File | Sequential File |
|---|---|---|
| Medio | disco real | `std::vector` en RAM |
| `insertar` rechaza duplicados | **no** | si (`contiene()`, O(P) por insercion) |
| Orden de `scan()` | de llegada | ordenado por clave |
| `reorganizar()` | compacta intra-pagina, **no reduce paginas** | reconstruye y reduce paginas |
| `porcentaje_desperdicio` | razon de **registros** (`tumbas/slots`) | razon de **registros** |
| Area de overflow | no tiene (`registros_auxiliares = 0`) | si |
| `umbral_reorganizacion` | `0.0` — compacta por demanda, sin umbral | 0.30 configurable |

Cuatro consecuencias practicas:

1. **Comparar tiempos de pared mide disco contra RAM**, no heap contra secuencial. La metrica
   honesta e independiente del medio son los **accesos logicos a pagina**, que el heap instrumenta
   con `paginas_leidas()` / `paginas_escritas()` y el benchmark emite por fila.
2. **El heap hace menos trabajo por diseno** al no validar unicidad: validarla costaria un scan
   O(P) por insercion, o sea O(n^2), y haria inviable el test de 100 000. Es coherente con la
   semantica de un heap file, pero los tiempos no son comparables sin anotarlo.
3. **`porcentaje_desperdicio` usa la definicion del Sequential File** (razon de registros) para ser
   directamente comparable. La metrica de bytes que pide el issue vive aparte, en
   `EstadisticasHeap::bytes_desperdiciados`. Meter bytes en ese campo produciria un grafico que
   compara peras con manzanas sin que se note.
4. **`reorganizar()` hace cosas distintas** en cada implementacion, asi que comparar
   `t_reorganizacion_us` a secas engana. La metrica comparable es **bytes recuperados por
   milisegundo**.

---

## 10. Limitaciones conocidas

- **Sin paginas de overflow.** Un registro mayor que 4080 B es un error (`std::invalid_argument`
  desde la API nativa, `false` desde el adaptador), no se fragmenta entre paginas. Con este
  dataset es inalcanzable: el maximo observado son 224 B, el 5.5 % de una pagina.
- **Sin buffer pool.** Cada operacion es un `read`/`write` al sistema operativo; la cache de
  paginas de Windows hace de buffer pool de facto. Correcto para el alcance del issue, pero hay que
  decirlo para que las mediciones no se malinterpreten.
- **Los slots de tumba nunca se liberan.** El directorio solo crece, asi que con un patron de
  borrado e insercion sostenido `slot_count` crece indefinidamente y el espacio util de la pagina
  decrece. La solucion estandar es anadir un contador de generacion al RID
  (`page_id, slot_id, generacion`); esta fuera del alcance de este issue.
- **Endianness nativo**: los archivos no son portables entre arquitecturas de distinto orden de
  bytes.
- **El visitante de `recorrer` recibe un puntero al buffer interno**, valido solo durante la
  llamada, y **no debe invocar metodos que lean o escriban paginas** porque clobberearian ese mismo
  buffer a media iteracion. Por eso `eliminar(int clave)` captura el RID dentro del recorrido y
  llama a `eliminar_rid` **despues** de que termine.
