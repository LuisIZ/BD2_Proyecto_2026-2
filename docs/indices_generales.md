# Compatibilidad de índices

`CREATE INDEX nombre ON tabla (columna) USING BPLUS | HASH | RTREE` se puede usar en las tres
organizaciones (`HEAP`, `SEQUENTIAL` y `BPLUS`). Lo que cambia es el tipo de la columna:

| Índice | `INT` | `VARCHAR` | `POINT` |
|---|---|---|---|
| `BPLUS` | sí | sí | no |
| `HASH` | sí | sí | no |
| `RTREE` | no | no | sí |

Si la combinación no es válida, el motor da un error, por ejemplo
`un indice RTREE solo se crea sobre columnas POINT, no VARCHAR`. La tabla está en `ADMITIDOS`
([ejecutor.cpp](../motor/consultas/ejecutor.cpp)).

En `VARCHAR` la clave del índice son los primeros 4 caracteres del texto, así que puede
devolver filas de más y después se revisa la condición completa. Con `HASH` solo funciona la
igualdad.

En `SEQUENTIAL` y `BPLUS` el índice guarda la clave primaria en vez de la posición de la fila.

tambien tenemos una prueba `prueba_matriz_indices` ([sql_test.cpp](../motor/pruebas/sql_test.cpp)) donde probamos todas las
combinaciones.
