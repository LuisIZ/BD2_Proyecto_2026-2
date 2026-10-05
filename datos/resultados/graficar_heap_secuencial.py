import csv
import math
import pathlib
import statistics
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter

AQUI = pathlib.Path(__file__).resolve().parent
TAMANOS = [1000, 10000, 100000]
MB = 1024 * 1024
ROJO = "#c44e52"
NARANJA = "#dd8452"


def leer(nombre):
    ruta = AQUI / nombre
    if not ruta.exists():
        print(f"falta {nombre}: ejecuta primero los benchmarks")
        sys.exit(1)
    with open(ruta, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def agrupar(filas):
    grupos = {}
    for fila in filas:
        grupos.setdefault((fila["barajado"], int(fila["n"])), []).append(fila)
    return {clave: lista[1:] for clave, lista in grupos.items()}


def p95(valores):
    ordenados = sorted(valores)
    return ordenados[math.ceil(0.95 * len(ordenados)) - 1]


def formato_eje(v, _):
    if v >= 1000:
        return f"{v:,.0f}".replace(",", " ")
    return f"{v:g}"


def preparar_ejes(ax, unidad, titulo):
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xticks(TAMANOS)
    ax.set_xticklabels([f"{n:,}".replace(",", " ") for n in TAMANOS])
    ax.xaxis.set_minor_formatter(NullFormatter())
    ax.yaxis.set_major_locator(LogLocator(base=10, subs=(1.0, 2.0, 5.0), numticks=20))
    ax.yaxis.set_major_formatter(FuncFormatter(formato_eje))
    ax.yaxis.set_minor_formatter(NullFormatter())
    ax.set_xlabel("registros (n), escala log")
    ax.set_ylabel(f"{unidad}, escala log")
    ax.set_title(titulo, fontsize=12)
    ax.grid(True, which="both", alpha=0.25, linewidth=0.6)


# genera las cuatro gráficas y el resumen
def main():
    heap = agrupar(leer("corrida_heap.csv"))
    sec = agrupar(leer("corrida_secuencial.csv"))
    resumen = []

    def valores(grupos, orden, n, campo, escala):
        return [float(f[campo]) / escala for f in grupos[(orden, n)]]

    def figura(nombre, titulo, unidad, campo, escala, experimento):
        series = [
            (heap, "1", "Heap file", ROJO, "-", "o"),
            (sec, "1", "Secuencial paginado, claves aleatorias", NARANJA, "-", "o"),
            (sec, "0", "Secuencial paginado, claves ascendentes", NARANJA, "--", "s"),
        ]
        fig, ax = plt.subplots(figsize=(8, 5))
        preparar_ejes(ax, unidad, titulo)
        for grupos, orden, etiqueta, color, estilo, marcador in series:
            ys = [statistics.median(valores(grupos, orden, n, campo, escala)) for n in TAMANOS]
            ax.plot(TAMANOS, ys, marker=marcador, linestyle=estilo, color=color, label=etiqueta, linewidth=2)
        for grupos, etiqueta in ((heap, "Heap file"), (sec, "Secuencial paginado")):
            for orden, nombre_orden in (("1", "aleatorio"), ("0", "ascendente")):
                for n in TAMANOS:
                    v = valores(grupos, orden, n, campo, escala)
                    resumen.append([experimento, etiqueta, nombre_orden, n, unidad,
                                    round(statistics.mean(v), 4), round(statistics.median(v), 4),
                                    round(p95(v), 4), round(min(v), 4), round(max(v), 4), len(v)])
        ax.legend(fontsize=9)
        fig.tight_layout()
        fig.savefig(AQUI / nombre, dpi=150)
        plt.close(fig)
        print(f"  {nombre}")

    print("generando gráficas en", AQUI)
    figura("hs1_insercion.png", "Tiempo de inserción de todos los registros", "tiempo (ms)",
           "t_insercion_us", 1000, "1 inserción")
    figura("hs2_busqueda_pk.png", "Búsqueda por clave primaria", "tiempo por consulta (µs)",
           "t_busqueda_pk_prom_us", 1, "2 búsqueda por clave primaria")
    figura("hs3_espacio.png", "Espacio en disco utilizado", "tamaño (MB)",
           "bytes_en_disco", MB, "3 espacio en disco")
    figura("hs4_reorganizacion.png", "Tiempo de reorganización", "tiempo (ms)",
           "t_reorganizacion_us", 1000, "4 reorganización")

    with open(AQUI / "resumen_heap_secuencial.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["experimento", "algoritmo", "orden_de_claves", "n", "unidad", "media", "mediana",
                    "p95", "minimo", "maximo", "corridas"])
        w.writerows(resumen)
    print("  resumen_heap_secuencial.csv")


if __name__ == "__main__":
    main()
