# Extendible Hashing en C++17

## 1. Alcance

El indice hash extensible esta implementado en:

- `motor/indices/extendible_hash.h`: implementacion template completa.
- `motor/indices/extendible_hash.cpp`: unidad de compilacion del modulo.
- `motor/pruebas/extendible_hash_test.cpp`: pruebas estructurales y de rendimiento.
- `motor/pruebas/extendible_hash_csv_test.cpp`: prueba con el CSV real de 100 000 organizaciones.

La clase acepta tipos de clave, valor y funcion hash:

```cpp
motor::ExtendibleHashing<int, std::string> indice(128);
```

La implementacion actual es **en memoria**. El directorio es un
`std::vector` de `std::shared_ptr<Bucket>` y cada bucket usa un
`std::unordered_map`. Las paginas descritas aqui son una dimension logica; aun
no existe un administrador de paginas que persista bloques en disco.

## 2. Estructura C++

Cada instancia mantiene:

- `directorio_`: vector de referencias a buckets.
- `profundidad_global_`: bits usados para indexar el directorio.
- `Bucket::profundidad_local`: bits que distinguen a un bucket.
- `Bucket::registros`: `std::unordered_map<Clave, Valor, Hash>`.
- `capacidad_bucket_`: cantidad maxima de registros por bucket.
- `profundidad_maxima_`: limite de bits permitidos.
- `cantidad_`: registros activos.

Varias posiciones pueden compartir el mismo `shared_ptr`. Si la profundidad
global es `d`, el vector tiene `2^d` posiciones, aunque puede haber menos
buckets fisicos distintos.

## 3. Tamano de pagina y dataset

El archivo `datos/organizations-100000.csv` tiene, medido en UTF-8 con
`CRLF` incluido por fila:

| Medida | Valor |
|---|---:|
| Registros | 100 000 |
| Tamano minimo de fila | 100 bytes |
| Tamano promedio de fila | 140.17 bytes |
| Tamano maximo de fila | 220 bytes |
| Tamano total | 14 017 171 bytes |

El C++ no fija una pagina fisica. Para planificar una version persistente:

### Pagina de 4 KiB

Una pagina tiene 4096 bytes:

```text
floor(4096 / 140.17) = 29 registros promedio
ceil(14 017 171 / 4096) = 3423 paginas para el CSV
```

Con 16-32 bytes estimados de header, slot, longitud y alineamiento, la
capacidad practica es aproximadamente 25-27 filas por pagina.

### Pagina de 8 KiB

Una pagina tiene 8192 bytes:

```text
floor(8192 / 140.17) = 58 registros promedio
ceil(14 017 171 / 8192) = 1712 paginas para el CSV
```

Con el mismo overhead estimado, caben aproximadamente 50-53 filas por pagina.

Estas cifras son para la fila CSV serializada. Un `std::string` tambien tiene
objeto, capacidad asignada y allocator; el nodo de `unordered_map` agrega
overhead dependiente de la biblioteca estandar.

## 4. Capacidad de bucket

`capacidad_bucket_` cuenta registros, no bytes. La prueba CSV usa:

```cpp
auto indice = motor::ExtendibleHashing<int, std::string>::construir(
    registros, 128);
```

Con el promedio del CSV, 128 filas representan aproximadamente:

```text
128 * 140.17 = 17 941.76 bytes de payload promedio
```

Eso equivale a unas 5 paginas de 4 KiB o 3 paginas de 8 KiB antes de
metadatos. Es adecuado para la prueba en memoria, pero seria grande para un
bucket persistente de una pagina. Como punto de partida se recomiendan 25
registros por bucket para paginas de 4 KiB y 50 para 8 KiB.

## 5. Direccionamiento

Se usan los bits menos significativos del hash:

```text
indice = hash(clave) & ((1 << profundidad_global_) - 1)
```

Con profundidad global 3 hay 8 posiciones. Para `int`, el hash por defecto es
`std::hash<int>`.

`buscar` calcula el hash, indexa el vector y consulta el `unordered_map` del
bucket. Su costo esperado es $O(1)$ en memoria. En una version paginada,
equivaldria a consultar el directorio y luego una pagina de bucket.

