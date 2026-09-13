# B+ agrupado

Código: [bplus_agrupado.cpp](bplus_agrupado.cpp), autónomo, se incluye con `#include`.

## La idea

Las hojas **son** las páginas de datos. El registro completo vive dentro de la hoja,
en orden físico de la clave `Index`. No hay heap aparte, no hay RID, no hay salto extra.

Como las hojas son los datos, el árbol no puede vivir en RAM. Todo está en un solo
archivo `.dat` de páginas de 4096 B: la página 0 es la cabecera, el resto son nodos
internos (claves y punteros) y hojas (registros). La página `p` está en el byte `p × 4096`.

## Qué hay en RAM

Solo un `BufferPool` de 64 marcos, 256 KB, que guarda copias temporales de las
páginas en uso. Cuando no hay sitio, desaloja la menos usada (LRU). Los punteros
del árbol son números de página, no direcciones de memoria.

## Capacidad

No se elige, se deriva de la página. Con `B = 4096`, cabecera `H = 8`, clave `V = 4`,
puntero `P = 4` y registro `R = 180`:

```
interno   h ≤ (B − H + V) / (V + P) = 511 hijos  →  510 claves
hoja      n ≤ (B − H) / R           = 22 registros
```

Comparado con el no agrupado, cuya hoja guarda pares de 8 bytes y cabe 511 entradas:
veintidós contra quinientas once en la misma página, porque aquí cada entrada carga
la fila entera.

`DEMO_REGS_HOJA = 3` y `DEMO_HIJOS = 4` recortan eso a mano para ver el árbol crecer con
pocos datos. En modo demo la página sigue ocupando 4096 B, así que las cifras de disco
no son representativas. Poner ambos en `0` para medir de verdad.

## Insertar: `insertar(Registro)`

- Baja hasta la hoja fijando cada página y mete el registro en orden.
- Si la hoja pasa de su máximo, se parte y **copia** la primera clave de la derecha al padre.
- Si el padre se llena, se parte y la clave del medio **sube y desaparece de abajo**.
  Si se parte la raíz, el árbol crece.

## Buscar: `buscar(clave, salida)` / `buscar_rango(desde, hasta)`

- Baja hasta la hoja con búsqueda binaria en cada página. La fila ya está ahí.
- El rango avanza por las hojas (`siguiente`) leyendo páginas consecutivas.

Costo puntual: una lectura por nivel, sin salto extra. Costo por rango: una lectura
por página, secuencial.

## Eliminar: `eliminar(clave)`

- Borra de la hoja. Si queda bajo el mínimo, el padre pide prestado a un hermano y,
  si no se puede, fusiona. Puede repetirse hacia arriba; si la raíz queda vacía, el
  árbol baja un nivel. La página vacía vuelve a la lista de libres.

## Carga masiva: `cargar_masivo(registros)`

Ordena y construye de abajo hacia arriba llenando al 90 %. Insertar en orden
ascendente deja el árbol al 50 %, porque al partirse una hoja la mitad izquierda ya
nunca recibe nada más.

## Límites

- Claves repetidas no se recuperan todas: el descenso se va a la derecha y no mira
  las páginas anteriores. Con `Index`, que es único, no ocurre.
- El rango devuelve todo en un vector, no un cursor.
- Registros de tamaño fijo; los campos de texto se rellenan hasta su máximo.
