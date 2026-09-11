# Implementacion Sequential File (Paged)

## 1. Objetivo

Se implemento una organizacion de archivos secuencial paginada para el motor de base de datos. La idea principal es mantener los registros ordenados por clave, dividirlos en paginas de capacidad fija y usar un area auxiliar cuando una pagina esta llena.

La implementacion se encuentra en:

- `motor/comun/archivo.h`: interfaz comun y tipos compartidos.
- `motor/archivos/sequential_file.h`: declaracion de `SequentialFile`.
- `motor/archivos/sequential_file.cpp`: logica de insercion, eliminacion, consulta y reorganizacion.
- `motor/pruebas/sequential_file_test.cpp`: prueba de los criterios principales.

## 2. Interfaz comun

Se creo la interfaz `IFileOrganization` para que las diferentes organizaciones de archivos puedan ser intercambiables.

La interfaz define estas operaciones:

- `insertar(registro)`: agrega un registro y evita claves duplicadas.
- `eliminar(clave)`: elimina logicamente un registro.
- `scan()`: devuelve todos los registros activos en orden.
- `stats()`: devuelve estadisticas del archivo.
- `reorganizar()`: reconstruye las paginas y elimina el desperdicio acumulado.

Esto permite que un futuro Heap File implemente la misma interfaz y que ambos puedan utilizar la misma bateria de pruebas y compararse directamente.

## 3. Estructura interna

Cada registro tiene una clave entera y un valor de texto:

```cpp
struct Registro {
    int clave;
    std::string valor;
};
```

El Sequential File utiliza:

- **Paginas principales**: contienen registros ordenados.
- **Slots**: guardan un registro y una marca `tumba`.
- **Area auxiliar**: contiene temporalmente los registros que no caben en las paginas principales.
- **Umbral de reorganizacion**: porcentaje maximo de desperdicio permitido.

La capacidad de pagina se configura al crear el archivo. En la prueba se utilizan paginas de capacidad 2 para provocar overflow facilmente.

## 4. Insercion ordenada

Cuando se inserta un registro:

1. Se verifica que la clave no exista.
2. Se busca la pagina destino mediante busqueda binaria.
3. Si la pagina tiene espacio, el registro se inserta en su posicion ordenada.
4. Si la pagina esta llena, el registro se agrega al area auxiliar, tambien ordenada.
5. Se revisa si debe ejecutarse una reorganizacion.

La busqueda binaria compara la clave con el ultimo registro de cada pagina. Esto reduce la cantidad de paginas que deben revisarse para ubicar el punto de insercion.

El area auxiliar evita desplazar inmediatamente todos los registros cuando una pagina se llena. Por eso el orden logico se mantiene aunque fisicamente existan registros en dos zonas.

## 5. Eliminacion lazy con tumbas

La eliminacion no borra fisicamente el slot. En su lugar, establece:

```cpp
slot.tumba = true;
```

Una tumba conserva la posicion ocupada y permite realizar eliminaciones de forma rapida. Los registros marcados como tumbas no aparecen en `scan()`.

Ademas, `stats()` cuenta las tumbas y calcula el desperdicio:

```text
porcentaje de desperdicio = cantidad de tumbas / cantidad total de slots
```

Por ejemplo, si hay 1 tumba entre 5 slots, el desperdicio reportado es 20%.

## 6. Reorganizacion automatica

Cada insercion o eliminacion revisa el porcentaje de desperdicio. Si supera el umbral configurado, se ejecuta `reorganizar()` automaticamente.

La reorganizacion realiza estas acciones:

1. Ejecuta un recorrido logico con `scan()`.
2. Descarta tumbas y slots auxiliares ya eliminados.
3. Limpia las paginas existentes.
4. Distribuye nuevamente los registros ordenados en paginas llenas de forma secuencial.
5. Vacía el area auxiliar.
6. Mide el tiempo transcurrido en microsegundos.

El umbral por defecto es 30%, pero puede cambiarse con:

```cpp
archivo.set_umbral_reorganizacion(0.30);
```

El tiempo de la ultima reorganizacion queda disponible en:

```cpp
archivo.stats().ultima_reorganizacion_us
```

## 7. `scan()` y orden global

`scan()` recoge los registros activos de las paginas principales y del area auxiliar. Luego combina ambas colecciones con `std::merge` para devolver un solo resultado ordenado por clave.

Esto garantiza que el usuario vea el orden logico completo, aunque algunos registros todavia esten en overflow.

## 8. Prueba demostrativa

La prueba realiza este flujo:

1. Inserta las claves 2, 1, 4, 3 y 5.
2. Verifica que `scan()` devuelva 1, 2, 3, 4 y 5.
3. Verifica que los registros que no caben aparezcan en el overflow.
4. Intenta insertar una clave duplicada y confirma que se rechace.
5. Elimina la clave 2 y verifica la creacion de una tumba.
6. Cambia el umbral a 30%.
7. Elimina la clave 4 y provoca la reorganizacion automatica.
8. Verifica que no queden tumbas, que el overflow este vacio y que se haya medido el tiempo.
9. Confirma que el resultado final sea 1, 3 y 5.

Una salida representativa es:

```text
Registros ordenados: (1, uno) (2, dos) (3, tres) (4, cuatro) (5, cinco)
Stats: registros=5, tumbas=0, paginas=1, overflow=3, desperdicio=0%

Despues de eliminar la clave 2:
Stats: registros=4, tumbas=1, paginas=1, overflow=3, desperdicio=20%

Despues de eliminar la clave 4 y reorganizar:
Stats: registros=3, tumbas=0, paginas=2, overflow=0, desperdicio=0%
ultima_reorganizacion=6 us

Registros ordenados: (1, uno) (3, tres) (5, cinco)
Prueba completada correctamente.
```

El tiempo exacto puede variar dependiendo del equipo.

## 9. Como ejecutar la prueba

Desde la raiz del proyecto, en PowerShell:

```powershell
New-Item -ItemType Directory -Force .build | Out-Null

g++ -std=c++17 -Wall -Wextra -pedantic `
  motor/archivos/sequential_file.cpp `
  motor/pruebas/sequential_file_test.cpp `
  -o .build/sequential_file_test.exe

.\.build\sequential_file_test.exe
```

Si la prueba termina sin errores de `assert`, los criterios verificados se cumplen. En esta version, los resultados tambien se imprimen en la terminal para facilitar la demostracion.

## 10. Resumen para la reunion

La implementacion cumple el flujo principal de un Sequential File paginado:

- mantiene la insercion ordenada;
- usa busqueda binaria para seleccionar la pagina;
- envia los registros adicionales al overflow;
- realiza eliminacion lazy mediante tumbas;
- reporta el porcentaje de desperdicio;
- reorganiza automaticamente al superar un umbral configurable;
- mide el tiempo de reorganizacion;
- entrega un `scan()` ordenado incluyendo el overflow;
- utiliza una interfaz comun que permite compararlo con Heap File.

La implementacion actual trabaja en memoria y sirve como base de la organizacion logica. La siguiente etapa natural seria conectar las paginas con el administrador de paginas y persistirlas en disco, manteniendo esta misma interfaz publica.