## 6. Insercion y split

`insertar(clave, valor)`:

1. Busca el bucket y verifica duplicados.
2. Inserta si hay espacio.
3. Si esta lleno, verifica que las claves no tengan hashes identicos.
4. Divide el bucket y redistribuye registros.
5. Repite hasta encontrar espacio.

Si la profundidad local coincide con la global, el directorio se duplica:

```text
[B0, B1] -> [B0, B1, B0, B1]
```

Si las claves coinciden en el siguiente bit pero difieren en bits posteriores,
se permiten splits sucesivos. Solo se lanza `std::overflow_error` cuando todos
los bits disponibles son iguales o se alcanza `profundidad_maxima_`.

## 7. Invariantes

Despues de un split:

- `directorio_.size() == 2^profundidad_global_`.
- `profundidad_local <= profundidad_global_`.
- Un bucket local `l` aparece `2^(global-local)` veces.
- Cada registro se encuentra en su bucket calculado.
- Un bucket exitosamente insertado no supera `capacidad_bucket_`.

Las pruebas C++ cubren crecimiento, colisiones inseparables, merges y
reduccion de profundidad.

## 8. Eliminacion y merge

`eliminar` borra del `unordered_map` y busca el buddy mediante el bit de la
profundidad local. Dos buckets se fusionan si tienen igual profundidad local,
son referencias hermanas y sus registros caben juntos.

Despues del merge se reduce la profundidad local. El directorio se reduce
mientras no exista un bucket con profundidad local igual a la global.

La eliminacion puede ser mas costosa que la consulta porque la implementacion
recorre referencias del directorio para localizar y reemplazar buddies. En
una ejecucion del test CSV se observaron aproximadamente 14.4 ms promedio por
eliminacion; cambia segun hardware y patron de merges.

## 9. API y metricas

- `insertar`: `true` si agrega, `false` si la clave existe.
- `buscar`: devuelve `std::optional<Valor>`.
- `contiene`: comprueba existencia.
- `eliminar`: `true` si elimina.
- `supportsRange()` y `supports_range()`: `false`.
- `cantidad`, `profundidad_global`, `cantidad_buckets`, `capacidad_bucket`.
- `metricas`: devuelve `MetricasHash`.
- `resetear_metricas`: reinicia metricas operativas y conserva construccion.

`MetricasHash` mide tiempo de construccion, cantidad y promedio de consultas,
inserciones y eliminaciones, y espacio adicional estimado.

El espacio suma vector de directorio, buckets y una aproximacion de
`sizeof(Clave) + sizeof(Valor)` por registro. No mide exactamente el allocator
ni toda la capacidad sobrante de cada `unordered_map`.

## 10. Logs

El log predeterminado es:

```text
logs/extendible_hash_cpp.log
```

Se registran construcciones y solicitudes de metricas. La ruta puede darse al
constructor mediante `std::filesystem::path`. El archivo usa modo append.

## 11. Pruebas

Desde la raiz:

```powershell
New-Item -ItemType Directory -Force .build | Out-Null
g++ -std=c++17 -Wall -Wextra -pedantic motor/indices/extendible_hash.cpp motor/pruebas/extendible_hash_test.cpp -o .build/extendible_hash_test.exe
g++ -std=c++17 -Wall -Wextra -pedantic motor/indices/extendible_hash.cpp motor/pruebas/extendible_hash_csv_test.cpp -o .build/extendible_hash_csv_test.exe
```

```powershell
.\.build\extendible_hash_test.exe
.\.build\extendible_hash_csv_test.exe
```

La prueba CSV construye el indice con 100 000 filas, consulta claves 1,
50 000 y 100 000, verifica una inexistente, elimina 1 000 claves y las
reinserta.

## 12. Limitaciones

Actualmente no se persisten directorio ni buckets, no se serializan strings
en paginas y no hay recuperacion ante fallos. Para pasar a disco se requiere
un formato de pagina, bitmap de slots, registros de longitud variable y
overflow. Las capacidades por pagina deben recalcularse despues de fijar ese
formato.
