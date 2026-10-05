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

ESTRUCTURAS = ["bplus_agrupado", "bplus_no_agrupado", "hash_disco"]
COLOR = {"bplus_agrupado": "#4c72b0", "bplus_no_agrupado": "#55a868", "hash_disco": "#8172b3"}
ETIQUETA = {"bplus_agrupado": "B+ agrupado", "bplus_no_agrupado": "B+ no agrupado",
            "hash_disco": "Hash extensible"}


def leer():
    ruta = AQUI / "comparacion_indices.csv"
    if not ruta.exists():
        print("falta comparacion_indices.csv: ejecuta primero el benchmark")
        sys.exit(1)
    with open(ruta, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def agrupar(filas):
    grupos = {}
    for fila in filas:
        clave = (fila["barajado"], fila["estructura"], int(fila["n"]))
        grupos.setdefault(clave, []).append(fila)
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


# genera las seis gráficas y el resumen
def main():
    filas = leer()
    grupos = agrupar(filas)
    resumen = []

    def valores(orden, estructura, n, calcular):
        return [calcular(f) for f in grupos[(orden, estructura, n)]]

    def figura(nombre, titulo, unidad, series, experimento):
        fig, ax = plt.subplots(figsize=(8, 5))
        preparar_ejes(ax, unidad, titulo)
        for estructura, calcular, etiqueta, estilo, marcador in series:
            ys = [statistics.median(valores("1", estructura, n, calcular)) for n in TAMANOS]
            ax.plot(TAMANOS, ys, marker=marcador, linestyle=estilo, color=COLOR[estructura],
                    label=etiqueta, linewidth=2)
            for orden, nombre_orden in (("1", "aleatorio"), ("0", "ascendente")):
                for n in TAMANOS:
                    v = valores(orden, estructura, n, calcular)
                    resumen.append([experimento, etiqueta, nombre_orden, n, unidad,
                                    round(statistics.mean(v), 4), round(statistics.median(v), 4),
                                    round(p95(v), 4), round(min(v), 4), round(max(v), 4), len(v)])
        ax.legend(fontsize=9)
        fig.tight_layout()
        fig.savefig(AQUI / nombre, dpi=150)
        plt.close(fig)
        print(f"  {nombre}")

    def col(campo, escala=1.0):
        return lambda f: float(f[campo]) / escala

    def datos_mas_indice(f):
        return float(f["datos_ms"]) + float(f["construccion_ms"])

    a, n, h = ESTRUCTURAS
    solido, rayas = "-", "--"

    print("generando gráficas en", AQUI)

    figura("exp1_construccion.png", "Tiempo de construcción", "tiempo (ms)", [
        (a, col("construccion_ms"), "B+ agrupado (el índice es el archivo)", solido, "o"),
        (n, datos_mas_indice, "B+ no agrupado: heap + índice", solido, "o"),
        (n, col("construccion_ms"), "B+ no agrupado: solo el índice", rayas, "s"),
        (h, datos_mas_indice, "Hash extensible: heap + índice", solido, "o"),
        (h, col("construccion_ms"), "Hash extensible: solo el índice", rayas, "s"),
    ], "1 construcción")

    figura("exp2_igualdad.png", "Búsqueda por igualdad exacta", "tiempo por consulta (µs)", [
        (a, col("igualdad_us"), ETIQUETA[a], solido, "o"),
        (n, col("igualdad_us"), "B+ no agrupado (índice + salto al heap)", solido, "o"),
        (h, col("igualdad_us"), "Hash extensible (índice + salto al heap)", solido, "o"),
    ], "2 igualdad exacta")

    figura("exp3_rango.png", "Búsqueda por rango (100 claves consecutivas)", "tiempo por consulta (µs)", [
        (a, col("rango100_us"), ETIQUETA[a], solido, "o"),
        (n, col("rango100_us"), "B+ no agrupado (hojas + un salto al heap por fila)", solido, "o"),
        (h, col("rango100_us"), "Hash extensible (sin rango: recorre todo el heap)", rayas, "x"),
    ], "3 rango de 100 claves")

    figura("exp4_ordenamiento.png", "Ordenar todos los registros por clave", "tiempo (ms)", [
        (a, col("ordenamiento_ms"), "B+ agrupado (recorrer hojas)", solido, "o"),
        (n, col("ordenamiento_ms"), "B+ no agrupado (hojas + un salto al heap por fila)", solido, "o"),
        (h, col("ordenamiento_ms"), "Hash extensible (ordenación externa)", rayas, "x"),
    ], "4 ordenamiento")

    figura("exp5_espacio_adicional.png", "Espacio adicional requerido", "tamaño (MB)", [
        (a, col("bytes_adicional", MB), "B+ agrupado (archivo menos las filas)", solido, "o"),
        (n, col("bytes_adicional", MB), "B+ no agrupado (solo el índice)", solido, "o"),
        (h, col("bytes_adicional", MB), "Hash extensible (solo el índice)", solido, "o"),
    ], "5 espacio adicional")

    figura("exp6_insercion_eliminacion.png", "Inserciones y eliminaciones frecuentes", "tiempo por operación (µs)", [
        (a, col("insercion_us"), "B+ agrupado: inserción", solido, "o"),
        (a, col("eliminacion_us"), "B+ agrupado: eliminación", rayas, "s"),
        (n, col("insercion_us"), "B+ no agrupado: inserción", solido, "o"),
        (n, col("eliminacion_us"), "B+ no agrupado: eliminación", rayas, "s"),
        (h, col("insercion_us"), "Hash extensible: inserción", solido, "o"),
        (h, col("eliminacion_us"), "Hash extensible: eliminación", rayas, "s"),
    ], "6 inserción y eliminación")

    with open(AQUI / "resumen_comparacion.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["experimento", "algoritmo", "orden_de_claves", "n", "unidad", "media", "mediana",
                    "p95", "minimo", "maximo", "corridas"])
        w.writerows(resumen)
    print("  resumen_comparacion.csv")


if __name__ == "__main__":
    main()
