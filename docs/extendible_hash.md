# Extendible Hashing

La implementacion Python se encuentra en:


## Uso

```python
from motor.indices import ExtendibleHashing

indice = ExtendibleHashing(bucket_capacity=2)
indice.insertar(10, "diez")
indice.insertar(20, "veinte")

assert indice.buscar(10) == "diez"
assert indice.eliminar(10)
assert not indice.contiene(10)
assert not indice.supportsRange()
```

# Implementacion de Extendible Hashing

## 1. Objetivo

Se implemento un indice hash extensible en Python para resolver busquedas
exactas por clave en el motor de base de datos. La estructura puede crecer y
redistribuir sus registros sin reconstruir todo el indice cada vez que un
bucket se llena.

El indice es adecuado para predicados como:

```sql
WHERE id = 125
```

No es adecuado para predicados de rango como:

```sql
WHERE id BETWEEN 100 AND 200
```

La implementacion trabaja actualmente en memoria. Su organizacion esta
preparada para conectarse posteriormente con paginas persistidas en disco.

Los archivos principales son:

- `motor/indices/extendible_hash.py`: clase `ExtendibleHashing` y la logica
	del indice.
- `motor/indices/__init__.py`: exporta la clase como parte del paquete.
- `motor/pruebas/test_extendible_hash.py`: pruebas automatizadas.
- `motor/pruebas/test_extendible_hash_csv.py`: prueba de integracion con el
	CSV real de 100 000 organizaciones.

## 2. Conceptos principales

El extendible hashing utiliza dos niveles de organizacion:

- **Directorio**: arreglo de referencias a buckets. Varias posiciones pueden
	apuntar al mismo bucket.
- **Bucket**: contenedor que guarda las claves y sus valores. Cada bucket
	tiene una capacidad maxima y una profundidad local.

Tambien existen dos profundidades:

- **Profundidad global (`global depth`)**: cantidad de bits usados para
	seleccionar una posicion del directorio.
- **Profundidad local (`local depth`)**: cantidad de bits que distingue a un
	bucket particular.

Al comenzar, la estructura tiene profundidad global cero y un unico bucket:

```text
Directorio: [ B0 ]
Profundidad global: 0
B0.profundidad_local: 0
```

Cuando la profundidad global es `d`, el directorio tiene `2^d` posiciones.
Sin embargo, no necesariamente existen `2^d` buckets distintos, porque
varias posiciones pueden compartir la misma referencia.

## 3. Hash y direccionamiento

La clase recibe una funcion hash opcional. Si no se proporciona, utiliza la
funcion `hash` de Python. Para seleccionar el bucket se toman los bits menos
significativos del hash:

```text
indice = hash(clave) AND ((1 << profundidad_global) - 1)
```

Por ejemplo, si la profundidad global es 3, se utilizan los ultimos tres
bits:

```text
hash(clave) = ...101101
								 ^^^
indice = 101 en binario = 5
```

El directorio se consulta directamente con ese indice. Por eso una busqueda
exacta no recorre todos los registros ni todos los buckets.

## 4. Insercion normal

Cuando se ejecuta `insertar(clave, valor)`, se siguen estos pasos:

1. Se busca el bucket usando el hash y la profundidad global.
2. Se verifica que la clave no exista.
3. Si el bucket tiene espacio, se agrega el registro.
4. Si el bucket esta lleno, se intenta dividirlo.
5. Los registros del bucket se redistribuyen utilizando un bit adicional.
6. Se repite la busqueda hasta encontrar un bucket con espacio.

Las claves duplicadas no reemplazan el valor anterior. En ese caso,
`insertar` devuelve `False` y conserva el registro original.

## 5. Split de un bucket

Un split aumenta en uno la profundidad local del bucket lleno y crea un
bucket hermano. Las referencias del directorio que correspondan al nuevo bit
se cambian para apuntar al hermano.

Supongamos una capacidad de bucket igual a 2:

```text
Antes del split:

Directorio: [ B0, B0 ]
Profundidad global: 1
B0.profundidad_local: 1
```

Si `B0` se llena, se divide:

```text
Despues del split:

Directorio: [ B0, B1 ]
Profundidad global: 1
B0.profundidad_local: 1
B1.profundidad_local: 1
```

Los registros que estaban en `B0` se vuelven a direccionar. Un registro va a
`B0` o `B1` segun el bit que acaba de incorporarse.

### Duplicacion del directorio

Si el bucket lleno tiene una profundidad local igual a la global, no existe
un bit adicional disponible dentro del directorio actual. Primero se duplica
el directorio y se aumenta la profundidad global:

```text
Antes:

Directorio: [ B0, B1 ]
Profundidad global: 1

Despues de duplicar:

Directorio: [ B0, B1, B0, B1 ]
Profundidad global: 2
```

Luego se divide el bucket lleno y se actualizan solo las referencias
correspondientes. Si la profundidad local es menor que la global, el
directorio no se duplica; solamente se divide el bucket.

