"""Genera las graficas de la comparacion de indices a partir de
datos/resultados/indices_bench.csv (lo escribe motor/pruebas/indices_bench).

    python3 docs/comparacion_indices/graficar.py

Salida: docs/comparacion_indices/graficas/*.png y docs/comparacion_indices/resultados.md
"""

import csv
import sys
import textwrap
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FuncFormatter  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent.parent
CSV = RAIZ / "datos" / "resultados" / "indices_bench.csv"
SALIDA = Path(__file__).resolve().parent / "graficas"

# paleta validada (dataviz): un color por estructura, siempre en este orden
COLOR = {
    "bplus_agrupado": "#2a78d6",
    "bplus_agrupado_masiva": "#86b6ef",  # variante del mismo azul
    "bplus_no_agrupado": "#eb6834",
    "hash_ram": "#1baf7a",
}
NOMBRE = {
    "bplus_agrupado": "B+ agrupado",
    "bplus_agrupado_masiva": "B+ agrupado (carga masiva)",
    "bplus_no_agrupado": "B+ no agrupado + heap",
    "hash_ram": "Hash extensible (RAM) + heap",
}
SUPERFICIE = "#fcfcfb"
TEXTO = "#0b0b0b"
TEXTO_2 = "#52514e"
GRILLA = "#e6e5e2"

plt.rcParams.update({
    "figure.facecolor": SUPERFICIE,
    "axes.facecolor": SUPERFICIE,
    "axes.edgecolor": GRILLA,
    "axes.labelcolor": TEXTO_2,
    "axes.titlecolor": TEXTO,
    "xtick.color": TEXTO_2,
    "ytick.color": TEXTO_2,
    "text.color": TEXTO,
    "font.size": 10,
    "axes.grid": True,
    "axes.grid.axis": "y",
    "grid.color": GRILLA,
    "grid.linewidth": 0.8,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.spines.left": False,
    "legend.frameon": False,
})


def leer():
    with open(CSV, newline="") as f:
        filas = list(csv.DictReader(f))
    for r in filas:
        for k, v in r.items():
            if k not in ("estructura",):
                r[k] = float(v)
    return filas


def formato(v):
    if v >= 1000:
        return f"{v:,.0f}"
    if v >= 100:
        return f"{v:.0f}"
    if v >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def etiqueta_n(n):
    return f"{int(n):,}".replace(",", " ")


def lineas(ax, series, valores, grupos, unidad="", discontinua=None, titulo=None, etiqueta_y=""):
    """Una línea por estructura; x = tamaños equiespaciados; y lineal desde cero; valor al final."""
    xs = list(range(len(grupos)))
    maximo = 0
    finales = []
    for serie in series:
        ys = [valores[serie].get(n, 0) for n in grupos]
        maximo = max(maximo, max(ys))
        ax.plot(xs, ys, color=COLOR[serie], linewidth=2, marker="o", markersize=7,
                markeredgecolor=SUPERFICIE, markeredgewidth=1.5, label=NOMBRE[serie],
                linestyle="--" if (discontinua or {}).get(serie) else "-", solid_capstyle="round")
        finales.append((ys[-1], serie))
    # valor al final de cada línea, separando etiquetas que caerían encima
    finales.sort()
    ultimo_y = None
    paso = maximo * 0.06
    for y, serie in finales:
        y_texto = y if ultimo_y is None or y - ultimo_y >= paso else ultimo_y + paso
        ax.annotate(formato(y) + unidad, (xs[-1], y), xytext=(8, 0), textcoords="offset points",
                    ha="left", va="center", fontsize=9, color=TEXTO,
                    xycoords=("data", "data"))
        if y_texto != y:
            ax.texts[-1].set_position((8, (y_texto - y) / maximo * ax.get_window_extent().height * 0.72 if maximo else 0))
        ultimo_y = y_texto
    ax.set_xticks(xs)
    ax.set_xticklabels([etiqueta_n(n) for n in grupos])
    ax.set_xlim(-0.15, len(grupos) - 1 + 0.75)
    ax.set_ylim(0, maximo * 1.15 if maximo > 0 else 1)
    ax.tick_params(axis="both", length=0)
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:,.0f}".replace(",", " ") if v >= 10 else f"{v:g}"))
    ax.set_xlabel("registros", color=TEXTO_2)
    if etiqueta_y:
        ax.set_ylabel(etiqueta_y, color=TEXTO)
    if titulo:
        ax.set_title(titulo, loc="left", fontsize=10, color=TEXTO)


