# B+ agrupado

| Archivo | Qué es |
|---|---|
| [bplus_agrupado.h](bplus_agrupado.h) | API pública: `motor::BPlusAgrupado`. |
| [bplus_agrupado.cpp](bplus_agrupado.cpp) | Implementación (pimpl) sobre [gestor_paginas.h](gestor_paginas.h) y [buffer_pool.h](buffer_pool.h), los mismos del B+ no agrupado. |
| [../pruebas/bplus_agrupado_test.cpp](../pruebas/bplus_agrupado_test.cpp) | Prueba unitaria con capacidades recortadas para forzar splits, préstamos y fusiones. |
| [../pruebas/bplus_agrupado_csv_test.cpp](../pruebas/bplus_agrupado_csv_test.cpp) | Carga `organizations-<n>.csv` y mide construcción, búsqueda, rango y eliminación. |

## La idea

Las hojas **son** las páginas de datos. El registro completo vive dentro de la hoja,
en orden físico de la clave. No hay heap aparte, no hay RID, no hay salto extra.

Como las hojas son los datos, el árbol no puede vivir en RAM. Todo está en un solo
archivo de páginas de 4096 B: la página 0 es la cabecera, el resto son nodos
internos (claves y punteros) y hojas (registros). La página `p` está en el byte `p × 4096`.

## Qué hay en RAM

Solo el `BufferPool` de 64 marcos (256 KB) que guarda copias temporales de las
páginas en uso y desaloja la menos usada (LRU). Los punteros del árbol son
números de página, no direcciones de memoria.

## El registro

El árbol no conoce el esquema. Recibe registros de **tamaño fijo** (`tam_registro`
bytes) cuyos **primeros 4 bytes son la clave `int`**. Cualquier struct trivialmente
copiable que empiece con `int clave` sirve:

```cpp
struct Organizacion {
    int  clave;          // va primero
    int  fundada;
    int  empleados;
    char org_id[16];
    char nombre[40];
    char pais[56];
    char industria[56];
};                       // 180 bytes

motor::BPlusAgrupado arbol(".build/orgs.dat", sizeof(Organizacion), /*truncar=*/true);
arbol.insertar(o);                              // false si la clave ya existe
Organizacion salida;
arbol.buscar(4321, salida);                     // false si no está
auto filas = arbol.buscar_rango<Organizacion>(1000, 1999);
arbol.eliminar(4321);
```

Por debajo la API es por bytes (`insertar_bytes`, `buscar_bytes`,
`buscar_rango_bytes` con visitante, `cargar_masivo_bytes`); los métodos tipados
comprueban que `sizeof(R) == tam_registro` y delegan. Cuando el parser SQL tenga
tablas con esquema, serializa la fila a un buffer fijo y usa la API por bytes.

## Capacidad

Se deriva de la página al crear el archivo y queda grabada en la cabecera, así
al reabrir se usa el mismo layout. Con `B = 4096`, cabecera `H = 8`, clave `V = 4`,
puntero `P = 4` y registro `R`:

```
interno   h ≤ (B − H + V) / (V + P) = 511 hijos  →  510 claves
hoja      n ≤ (B − H) / R                         →  22 registros con R = 180
```

Los parámetros `max_regs_hoja` y `max_hijos` del constructor recortan eso para ver
el árbol crecer con pocos datos (la prueba unitaria usa 3 y 4). En 0 se usa todo lo
que cabe. Con capacidades recortadas la página sigue ocupando 4096 B, así que las
cifras de disco no son representativas: para medir, dejarlos en 0.

## Layout en disco

```
página 0   CabeceraArchivo: magico, version, tam_registro, max_regs_hoja,
           max_hijos, raiz, libres, num_registros
hoja       CabeceraPagina(8 B) | reg[0] | reg[1] | ... (tam_registro cada uno)
interno    CabeceraPagina(8 B) | hijos[max_hijos] | claves[max_hijos − 1]
```

`CabeceraPagina` = tipo (1 B), reservado (1 B), num (2 B), siguiente (4 B). En las
hojas `siguiente` enlaza con la hoja de la derecha; en los internos no se usa.

## Operaciones

