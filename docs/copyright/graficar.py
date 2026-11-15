import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

CARPETA = Path(__file__).resolve().parent
SUPERFICIE = "#fcfcfb"
GRILLA = "#e6e5e2"
TEXTO = "#0b0b0b"
TEXTO_SECUNDARIO = "#52514e"
SERIES = [("precision", "Precisión", "#2a78d6"), ("recall", "Recall", "#eb6834"), ("f1", "F1", "#1baf7a")]

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


def main():
    with open(CARPETA / "evaluacion.csv", encoding="utf-8", newline="") as archivo:
        filas = [{k: float(v) for k, v in fila.items()} for fila in csv.DictReader(archivo)]
    # el umbral con mayor f1; si empatan, el mas alto
    mejor = max(filas, key=lambda f: (f["f1"], f["umbral"]))
    umbrales = [f["umbral"] for f in filas]

    fig, ax = plt.subplots(figsize=(7.2, 4.4))
    for clave, nombre, color in SERIES:
        valores = [f[clave] for f in filas]
        ax.plot(umbrales, valores, color=color, linewidth=2, label=nombre)
        ax.annotate(nombre, (umbrales[2], valores[2]), textcoords="offset points", xytext=(0, 6),
                    fontsize=9, color=TEXTO_SECUNDARIO)
    ax.axvline(mejor["umbral"], color=TEXTO_SECUNDARIO, linewidth=1, linestyle="--")
    ax.annotate(f"umbral elegido: {mejor['umbral']:.0f} %\nF1 {mejor['f1']:.3f}",
                (mejor["umbral"], 0.08), textcoords="offset points", xytext=(6, 0), fontsize=9, color=TEXTO)
    ax.set_xlim(umbrales[0], umbrales[-1])
    ax.set_ylim(0, 1.02)
    ax.set_title("Detección de copyright: precisión, recall y F1 por umbral", loc="left", fontweight="bold",
                 color=TEXTO)
    ax.set_xlabel("umbral de similitud (%)", color=TEXTO_SECUNDARIO)
    ax.set_ylabel("valor", color=TEXTO_SECUNDARIO)
    ax.tick_params(colors=TEXTO_SECUNDARIO)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.16), ncol=3, fontsize=9)
    fig.tight_layout()
    fig.savefig(CARPETA / "evaluacion.png", dpi=150)
    plt.close(fig)
    print(f"escrito {CARPETA / 'evaluacion.png'} (umbral {mejor['umbral']:.0f})")


if __name__ == "__main__":
    main()