def tabla(filas, clave, series, grupos):
    return {s: {r["n"]: r[clave] for r in filas if r["estructura"] == s and r["n"] in grupos} for s in series}


def figura_lineas(nombre, titulo, subtitulo, grupos, series, metricas, discontinua=None):
    """metricas: lista de (titulo_panel, etiqueta_y, valores, unidad); un panel por métrica."""
    ncols = len(metricas)
    ancho = 6.2 * ncols
    # el subtítulo se parte para que quepa en el ancho de la figura (~13 caracteres por pulgada a 9.5 pt)
    titulo = textwrap.fill(titulo, width=int(ancho * 9))
    subtitulo = textwrap.fill(subtitulo, width=int(ancho * 13))
    lineas_tit = titulo.count("\n") + 1
    lineas_sub = subtitulo.count("\n") + 1
    alto_titulo = 0.12 + 0.24 * lineas_tit
    cabecera = alto_titulo + 0.08 + 0.17 * lineas_sub + 0.2  # pulgadas
    # leyenda: tantas columnas como quepan (~2.6 pulgadas por entrada), el resto en filas
    por_fila = max(1, int(ancho // 2.6))
    filas_leyenda = -(-len(series) // por_fila)
    pie = 0.2 + 0.26 * filas_leyenda
    alto = 4.3 + cabecera + pie
    fig, axes = plt.subplots(1, ncols, figsize=(ancho, alto), squeeze=False)
    fig.text(0.01, 1 - 0.1 / alto, titulo, ha="left", va="top", fontsize=13, fontweight="bold", linespacing=1.3)
    fig.text(0.01, 1 - (alto_titulo + 0.08) / alto, subtitulo, ha="left", va="top", fontsize=9.5, color=TEXTO_2, linespacing=1.4)
    vistos = {}
    for ax, metrica in zip(axes[0], metricas):
        titulo_panel, etiqueta_y, valores, unidad = metrica[:4]
        propias = metrica[4] if len(metrica) > 4 else series
        lineas(ax, propias, valores, grupos, unidad, discontinua, titulo=titulo_panel, etiqueta_y=etiqueta_y)
        for m, e in zip(*ax.get_legend_handles_labels()):
            vistos.setdefault(e, m)
    orden = [NOMBRE[s] for s in series if NOMBRE[s] in vistos]
    fig.legend([vistos[e] for e in orden], orden, loc="lower left", bbox_to_anchor=(0.01, 0.0),
               ncol=min(por_fila, len(orden)), fontsize=9, labelspacing=0.9, columnspacing=1.6)
    fig.tight_layout(rect=(0, pie / alto, 1, 1 - (cabecera + 0.05) / alto))
    ruta = SALIDA / nombre
    fig.savefig(ruta, dpi=150)
    plt.close(fig)
    print("  ", ruta.relative_to(RAIZ))


def main():
    if not CSV.exists():
        sys.exit(f"no existe {CSV}; corre primero .build/indices_bench")
    SALIDA.mkdir(parents=True, exist_ok=True)
    todas = leer()
    filas = [r for r in todas if r["barajado"] == 0]
    grupos = sorted({r["n"] for r in filas})
    tres = ["bplus_agrupado", "bplus_no_agrupado", "hash_ram"]
    print("graficas:")
    scan = {"hash_ram": True}

    # 1. construccion
    v = tabla(filas, "construccion_ms", tres, grupos)
    v["bplus_agrupado_masiva"] = {r["n"]: r["construccion_masiva_ms"] for r in filas if r["estructura"] == "bplus_agrupado"}
    figura_lineas("01_construccion.png", "Tiempo de construcción del índice",
                  "ms para indexar n registros ya cargados; el agrupado escribe también los datos.",
                  grupos, ["bplus_agrupado", "bplus_agrupado_masiva", "bplus_no_agrupado", "hash_ram"],
                  [("Construcción", "ms", v, " ms")])

    # 2. igualdad
    pag = {s: {r["n"]: r["igualdad_pag_indice"] + r["igualdad_pag_datos"] for r in filas if r["estructura"] == s} for s in tres}
    figura_lineas("02_igualdad.png", "Búsqueda por igualdad exacta",
                  "Promedio de 1 000 claves al azar con caché fría. Páginas = índice + salto al heap; el hash vive en RAM y solo paga el heap.",
                  grupos, tres, [("Tiempo por búsqueda", "µs", tabla(filas, "igualdad_us", tres, grupos), " µs"),
                                 ("Páginas leídas por búsqueda", "páginas", pag, "")])

    # 3. rango
    dos = ["bplus_agrupado", "bplus_no_agrupado"]
    figura_lineas("03_rango.png", "Búsqueda por rango",
                  "Promedio de 100 rangos al azar, caché fría. El hash no soporta rango: su único camino es recorrer todo el heap (panel derecho, discontinua).",
                  grupos, tres, [("Rango de 100 claves", "µs", tabla(filas, "rango100_us", tres, grupos), " µs", dos),
                                 ("Rango de 1 000 claves", "µs", tabla(filas, "rango1000_us", tres, grupos), " µs", dos),
                                 ("Hash: scan del heap (cualquier rango)", "µs", tabla(filas, "rango100_us", tres, grupos), " µs", ["hash_ram"])],
                  discontinua=scan)

    # 4. ordenamiento
    figura_lineas("04_ordenamiento.png", "Ordenamiento: recorrer todos los registros en orden de clave",
                  "Agrupado: cadena de hojas. No agrupado: hojas + un salto al heap por registro. Hash: scan del heap, external merge sort y relectura.",
                  grupos, tres, [("Ordenamiento completo", "ms", tabla(filas, "ordenamiento_ms", tres, grupos), " ms")])

    # 5. espacio adicional
    kb = {s: {r["n"]: r["bytes_adicional"] / 1024 for r in filas if r["estructura"] == s} for s in tres}
    figura_lineas("05_espacio.png", "Espacio adicional sobre los datos crudos (180 B × n)",
                  "Agrupado: hojas al 50 % por inserción ordenada más nodos internos. No agrupado: archivo del índice. Hash: bytes en RAM.",
                  grupos, tres, [("Espacio adicional", "KB", kb, " KB")])

    # 6. inserciones y eliminaciones
    figura_lineas("06_insercion_eliminacion.png", "Inserciones y eliminaciones frecuentes",
                  "µs por operación sobre un lote del 10 % de n. No agrupado y hash actualizan índice y heap.",
                  grupos, tres, [("Inserción", "µs por registro", tabla(filas, "insercion_us", tres, grupos), " µs"),
                                 ("Eliminación, B+", "µs por registro", tabla(filas, "eliminacion_us", tres, grupos), " µs", dos),
                                 ("Eliminación, hash (fusión recorre el directorio)", "µs por registro", tabla(filas, "eliminacion_us", tres, grupos), " µs", ["hash_ram"])])

    # tabla en markdown con todas las corridas
    columnas = [("construccion_ms", "construcción ms"), ("construccion_masiva_ms", "carga masiva ms"), ("bytes_adicional", "adicional KB"),
                ("altura", "altura"), ("igualdad_us", "igualdad µs"), ("igualdad_pag_indice", "pág índice"), ("igualdad_pag_datos", "pág datos"),
                ("rango100_us", "rango 100 µs"), ("rango100_pag", "pág"), ("rango1000_us", "rango 1000 µs"), ("rango1000_pag", "pág"),
                ("ordenamiento_ms", "orden ms"), ("insercion_us", "inserción µs"), ("eliminacion_us", "eliminación µs")]
    with open(SALIDA.parent / "resultados.md", "w") as f:
        f.write("| estructura | n | orden | " + " | ".join(c[1] for c in columnas) + " |\n")
        f.write("|---|---:|---|" + "---:|" * len(columnas) + "\n")
        for r in sorted(todas, key=lambda r: (r["barajado"], r["n"], tres.index(r["estructura"]))):
            celdas = []
            for k, _ in columnas:
                v = r[k]
                if k == "bytes_adicional":
                    v = v / 1024
                if k == "construccion_masiva_ms" and v == 0:
                    celdas.append("—")
                elif k.startswith("rango") and r["soporta_rango"] == 0 and not k.endswith("pag"):
                    celdas.append(formato(v) + " (scan)")
                else:
                    celdas.append(formato(v))
            f.write(f"| {NOMBRE[r['estructura']]} | {etiqueta_n(r['n'])} | {'barajado' if r['barajado'] else 'ordenado'} | " + " | ".join(celdas) + " |\n")
    print("  ", (SALIDA.parent / "resultados.md").relative_to(RAIZ))


if __name__ == "__main__":
    main()
