# Compatibilidad de índices

`CREATE INDEX nombre ON tabla (columna) USING BPLUS | HASH | RTREE | SPIMI | IVF | HNSW` se puede usar en las tres
organizaciones (`HEAP`, `SEQUENTIAL` y `BPLUS`). Lo que cambia es el tipo de la columna:

| Índice | `INT` | `VARCHAR` | `POINT` | `VECTOR` |
|---|---|---|---|---|
| `BPLUS` | sí | sí | no | no |
| `HASH` | sí | sí | no | no |
| `RTREE` | no | no | sí | no |
| `SPIMI` | no | sí | no | no |
| `IVF` | no | no | no | sí |
| `HNSW` | no | no | no | sí |

`FLOAT` no admite ninguno de estos índices. `IVF` y `HNSW` son los de FAISS: el motor los valida
y los anota en su catálogo, y la API los construye y los consulta (ver
[sql_multimedia.md](sql_multimedia.md)). Sin ellos, el k-NN del motor es secuencial (ver
[vectores.md](vectores.md)).

Si la combinación no es válida, el motor da un error, por ejemplo
`un indice RTREE solo se crea sobre columnas POINT, no VARCHAR`. La tabla está en `ADMITIDOS`
([ejecutor.cpp](../motor/consultas/ejecutor.cpp)).

En `VARCHAR` la clave del índice son los primeros 4 caracteres del texto, así que puede
devolver filas de más y después se revisa la condición completa. Con `HASH` solo funciona la
igualdad.

En `SEQUENTIAL` y `BPLUS` el índice guarda la clave primaria en vez de la posición de la fila.

`SPIMI` es un índice invertido para búsqueda de texto: cada documento es una fila, identificada por su posición (o su clave primaria en `SEQUENTIAL` y `BPLUS`). Se guarda en el directorio `<tabla>__<indice>.spimi`. Con `INSERT` o `DELETE` se invalida y se reconstruye en la siguiente búsqueda con `MATCH`.

La prueba `prueba_matriz_indices` ([sql_test.cpp](../motor/pruebas/sql_test.cpp)) recorre las
combinaciones de los índices del motor, y `prueba_indices_vectoriales` valida `IVF` y `HNSW`.
