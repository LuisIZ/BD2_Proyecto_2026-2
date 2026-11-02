"""Genera las graficas de la comparacion de texto a partir de
datos/resultados/texto_bench.csv (make bench-texto), datos/resultados/gin_bench.csv
(datos/resultados/gin_bench.sql) y datos/resultados/texto_relevancia.csv (relevancia.py).

    python docs/comparacion_texto/graficar.py

Salida: docs/comparacion_texto/graficas/*.png
"""

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent.parent
RESULTADOS = RAIZ / "datos" / "resultados"
SALIDA = Path(__file__).resolve().parent / "graficas"

COLOR = {"spimi": "#2a78d6", "tf_idf": "#2a78d6", "bm25": "#1baf7a", "gin": "#eb6834"}
NOMBRE = {"spimi": "SPIMI (motor)", "tf_idf": "TF-IDF (motor)", "bm25": "BM25 (motor)", "gin": "GIN (PostgreSQL)"}
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


def leer(nombre):
    with open(RESULTADOS / nombre, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def valor(filas, estructura, operacion, n, palabras="", campo="tiempo_ms"):
    for r in filas:
        if (r["estructura"] == estructura and r["operacion"] == operacion and int(r["n"]) == n
                and (r["palabras"] or "") == str(palabras)):
            return float(r[campo])
    return None


def formato_n(ax):
    ax.set_xscale("log")
    ax.set_xticks([1000, 10000, 100000])
    ax.get_xaxis().set_major_formatter(plt.FuncFormatter(lambda v, _: f"{int(v):,}".replace(",", " ")))


def guardar(fig, ax, titulo, archivo):
    ax.set_title(titulo, loc="left", fontweight="bold")
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.2), ncol=3, fontsize=9)
    fig.tight_layout()
    fig.savefig(SALIDA / archivo, dpi=150)
    plt.close(fig)


def construccion(filas):
    """Tiempo de construir el índice y su tamaño en disco, contra n."""
    tamanos = [1000, 10000, 100000]
    for campo, titulo, eje, archivo, escala in (
        ("tiempo_ms", "Construcción del índice", "ms (escala log)", "01_construccion.png", 1),
        ("bytes", "Tamaño del índice en disco", "MB (escala log)", "02_tamano.png", 2**20),
    ):
        fig, ax = plt.subplots(figsize=(6.4, 4.4))
        for e in ("spimi", "gin"):
            y = [valor(filas, e, "construccion", n, campo=campo) for n in tamanos]
            puntos = [(n, v / escala) for n, v in zip(tamanos, y) if v is not None]
            ax.plot(*zip(*puntos), marker="o", linewidth=2, color=COLOR[e], label=NOMBRE[e])
            for x, v in puntos:
                ax.annotate(f"{v:,.1f}", (x, v), textcoords="offset points", xytext=(4, 4), fontsize=8, color=COLOR[e])
        formato_n(ax)
        ax.set_yscale("log")
        ax.set_xlabel("productos")
        ax.set_ylabel(eje)
        guardar(fig, ax, titulo, archivo)


def consultas(filas):
    """Tiempo por consulta según el número de palabras, con 100 000 productos, y contra n."""
    tecnicas = ("tf_idf", "bm25", "gin")
    fig, ax = plt.subplots(figsize=(6.4, 4.4))
    ancho = 0.26
    for i, e in enumerate(tecnicas):
        y = [valor(filas, e, "consulta", 100000, p) for p in (1, 3, 5)]
        x = [j + (i - 1) * ancho for j in range(3)]
        barras = ax.bar(x, y, ancho, color=COLOR[e], label=NOMBRE[e])
        ax.bar_label(barras, fmt="%.1f", fontsize=8, padding=2)
    ax.set_xticks(range(3), ["1 palabra", "3 palabras", "5+ palabras"])
    ax.set_ylabel("ms por consulta")
    guardar(fig, ax, "Consulta top 10, 100 000 productos", "03_consulta_palabras.png")

    fig, ax = plt.subplots(figsize=(6.4, 4.4))
    for e in tecnicas:
        y = [valor(filas, e, "consulta", n, 3) for n in (1000, 10000, 100000)]
        ax.plot([1000, 10000, 100000], y, marker="o", linewidth=2, color=COLOR[e], label=NOMBRE[e])
    formato_n(ax)
    ax.set_yscale("log")
    ax.set_xlabel("productos")
    ax.set_ylabel("ms por consulta (escala log)")
    guardar(fig, ax, "Consulta de 3 palabras según el tamaño", "04_consulta_tamano.png")


def relevancia():
    """Precision@10 y Recall@10 promedio de las 30 consultas."""
    filas = leer("texto_relevancia.csv")
    tecnicas = [t for t in ("tf_idf", "bm25", "gin") if any(f["tecnica"] == t for f in filas)]
    fig, ax = plt.subplots(figsize=(6.4, 4.4))
    ancho = 0.26
    for i, e in enumerate(tecnicas):
        mias = [f for f in filas if f["tecnica"] == e]
        y = [sum(float(f[m]) for f in mias) / len(mias) for m in ("p10", "r10")]
        barras = ax.bar([j + (i - (len(tecnicas) - 1) / 2) * ancho for j in range(2)], y, ancho,
                        color=COLOR[e], label=NOMBRE[e])
        ax.bar_label(barras, fmt="%.3f", fontsize=8, padding=2)
    maximo = sum(float(f["r10_max"]) for f in filas if f["tecnica"] == tecnicas[0]) / 30
    ax.axhline(maximo, xmin=0.55, xmax=0.95, color="#7a847d", linestyle="--", linewidth=1)
    ax.annotate(f"R@10 máximo posible {maximo:.3f}", (1, maximo), textcoords="offset points", xytext=(0, 4),
                ha="center", fontsize=8, color="#5b655e")
    ax.set_xticks(range(2), ["Precision@10", "Recall@10"])
    ax.set_ylim(0, 1)
    guardar(fig, ax, "Calidad del ranking, 30 consultas sobre 10 000 productos", "05_relevancia.png")


def main():
    SALIDA.mkdir(parents=True, exist_ok=True)
    filas = leer("texto_bench.csv") + leer("gin_bench.csv")
    construccion(filas)
    consultas(filas)
    relevancia()
    print(f"graficas en {SALIDA}")


if __name__ == "__main__":
    main()
