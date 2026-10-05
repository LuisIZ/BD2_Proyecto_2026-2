# Transacciones y concurrencia

Sección 2.1.4 del enunciado. El motor agrupa sentencias en transacciones, controla el
acceso concurrente con locks y deshace los cambios de una transacción abortada.

## 1. Archivos

| Archivo | Qué es |
|---|---|
| `motor/transacciones/gestor_locks.h` | Gestor de locks S/X con 2PL estricto, detección de deadlock y registro de eventos. |
| `motor/consultas/ejecutor.{h,cpp}` | `BEGIN`, `END`, `ROLLBACK`, locks por sentencia y registro de undo. |
| `motor/pruebas/transacciones_test.cpp` | Pruebas del gestor y del ejecutor (incluye un deadlock real entre dos hilos). |
| `motor/pruebas/transacciones_demo.cpp` | Demo con hilos: `make demo-transacciones`. |

## 2. Sintaxis

```sql
BEGIN TRANSACTION;          -- también BEGIN
INSERT INTO cuentas VALUES (1, 100);
DELETE FROM cuentas WHERE id = 2;
END TRANSACTION;            -- también END o COMMIT: confirma
-- o bien
ROLLBACK;                   -- deshace todo lo hecho desde BEGIN
```

- Cada llamada al binario `motor_sql` es un proceso, así que una transacción vive dentro de
  un mismo lote de sentencias. Si el lote termina con la transacción abierta, el motor hace
  rollback automático y lo avisa en la respuesta.
- Dentro de una transacción no se permiten `CREATE`, `DROP` ni `COPY`.
- `BEGIN` dentro de otra transacción, o `END`/`ROLLBACK` sin transacción, son errores.
- Fuera de una transacción cada sentencia es su propia transacción (autocommit).

## 3. Control de concurrencia: 2PL estricto

Cada sentencia pide un lock sobre la tabla que usa antes de ejecutarse:

| Sentencia | Lock |
|---|---|
| `SELECT`, `EXPLAIN` | compartido (S) |
| `INSERT`, `DELETE`, DDL | exclusivo (X) |

Varias transacciones pueden tener S a la vez; X excluye a todas las demás. Una transacción
que ya tiene S puede subirlo a X si nadie más lo comparte. Dentro de una transacción los
locks se conservan hasta `END` o `ROLLBACK` (**2PL estricto**), así ninguna otra
transacción lee datos que todavía pueden deshacerse. La espera usa `std::condition_variable`,
sin espera activa.

La granularidad es la tabla. Es la opción más simple y suficiente para el motor, aunque
reduce la concurrencia frente a locks por registro.

Además, cuando varios hilos comparten la misma base, cada sentencia se ejecuta bajo un
*latch* del gestor (después de obtener su lock), para que dos hilos nunca escriban la misma
página a la vez. El latch protege los archivos; los locks protegen la consistencia de las
transacciones.

## 4. Deadlocks

Cuando una transacción tiene que esperar, el gestor arma el **grafo de espera** (quién espera
a quién) y busca un ciclo que pase por ella con una búsqueda en profundidad. Si lo encuentra,
elige como víctima a la transacción **más joven** del ciclo (la de mayor número), registra el
ciclo y el motivo, y la despierta con un error. El ejecutor de la víctima deshace sus cambios,
libera sus locks y devuelve:

```
deadlock: la transaccion T2 fue elegida como victima y se deshicieron sus cambios
```

El caso más común en la demo es el de dos lectores que quieren escribir: T1 y T2 tienen S
sobre `cuentas` y los dos piden X, así que cada uno espera al otro.

## 5. Rollback

Las páginas se escriben en disco en el momento, así que el rollback es un **undo lógico**:
cada `INSERT` y cada `DELETE` dentro de la transacción se anota, y `ROLLBACK` aplica las
operaciones inversas en orden contrario (un `INSERT` se deshace borrando por clave primaria
y un `DELETE` reinsertando la fila). Funciona igual en las tres organizaciones y mantiene los
índices secundarios, porque usa el mismo camino que un `INSERT` o `DELETE` normal.

```
BEGIN; INSERT ... (1 fila); DELETE ... (10 filas); SELECT COUNT(*)  -> 291
ROLLBACK                                          -> transaccion T1 deshecha (11 cambios)
SELECT COUNT(*)                                   -> 300
```

## 6. Demo con hilos

`make demo-transacciones` (o `.build/transacciones_demo --hilos 4 --depositos 5 --espera 5`)
lanza 4 hilos que hacen 5 depósitos de 100 cada uno sobre la misma cuenta. Cada depósito lee
el saldo, espera unos milisegundos y escribe el saldo nuevo con `DELETE` + `INSERT` (el
motor no tiene `UPDATE`). Se corre dos veces:

```
4 hilos x 5 depositos de 100 sobre la misma cuenta, 5 ms entre leer y escribir

sin transacciones:   esperado 2000, obtenido 500, depositos perdidos 15
con BEGIN/END y 2PL: esperado 2000, obtenido 2000, deadlocks detectados 31 (cada victima se deshizo y reintento)
```

- **Sin transacciones** cada sentencia es atómica, pero entre leer y escribir otro hilo lee
  el mismo saldo viejo y uno de los dos depósitos se pierde (*lost update*). En las corridas
  hechas se perdieron entre 13 y 15 de los 20 depósitos.
- **Con `BEGIN ... END`** el lock S de la lectura se mantiene hasta escribir, los conflictos
  terminan en deadlock, la víctima se deshace y reintenta, y el saldo final es exacto.

El número exacto de depósitos perdidos y de deadlocks cambia un poco entre corridas porque
depende de cómo el sistema operativo reparte los hilos; las esperas son fijas y la
conclusión no cambia: sin locks siempre se pierden depósitos y con locks el saldo siempre es
2000.

## 7. Eventos para el frontend

El gestor registra cada paso con su tiempo y la demo los guarda en
`datos/resultados/transacciones_eventos.json`:

```json
{"txn":1,"recurso":"cuentas","accion":"concede","modo":"S","detalle":"","ms":16.031},
{"txn":2,"recurso":"cuentas","accion":"deadlock","modo":"X","detalle":"ciclo T2 -> T1 -> T2; victima T2 por ser la mas joven","ms":25.469}
```

`accion` es `pide`, `concede`, `espera`, `deadlock`, `aborta` o `libera`, y alcanza para
dibujar una línea de tiempo por transacción.
