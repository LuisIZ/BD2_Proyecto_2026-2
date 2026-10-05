"""Compara el R-Tree del motor con PostGIS geography/GiST.

Requiere datos/resultados/espacial_haversine_bench.csv generado localmente y
datos/resultados/postgis_bench.csv exportado desde postgis_bench.sql.
"""

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

RAIZ = Path(__file__).resolve().parents[2]
RESULTADOS = RAIZ / "datos" / "resultados"
SALIDA = Path(__file__).resolve().parent / "graficas"
COLORES = {
    "secuencial": "#eb6834",
    "rtree": "#2a78d6",
    "postgis": "#1baf7a",
}
NOMBRES = {
    "secuencial": "Secuencial",
    "rtree": "R-Tree (motor)",
    "postgis": "PostGIS GiST",
}


def leer():
    filas = []
    for archivo in ("espacial_haversine_bench.csv", "postgis_bench.csv"):
        ruta = RESULTADOS / archivo
        if not ruta.exists():
            raise FileNotFoundError(f"Falta {ruta}; consulta las instrucciones del README.")
        with ruta.open(newline="", encoding="utf-8-sig") as f:
            for fila in csv.DictReader(f):
                fila["n"] = int(fila["n"])
                fila["parametro"] = int(fila["parametro"])
                fila["tiempo_ms"] = float(fila["tiempo_ms"])
                filas.append(fila)
    return filas


def graficar(filas, operacion, parametro, titulo, eje_x, nombre):
    datos = [
        fila
        for fila in filas
        if fila["operacion"] == operacion
        and (parametro is None or fila["parametro"] == parametro)
    ]
    if not datos:
        raise ValueError(f"No hay mediciones para {operacion} con parámetro {parametro}.")
    ns = sorted({fila["n"] for fila in datos})
    estructuras = ["secuencial", "rtree", "postgis"]
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for estructura in estructuras:
        puntos = sorted(
            (fila for fila in datos if fila["estructura"] == estructura),
            key=lambda fila: fila["n"] if parametro is not None else fila["parametro"],
        )
        if not puntos:
            continue
        x = [fila["n"] if parametro is not None else fila["parametro"] for fila in puntos]
        y = [fila["tiempo_ms"] for fila in puntos]
        ax.plot(x, y, marker="o", linewidth=2, color=COLORES[estructura], label=NOMBRES[estructura])
    ax.set_yscale("log")
    if parametro is not None:
        ax.set_xscale("log")
        ax.set_xticks(ns, [f"{n:,}".replace(",", " ") for n in ns])
        ax.set_xlabel("Puntos")
    else:
        ax.set_xlabel(eje_x)
    ax.set_ylabel("Tiempo promedio (ms, escala log)")
    ax.set_title(titulo, loc="left", fontweight="bold")
    ax.grid(True, color="#e6e5e2")
    ax.legend()
    fig.tight_layout()
    fig.savefig(SALIDA / nombre, dpi=150)
    plt.close(fig)


def main():
    filas = leer()
    disponibles = {
        (fila["estructura"], fila["n"], fila["operacion"], fila["parametro"])
        for fila in filas
    }
    esperados = {
        (estructura, n, operacion, parametro)
        for estructura in ("secuencial", "rtree", "postgis")
        for n in (1000, 10000, 100000)
        for operacion, parametros in (("rango", (1000, 5000, 10000)), ("knn", (10, 50, 100)))
        for parametro in parametros
    }
    faltantes = sorted(esperados - disponibles)
    if faltantes:
        raise ValueError(f"Faltan combinaciones de benchmark: {faltantes[:5]}")
    SALIDA.mkdir(parents=True, exist_ok=True)
    for radio in (1000, 5000, 10000):
        graficar(
            filas,
            "rango",
            radio,
            f"Rango geográfico: {radio // 1000} km",
            "Radio (m)",
            f"postgis_rango_{radio}.png",
        )
    for k in (10, 50, 100):
        graficar(
            filas,
            "knn",
            k,
            f"k-NN geográfico: k={k}",
            "k",
            f"postgis_knn_{k}.png",
        )
    print(f"Gráficas PostGIS en {SALIDA}")


if __name__ == "__main__":
    main()
