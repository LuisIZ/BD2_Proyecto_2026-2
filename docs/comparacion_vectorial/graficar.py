"""Genera las graficas de la comparacion vectorial a partir de datos/resultados/vectorial_bench.csv
(api/multimedia/bench_vectorial.py --motor con los vectores reales de 1k, 10k y 100k).

    python docs/comparacion_vectorial/graficar.py

Salida: docs/comparacion_vectorial/graficas/*.png
"""

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent.parent
SALIDA = Path(__file__).resolve().parent / "graficas"

COLOR = {"motor_secuencial": "#1baf7a", "secuencial_exact": "#555555", "IVF": "#2a78d6", "HNSW": "#eb6834"}
NOMBRE = {"motor_secuencial": "k-NN secuencial (motor)", "secuencial_exact": "Flat exacto (FAISS)",
          "IVF": "IVF (FAISS)", "HNSW": "HNSW (FAISS)"}
METRICA = {"cosine": "coseno", "euclidean": "euclidiana"}
# configuración que se muestra en las gráficas contra n: la más barata con recall alto
IVF_ELEGIDO = "nprobe=8"
HNSW_ELEGIDO = "ef_busqueda=64"
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
    with open(RAIZ / "datos" / "resultados" / "vectorial_bench.csv", encoding="utf-8", newline="") as f:
        filas = list(csv.DictReader(f))
    for r in filas:
        r["n"] = int(r["n"])
        for campo in ("construccion_ms", "consulta_ms", "recall_at_10", "disco_index_bytes"):
            r[campo] = float(r[campo])
    return filas


def elegida(r):
    """La fila que representa a cada técnica en las gráficas contra n."""
    if r["indice"] == "IVF":
        return r["parametros"].endswith(IVF_ELEGIDO)
    if r["indice"] == "HNSW":
        return r["parametros"].endswith(HNSW_ELEGIDO)
    return True


def guardar(fig, ax, titulo, archivo):
    ax.set_title(titulo, loc="left", fontweight="bold")
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.2), ncol=2, fontsize=9)
    fig.tight_layout()
    fig.savefig(SALIDA / archivo, dpi=150)
    plt.close(fig)


def contra_n(filas, metrica, campo, indices, titulo, eje, archivo, escala=1.0):
    fig, ax = plt.subplots(figsize=(6.4, 4.6))
    for indice in indices:
        puntos = sorted((r["n"], r[campo] / escala) for r in filas
                        if r["metrica"] == metrica and r["indice"] == indice and elegida(r))
        if not puntos:
            continue
        ax.plot(*zip(*puntos), marker="o", linewidth=2, color=COLOR[indice], label=NOMBRE[indice])
        for x, y in puntos:
            ax.annotate(f"{y:,.2f}", (x, y), textcoords="offset points", xytext=(4, 4), fontsize=8,
                        color=COLOR[indice])
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xticks([1000, 10000, 100000])
    ax.get_xaxis().set_major_formatter(plt.FuncFormatter(lambda v, _: f"{int(v):,}".replace(",", " ")))
    ax.set_xlabel("vectores")
    ax.set_ylabel(eje)
    guardar(fig, ax, titulo, archivo)


def recall_tiempo(filas, metrica, n, archivo):
    """Curva de recall contra tiempo al variar nprobe (IVF) y ef_busqueda (HNSW)."""
    fig, ax = plt.subplots(figsize=(6.4, 4.6))
    for indice, parametro in (("IVF", "nprobe"), ("HNSW", "ef_busqueda")):
        puntos = sorted((r["consulta_ms"], r["recall_at_10"], r["parametros"].split(f"{parametro}=")[1])
                        for r in filas if r["metrica"] == metrica and r["n"] == n and r["indice"] == indice)
        ax.plot([p[0] for p in puntos], [p[1] for p in puntos], marker="o", linewidth=2,
                color=COLOR[indice], label=f"{NOMBRE[indice]}, variando {parametro}")
        for x, y, valor in puntos:
            ax.annotate(valor, (x, y), textcoords="offset points", xytext=(3, -11), fontsize=7, color=COLOR[indice])
    for indice in ("secuencial_exact", "motor_secuencial"):
        for r in filas:
            if r["metrica"] == metrica and r["n"] == n and r["indice"] == indice:
                ax.scatter(r["consulta_ms"], r["recall_at_10"], s=60, marker="D", color=COLOR[indice],
                           label=NOMBRE[indice], zorder=3)
    ax.set_xscale("log")
    ax.set_xlabel("ms por consulta (escala log)")
    ax.set_ylabel("Recall@10")
    ax.set_ylim(0, 1.05)
    vectores = f"{n:,}".replace(",", " ")
    guardar(fig, ax, f"Recall contra tiempo, {vectores} vectores, {METRICA[metrica]}", archivo)


def main():
    SALIDA.mkdir(parents=True, exist_ok=True)
    filas = leer()
    n_max = max(r["n"] for r in filas)
    todas = ("motor_secuencial", "secuencial_exact", "IVF", "HNSW")
    for metrica in ("cosine", "euclidean"):
        nombre = METRICA[metrica]
        contra_n(filas, metrica, "consulta_ms", todas, f"Consulta k = 10, {nombre}",
                 "ms por consulta (escala log)", f"consulta_{nombre}.png")
        recall_tiempo(filas, metrica, n_max, f"recall_tiempo_{nombre}.png")
    contra_n(filas, "cosine", "construccion_ms", ("IVF", "HNSW"), "Construcción del índice, coseno",
             "ms (escala log)", "construccion.png")
    contra_n(filas, "cosine", "disco_index_bytes", ("secuencial_exact", "IVF", "HNSW"),
             "Tamaño del índice en disco, coseno", "MB (escala log)", "tamano.png", escala=2**20)
    print(f"graficas en {SALIDA}")


if __name__ == "__main__":
    main()