Esta regla evita crecer el directorio innecesariamente.

## 6. Profundidades e invariantes

Despues de cada split se deben cumplir estas condiciones:

- `len(directorio) == 2 ** profundidad_global`.
- `0 <= profundidad_local <= profundidad_global` para cada bucket.
- Un bucket con profundidad local `l` debe ser referenciado por
	`2 ** (profundidad_global - l)` posiciones del directorio.
- Cada registro debe encontrarse en el bucket que devuelve su hash.
- Ningun bucket debe superar `bucket_capacity` despues de una insercion
	exitosa.

Estas invariantes se verifican en los tests despues de cada insercion que
puede provocar un split.

## 7. Colisiones y overflow

Dos claves pueden producir el mismo indice en el directorio. El extendible
hashing intenta separarlas usando mas bits del hash mediante nuevos splits.

Si varias claves tienen exactamente el mismo hash, ningun split puede
separarlas. Continuar duplicando el directorio en ese caso provocaria un
crecimiento infinito. La implementacion detecta la situacion antes de
dividir y lanza:

```python
OverflowError("Las claves colisionan en todos los bits disponibles")
```

La excepcion es controlada: la clave que no pudo insertarse no aumenta la
cantidad de registros y las claves que ya estaban almacenadas siguen siendo
buscables.

Tambien existe un limite configurable `max_depth`. Si se alcanza ese limite,
la insercion falla con `OverflowError` en lugar de dejar una estructura
inconsistente.

## 8. Busqueda exacta

La operacion `buscar(clave)` realiza:

1. Calculo del hash.
2. Extraccion de los bits menos significativos necesarios.
3. Una consulta al directorio.
4. Una consulta al diccionario del bucket seleccionado.

La complejidad esperada es $O(1)$ para la busqueda, insercion y eliminacion,
sin contar el costo de dividir o fusionar buckets. En una futura version con
paginas en disco, la busqueda exacta requiere aproximadamente un acceso al
directorio y un acceso al bucket; con el directorio residente en memoria, se
aproxima a un acceso de pagina.

La clase no implementa busquedas ordenadas ni rangos. Por eso expone:

```python
indice.supportsRange()   # False
indice.supports_range()  # False
```

El planner puede usar este contrato para no seleccionar este indice ante un
predicado de rango.

## 9. Eliminacion y merge de buckets

`eliminar(clave)` elimina el registro del bucket correspondiente y luego
intenta fusionarlo con su buddy.

Dos buckets son buddies cuando:

- tienen la misma profundidad local;
- se diferencian exactamente en el bit mas significativo de su direccion;
- la suma de sus registros cabe en un solo bucket.

Si se cumplen esas condiciones, los registros se combinan y la profundidad
local del bucket resultante disminuye en uno. El proceso puede repetirse con
el nuevo bucket resultante.

Despues del merge, el directorio se reduce mientras no exista ningun bucket
con profundidad local igual a la profundidad global. Al reducirlo, se elimina
la mitad superior del directorio y la profundidad global disminuye en uno.

Esto permite que el indice crezca con las inserciones y vuelva a ocupar menos
espacio despues de muchas eliminaciones.

## 10. Interfaz publica

Ejemplo de uso:

```python
from motor.indices import ExtendibleHashing

indice = ExtendibleHashing[int, str](bucket_capacity=2)

indice.insertar(10, "diez")
indice.insertar(20, "veinte")

assert indice.buscar(10) == "diez"
assert indice.contiene(20)
assert indice.eliminar(10)
assert not indice.contiene(10)
assert indice.supportsRange() is False
```

Operaciones disponibles:

- `insertar(clave, valor)`: agrega un registro y devuelve `True`; devuelve
	`False` para claves duplicadas.
- `buscar(clave, default=None)`: devuelve el valor o `default` si no existe.
- `contiene(clave)`: indica si la clave esta almacenada.
- `eliminar(clave)`: elimina la clave y devuelve `True` si existia.
- `items()`: devuelve los registros almacenados.
- `estadisticas()`: devuelve cantidad de registros, profundidad global,
	cantidad de buckets y capacidad configurada.
- `supportsRange()`: devuelve `False` para integracion con el planner.

## 11. Metricas y logs de rendimiento

La clase registra metricas acumuladas mediante `metricas()`:

```python
metricas = indice.metricas()
```

El resultado incluye:

- `construccion_ms`: tiempo de `construir()` en milisegundos.
- `consultas`: cantidad de busquedas realizadas.
- `consulta_promedio_us`: tiempo promedio de consulta en microsegundos.
- `inserciones`: cantidad de llamadas a `insertar`.
- `insercion_promedio_us`: tiempo promedio de insercion.
- `eliminaciones`: cantidad de llamadas a `eliminar`.
- `eliminacion_promedio_us`: tiempo promedio de eliminacion.
- `espacio_adicional_bytes`: estimacion del espacio ocupado por el
	directorio, buckets, diccionarios, claves y valores.

