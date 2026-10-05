"""Genera las graficas de la comparacion espacial a partir de
datos/resultados/espacial_bench.csv (lo escribe motor/pruebas/espacial_bench) y
datos/resultados/gist_bench.csv (salida de datos/resultados/gist_bench.sql).

    python3 docs/comparacion_espacial/graficar.py

Salida: docs/comparacion_espacial/graficas/*.png
"""

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent.parent
RESULTADOS = RAIZ / "datos" / "resultados"
SALIDA = Path(__file__).resolve().parent / "graficas"

COLOR = {"secuencial": "#eb6834", "rtree": "#2a78d6", "gist": "#1baf7a"}
NOMBRE = {"secuencial": "Búsqueda secuencial", "rtree": "R-Tree (motor)", "gist": "GiST (PostgreSQL)"}
SUPERFICIE = "#fcfcfb"
GRILLA = "#e6e5e2"

plt.rcParams.update({
    "figure.facecolor": SUPERFICIE,
    "axes.facecolor": SUPERFICIE,
    "axes.edgecolor": GRILLA,
    "font.size": 10,
    "axes.grid": True,
    "grid.color": GRILLA,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "legend.frameon": False,
})


def leer():
    filas = []
    for nombre in ("espacial_bench.csv", "gist_bench.csv"):
        with open(RESULTADOS / nombre, newline="") as f:
            for r in csv.DictReader(f):
                r["n"] = int(r["n"])
                r["parametro"] = int(r["parametro"])
                r["tiempo_ms"] = float(r["tiempo_ms"])
                filas.append(r)
    return filas


def serie(filas, estructura, operacion, n=None):
    puntos = [r for r in filas if r["estructura"] == estructura and r["operacion"] == operacion and (n is None or r["n"] == n)]
    clave = "n" if n is None else "parametro"
    puntos.sort(key=lambda r: r[clave])
    return [r[clave] for r in puntos], [r["tiempo_ms"] for r in puntos]


def grafica(filas, operacion, estructuras, titulo, eje_x, archivo, n=None, log_x=False):
    fig, ax = plt.subplots(figsize=(6.4, 4.4))
    for e in estructuras:
        x, y = serie(filas, e, operacion, n)
        ax.plot(x, y, marker="o", color=COLOR[e], label=NOMBRE[e], linewidth=2)
        for xi, yi in zip(x, y):
            ax.annotate(f"{yi:.2f}", (xi, yi), textcoords="offset points", xytext=(4, 4), fontsize=8, color=COLOR[e])
    ax.set_yscale("log")
    if log_x:
        ax.set_xscale("log")
    ax.set_xticks(sorted({xi for e in estructuras for xi in serie(filas, e, operacion, n)[0]}))
    ax.get_xaxis().set_major_formatter(plt.FuncFormatter(lambda v, _: f"{int(v):,}".replace(",", " ")))
    ax.set_title(titulo, loc="left", fontweight="bold")
    ax.set_xlabel(eje_x)
    ax.set_ylabel("ms (escala log)")
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.2), ncol=3, fontsize=9)
    fig.tight_layout()
    fig.savefig(SALIDA / archivo, dpi=150)
    plt.close(fig)


def main():
    SALIDA.mkdir(parents=True, exist_ok=True)
    filas = leer()
    grafica(filas, "construccion", ["rtree", "gist"], "Construcción del índice", "puntos", "01_construccion.png", log_x=True)
    grafica(filas, "rango", ["secuencial", "rtree", "gist"], "Rango por radio, 100 000 puntos", "radio (m)", "02_rango.png", n=100000)
    grafica(filas, "knn", ["secuencial", "rtree", "gist"], "k vecinos más cercanos, 100 000 puntos", "k", "03_knn.png", n=100000)
    print(f"graficas en {SALIDA}")


if __name__ == "__main__":
    main()
