"""Compara B+ agrupado contra heap + B+ no agrupado a través del motor SQL.

Responde la pregunta de por qué el agrupado puede salir más lento: mide por
separado construcción, búsqueda puntual, rango y recorrido completo, y anota
cuántas páginas lee cada uno. Se corre desde la raíz del repo:

    python3 api/bench_agrupado.py [--n 1000,10000,100000] [--repeticiones 5]
    make bench-agrupado

Escribe datos/resultados/bplus_agrupado_vs_no_agrupado.csv.
"""

import argparse
import shutil
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from api.motor_cli import MotorSQL, RAIZ  # noqa: E402

DB = RAIZ / ".build" / "db_bench_agrupado"
SALIDA = RAIZ / "datos" / "resultados" / "bplus_agrupado_vs_no_agrupado.csv"

# (nombre, sql) por tabla. El rango toma el 3% de las filas, como en la web.
CONSULTAS = [
    ("busqueda_puntual", "SELECT * FROM {t} WHERE Index = {mitad};"),
    ("rango_3pct", "SELECT Index, Name FROM {t} WHERE Index BETWEEN {a} AND {b};"),
    ("recorrido_completo", "SELECT COUNT(*) FROM {t} WHERE Founded > 0;"),
    ("orden_por_texto", "SELECT Name FROM {t} ORDER BY Name LIMIT 20;"),
]


def paginas(resultado):
    """Páginas leídas por la consulta, sumando índice y datos."""
    total = 0
    for paso in resultado.get("plan", []):
        for campo in ("paginas_leidas", "paginas_indice", "paginas_heap"):
            total += int(paso.get(campo, 0) or 0)
    return total


def una(motor, sql):
    respuesta = motor.ejecutar(sql)
    resultado = respuesta[0] if isinstance(respuesta, list) else respuesta
    if not resultado.get("ok"):
        raise SystemExit(f"falló: {sql}\n  {resultado.get('error')}")
    return resultado


def medir(motor, sql, repeticiones):
    """Mediana del tiempo; las páginas no varían entre repeticiones."""
    muestras = [una(motor, sql) for _ in range(repeticiones)]
    return statistics.median(m["tiempo_ms"] for m in muestras), paginas(muestras[-1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--n", default="1000,10000,100000")
    parser.add_argument("--repeticiones", type=int, default=5)
    opciones = parser.parse_args()

    shutil.rmtree(DB, ignore_errors=True)
    DB.mkdir(parents=True, exist_ok=True)
    SALIDA.parent.mkdir(parents=True, exist_ok=True)
    motor = MotorSQL(db=str(DB))

    filas = [("n", "estructura", "operacion", "tiempo_ms", "paginas_leidas", "paginas_tabla", "bytes")]
    for n in [int(x) for x in opciones.n.split(",")]:
        csv = f"datos/organizations-{n}.csv"
        if not (RAIZ / csv).is_file():
            print(f"falta {csv}, se omite n={n}")
            continue

        print(f"\n=== {n} filas " + "=" * 42)
        construccion = {}
        # el agrupado se construye con carga masiva; el otro, heap más índice
        construccion["bplus_agrupado"] = una(
            motor, f"CREATE TABLE ag_{n} FROM FILE '{csv}' USING BPLUS;"
        )["tiempo_ms"]
        heap = una(motor, f"CREATE TABLE no_{n} FROM FILE '{csv}' USING HEAP;")["tiempo_ms"]
        indice = una(motor, f"CREATE INDEX no_{n}_idx ON no_{n} (Index) USING BPLUS;")["tiempo_ms"]
        construccion["heap_bplus_no_agrupado"] = heap + indice

        tablas = una(motor, "SHOW TABLES;")
        estadisticas = {f[0]: f for f in tablas["filas"]}

        for etiqueta, tabla in (("bplus_agrupado", f"ag_{n}"), ("heap_bplus_no_agrupado", f"no_{n}")):
            fila = estadisticas[tabla]
            paginas_tabla, bytes_tabla = fila[5], fila[6]
            filas.append((n, etiqueta, "construccion", f"{construccion[etiqueta]:.3f}", "", paginas_tabla, bytes_tabla))
            print(f"  {etiqueta:24} construccion {construccion[etiqueta]:9.1f} ms   {paginas_tabla:>7} pags  {bytes_tabla:>10} B")

            for operacion, plantilla in CONSULTAS:
                sql = plantilla.format(t=tabla, mitad=n // 2, a=n // 10, b=n // 10 + max(1, n * 3 // 100))
                ms, leidas = medir(motor, sql, opciones.repeticiones)
                filas.append((n, etiqueta, operacion, f"{ms:.3f}", leidas, paginas_tabla, bytes_tabla))
                print(f"  {etiqueta:24} {operacion:18} {ms:9.3f} ms   {leidas:>7} pags leidas")

    with SALIDA.open("w", encoding="utf-8", newline="") as archivo:
        for fila in filas:
            archivo.write(",".join(str(x) for x in fila) + "\n")
    print(f"\nresultados en {SALIDA.relative_to(RAIZ)}")


if __name__ == "__main__":
    main()
