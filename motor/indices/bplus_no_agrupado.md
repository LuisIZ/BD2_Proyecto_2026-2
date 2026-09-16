# B+ no agrupado

Código: [bplus_no_agrupado.cpp](bplus_no_agrupado.cpp) 

Ejemplo con el heap: [ejemplo_heap.cpp](../pruebas/ejemplo_heap.cpp)

## RID

El heap nos da un RID con dos números: página y slot. Nosotros lo juntamos en uno:

```
pos    = pagina * 65536 + slot
pagina = pos / 65536
slot   = pos % 65536
```
El slot va de 0 a 65535, así página y slot nunca se pisan.
Esa cuenta va fuera del B+. El B+ solo ve un `long long`.

## Repetidos

Varias filas pueden tener la misma clave. Se ordena por el par: primero la clave y, si empatan, la `pos`.
Como dos filas nunca comparten RID, no hay dos entradas iguales. Eso lo hace `menor()`.

## Insertar: `insertar(clave, pos)`

- Baja hasta la hoja guardando el camino y mete el par en orden.
- Si la hoja pasa de 3, se parte y copia la primera entrada de la derecha al padre.
- Si el padre se llena, se parte y la entrada del medio sube. Si se parte la raíz, el árbol crece.

## Buscar: `buscar(clave)` / `buscar_rango(desde, hasta)`

- Baja con `(clave, -1)`: el -1 va antes de cualquier pos, así cae justo antes del primer repetido.
- Avanza por las hojas (`siguiente`) juntando pos hasta pasarse de `hasta`.
- Con cada pos se lee la fila del heap.

## Eliminar: `eliminar(clave)`

- Saca todas las pos de esa clave y borra cada par exacto con `eliminar_entrada`.
- Si una hoja queda con menos de 2: pide prestado a un hermano y, si no se puede, se fusiona. Puede repetirse hacia arriba; si la raíz queda vacía, el árbol baja un nivel.
- El B+ elimina la referencia; `HeapFile` elimina la fila.