**`insertar`**: baja hasta la hoja fijando cada página y mete el registro en orden.
Si la clave ya existe devuelve `false` sin tocar nada. Si la hoja pasa de su máximo,
se parte y **copia** la primera clave de la derecha al padre. Si el padre se llena,
se parte y la clave del medio **sube y desaparece de abajo**. Si se parte la raíz,
el árbol crece.

**`buscar`**: baja con búsqueda binaria en cada página. La fila ya está ahí.
Costo: una lectura por nivel.

**`buscar_rango`**: baja hasta la hoja de `desde` y avanza por `siguiente` hasta
pasarse de `hasta`. Una lectura por página, secuencial. El visitante puede cortar
devolviendo `false`.

**`eliminar`**: borra de la hoja. Si queda bajo el mínimo (`max / 2`), pide prestado
al hermano izquierdo, luego al derecho, y si ninguno puede, fusiona. Puede repetirse
hacia arriba; si la raíz queda vacía, el árbol baja un nivel. La página vacía vuelve
a la lista de libres y la reutiliza la siguiente asignación.

**`cargar_masivo`**: exige árbol vacío y registros ordenados sin repetir (la versión
tipada ordena por ti). Construye de abajo hacia arriba llenando al 90 %. Insertar
en orden ascendente deja el árbol al 50 %, porque al partirse una hoja la mitad
izquierda ya nunca recibe nada más. Con 100 000 organizaciones: 6451 hojas
insertando una a una vs 5263 con carga masiva, y 820 ms vs 87 ms.

## Verificación

`verificar_invariantes()` recorre todo el árbol y comprueba: hojas a la misma
profundidad, claves estrictamente crecientes dentro de cada página y a lo largo de
la cadena de hojas, cada clave dentro del rango que marcan los separadores del
padre, ocupación mínima en todo nodo que no sea raíz, y que el total coincida con
`num_registros`. Las pruebas lo llaman después de cada lote de operaciones.

## Compilar y correr

Desde la raíz del repo. Las pruebas escriben en `.build/` y el test CSV lee de `datos/`.

```bash
mkdir -p .build
g++ -std=c++17 -Wall -Wextra -pedantic -O2 \
  motor/indices/bplus_agrupado.cpp motor/indices/buffer_pool.cpp motor/indices/gestor_paginas.cpp \
  motor/pruebas/bplus_agrupado_test.cpp -o .build/bplus_agrupado_test
./.build/bplus_agrupado_test

g++ -std=c++17 -Wall -Wextra -pedantic -O2 \
  motor/indices/bplus_agrupado.cpp motor/indices/buffer_pool.cpp motor/indices/gestor_paginas.cpp \
  motor/pruebas/cargador_csv.cpp motor/pruebas/bplus_agrupado_csv_test.cpp -o .build/bplus_agrupado_csv_test
./.build/bplus_agrupado_csv_test --n 1000
./.build/bplus_agrupado_csv_test --n 100000 --barajar --salida datos/resultados/bplus_agrupado.csv
```

```powershell
New-Item -ItemType Directory -Force .build | Out-Null
g++ -std=c++17 -Wall -Wextra -pedantic -O2 `
  motor/indices/bplus_agrupado.cpp motor/indices/buffer_pool.cpp motor/indices/gestor_paginas.cpp `
  motor/pruebas/bplus_agrupado_test.cpp -o .build/bplus_agrupado_test.exe
.\.build\bplus_agrupado_test.exe

g++ -std=c++17 -Wall -Wextra -pedantic -O2 `
  motor/indices/bplus_agrupado.cpp motor/indices/buffer_pool.cpp motor/indices/gestor_paginas.cpp `
  motor/pruebas/cargador_csv.cpp motor/pruebas/bplus_agrupado_csv_test.cpp -o .build/bplus_agrupado_csv_test.exe
.\.build\bplus_agrupado_csv_test.exe --n 100000 --barajar --salida datos/resultados/bplus_agrupado.csv
```

## Límites

- La clave es `int` y única. Para claves compuestas o texto hace falta otra comparación.
- Registros de tamaño fijo; los campos de texto se rellenan hasta su máximo.
- `GestorPaginas` y `BufferPool` viven en `namespace detalle_bplus_no_agrupado`
  porque nacieron ahí; ahora los usan los dos índices y convendría moverlos a
  `motor/comun/` con un nombre neutro.