Para medir la construccion completa se recomienda usar:

```python
indice = ExtendibleHashing.construir(
		((int(fila["Index"]), fila) for fila in csv.DictReader(archivo)),
		bucket_capacity=128,
)
```

La construccion emite un log `INFO` en el logger
`motor.indices.extendible_hash` con cantidad de registros, tiempo y espacio.
Las consultas, inserciones y eliminaciones se acumulan para medir tambien
cargas con actualizaciones frecuentes. `resetear_metricas()` reinicia esos
contadores operativos sin eliminar los registros ni el tiempo de construccion.

Por defecto, los eventos se guardan en:

```text
logs/extendible_hash.log
```

La ruta puede cambiarse al construir el indice:

```python
indice = ExtendibleHashing(log_path="tmp/mi-hash.log")
```

Para cerrar el archivo de forma explicita, especialmente en Windows o en
tests que cambian de ruta, se puede usar `cerrar_log(ruta)`.

## 12. Pruebas implementadas

La suite se encuentra en `motor/pruebas/test_extendible_hash.py` y cubre:

1. Insercion, busqueda y rechazo de claves duplicadas.
2. Split de buckets y redistribucion de registros.
3. Profundidad local/global y multiplicidad de referencias del directorio.
4. Colisiones controladas y manejo de `OverflowError`.
5. Eliminacion, merge de buddies y reduccion del directorio.
6. Contrato `supportsRange() == False`.
7. Validacion de parametros y estadisticas.
8. Insercion de 100 000 claves y crecimiento del directorio.
9. Construccion desde `organizations-100000.csv`, consultas y 1 000 ciclos de
	eliminacion/reinsercion.

El test de escala utiliza buckets de capacidad 64. Con 100 000 claves se
verifica que el directorio crezca, que tenga exactamente `2^global_depth`
posiciones y que las primeras, intermedias y ultimas claves puedan buscarse.

## 13. Como ejecutar las pruebas

Desde la raiz del proyecto, en PowerShell:

```powershell
python -m unittest discover -s motor/pruebas -p "test_*.py" -v
```

Para ejecutar solo el benchmark de integracion con el CSV:

```powershell
python -m unittest motor.pruebas.test_extendible_hash_csv -v
```

Una ejecucion exitosa termina con todos los tests en estado `ok` y una salida
similar a:

```text
Ran 7 tests in 0.256s

OK
```

Tambien se puede comprobar la sintaxis con:

```powershell
python -m compileall -q motor/indices motor/pruebas
```

## 14. Resumen para la reunion

La implementacion cumple el flujo principal de un indice extendible hashing:

- organiza los registros en un directorio y buckets;
- utiliza profundidad global y local;
- divide buckets llenos y redistribuye registros;
- duplica el directorio solo cuando la profundidad local lo requiere;
- detecta colisiones que no pueden separarse;
- evita loops infinitos mediante `OverflowError` controlado;
- busca claves exactas en tiempo esperado constante;
- fusiona buckets buddy despues de eliminar;
- reduce la profundidad global cuando el directorio ya no necesita crecer;
- informa al planner que no soporta rangos;
- valida el crecimiento con 100 000 claves;
- mide construccion, consultas, inserciones, eliminaciones y espacio
	adicional;
- se prueba con el CSV real de organizaciones.

La implementacion actual es en memoria. La siguiente etapa natural seria
conectar el directorio y los buckets con el administrador de paginas para
persistirlos en disco, manteniendo la misma interfaz publica.

## Estructura

El indice mantiene:

- un directorio de referencias a buckets;
- una profundidad global para calcular el indice del directorio;
- una profundidad local por bucket;
- un limite de registros por bucket.

La consulta usa los bits menos significativos del hash. Cuando un bucket se
llena, se divide y sus registros se redistribuyen. Si su profundidad local es
igual a la global, primero se duplica el directorio. Al eliminar registros,
los buckets vecinos se fusionan cuando la suma de sus registros cabe en un
solo bucket y el directorio se reduce cuando ya no necesita su mitad superior.

Las claves duplicadas no reemplazan el valor existente: `insertar` devuelve
`False`. Las colisiones que no pueden separarse con ningun bit disponible
producen `OverflowError`; esto evita un crecimiento infinito del directorio.
El acceso exacto calcula un unico indice de directorio y consulta un solo
bucket, por lo que requiere aproximadamente un acceso de pagina cuando la
estructura se encuentra almacenada en disco. El indice no soporta rangos:
`supportsRange()` devuelve `False` para que el planner no lo seleccione ante
predicados de rango.

## Pruebas

Desde la raiz del proyecto:

```powershell
python -m unittest motor.pruebas.test_extendible_hash -v
```

Las pruebas cubren insercion, busqueda, claves duplicadas, crecimiento del
directorio, redistribucion, colisiones controladas, eliminacion, fusion,
contraccion, invariantes de profundidad local/global, validacion de parametros,
estadisticas, rechazo de rangos y crecimiento con 100000 claves.