import csv
import pathlib
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

AQUI = pathlib.Path(__file__).resolve().parent

COLOR = {
    "heap": "#c44e52",
    "secuencial": "#dd8452",
    "bplus_agrupado": "#4c72b0",
    "bplus_no_agrupado": "#55a868",
    "hash_extensible": "#8172b3",
}
ETIQUETA = {
    "heap": "Heap file",
    "secuencial": "Secuencial paginado",
    "bplus_agrupado": "B+ agrupado",
    "bplus_no_agrupado": "B+ no agrupado",
    "hash_extensible": "Hash extensible",
}

def leer(nombre):
    ruta = AQUI / nombre
    if not ruta.exists():
        print(f"  falta {nombre}, se omiten las figuras que dependen de el")
        return []
    with open(ruta, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))

def ultimo_por_n(filas, filtro=None):
    elegidas = {}
    for fila in filas:
        if filtro and not filtro(fila):
            continue
        elegidas[int(fila["n"])] = fila
    return [elegidas[n] for n in sorted(elegidas)]

def serie(filas, campo, escala=1.0):
    return [int(f["n"]) for f in filas], [float(f[campo]) / escala for f in filas]

def eje_n(ax):
    ax.set_xscale("log")
    ax.set_xlabel("registros (n)")
    ax.grid(True, which="both", alpha=0.25, linewidth=0.6)

def guardar(fig, nombre):
    fig.tight_layout()
    destino = AQUI / nombre
    fig.savefig(destino, dpi=150)
    plt.close(fig)
    print(f"  {destino.name}")

def figura_archivos(heap, seq):
    if not heap or not seq:
        return
    fig, ejes = plt.subplots(2, 2, figsize=(11, 8))
    fig.suptitle("Gestion de archivos: heap file vs archivo secuencial paginado", fontsize=13)

    paneles = [
        ("t_insercion_us", 1000.0, "Tiempo de insercion", "ms", True),
        ("paginas_leidas_por_busqueda", 1.0, "Paginas leidas por busqueda de clave primaria", "paginas", True),
        ("bytes_en_disco", 1024.0, "Espacio en disco", "KB", True),
        ("t_reorganizacion_us", 1000.0, "Tiempo de reorganizacion", "ms", True),
    ]
    for ax, (campo, escala, titulo, unidad, log_y) in zip(ejes.flat, paneles):
        for datos, clave in ((heap, "heap"), (seq, "secuencial")):
            x, y = serie(datos, campo, escala)
            ax.plot(x, y, marker="o", color=COLOR[clave], label=ETIQUETA[clave])
        ax.set_title(titulo, fontsize=10)
        ax.set_ylabel(unidad)
        if log_y:
            ax.set_yscale("log")
        eje_n(ax)
        ax.legend(fontsize=8)
    guardar(fig, "fig1_gestion_archivos.png")

def figura_clave_primaria(heap, seq, bpa):
    if not (heap and seq and bpa):
        return
    tamanos = sorted({int(f["n"]) for f in heap} & {int(f["n"]) for f in seq} & {int(f["n"]) for f in bpa})
    if not tamanos:
        return

    def valor(filas, n, campo):
        for f in filas:
            if int(f["n"]) == n:
                return float(f[campo])
        return 0.0

    fig, ax = plt.subplots(figsize=(8, 4.8))
    ancho = 0.26
    fuentes = [
        (heap, "paginas_leidas_por_busqueda", "heap"),
        (seq, "paginas_leidas_por_busqueda", "secuencial"),
        (bpa, "paginas_por_busqueda", "bplus_agrupado"),
    ]
    for i, (filas, campo, clave) in enumerate(fuentes):
        alturas = [valor(filas, n, campo) for n in tamanos]
        posiciones = [j + (i - 1) * ancho for j in range(len(tamanos))]
        barras = ax.bar(posiciones, alturas, ancho, color=COLOR[clave], label=ETIQUETA[clave])
        ax.bar_label(barras, fmt="%.0f", fontsize=7, padding=2)

    ax.set_yscale("log")
    ax.set_xticks(range(len(tamanos)))
    ax.set_xticklabels([f"{n:,}".replace(",", " ") for n in tamanos])
    ax.set_xlabel("registros (n)")
    ax.set_ylabel("paginas leidas (escala log)")
    ax.set_title("Busqueda por clave primaria: paginas leidas por consulta", fontsize=12)
    ax.legend(fontsize=9)
    ax.grid(True, axis="y", alpha=0.25, linewidth=0.6)
    guardar(fig, "fig2_busqueda_clave_primaria.png")

def figura_indices(indices, columna, nombre_archivo, subtitulo):
    filas = [f for f in indices if f["columna"] == columna and f["verifica_duplicado"] == "0"]
    if not filas:
        return
    bplus = ultimo_por_n(filas, lambda f: f["implementacion"] == "bplus_no_agrupado")
    hashd = ultimo_por_n(filas, lambda f: f["implementacion"] == "hash_extensible")
    if not bplus or not hashd:
        return

    fig, ejes = plt.subplots(2, 2, figsize=(11, 8))
    fig.suptitle(f"Indices secundarios: B+ no agrupado vs hash extensible\n{subtitulo}", fontsize=12)

    ax = ejes[0][0]
    for datos, clave in ((bplus, "bplus_no_agrupado"), (hashd, "hash_extensible")):
        x, y = serie(datos, "paginas_por_igualdad")
        ax.plot(x, y, marker="o", color=COLOR[clave], label=ETIQUETA[clave])
    ax.set_title("Busqueda por igualdad: paginas leidas", fontsize=10)
    ax.set_ylabel("paginas por consulta")
    ax.set_ylim(bottom=0)
    eje_n(ax)
    ax.legend(fontsize=8)

    ax = ejes[0][1]
    x, y = serie(bplus, "paginas_por_rango")
    ax.plot(x, y, marker="o", color=COLOR["bplus_no_agrupado"], label=ETIQUETA["bplus_no_agrupado"])
    ax.plot(x, [0] * len(x), marker="x", markersize=9, linestyle="--", linewidth=1.6,
            color=COLOR["hash_extensible"], label="Hash extensible (no soportado)")
    ax.text(0.5, 0.42, "un hash no conserva el orden de las claves:\nla consulta cae a scan completo de la tabla",
            transform=ax.transAxes, ha="center", fontsize=8, color=COLOR["hash_extensible"],
            bbox=dict(boxstyle="round,pad=0.4", facecolor="white",
                      edgecolor=COLOR["hash_extensible"], alpha=0.9))
    ax.set_title("Busqueda por rango: paginas leidas", fontsize=10)
    ax.set_ylabel("paginas por consulta")
    ax.set_ylim(bottom=-max(y) * 0.06)
    eje_n(ax)
    ax.legend(fontsize=8, loc="upper left")

    ax = ejes[1][0]
    for datos, clave in ((bplus, "bplus_no_agrupado"), (hashd, "hash_extensible")):
        x, y = serie(datos, "bytes_en_disco", 1024.0)
        ax.plot(x, y, marker="o", color=COLOR[clave], label=ETIQUETA[clave])
    ax.set_title("Espacio adicional del indice", fontsize=10)
    ax.set_ylabel("KB")
    ax.set_yscale("log")
    eje_n(ax)
    ax.legend(fontsize=8)

    ax = ejes[1][1]
    for datos, clave in ((bplus, "bplus_no_agrupado"), (hashd, "hash_extensible")):
        x, y = serie(datos, "t_construccion_us", 1000.0)
        ax.plot(x, y, marker="o", color=COLOR[clave], label=ETIQUETA[clave])
    ax.set_title("Tiempo de construccion del indice", fontsize=10)
    ax.set_ylabel("ms")
    ax.set_yscale("log")
    eje_n(ax)
    ax.legend(fontsize=8)

    guardar(fig, nombre_archivo)

def figura_dedupe(indices):
    filas = [f for f in indices if f["implementacion"] == "hash_extensible" and f["columna"] == "Founded"]
    con = [f for f in filas if f["verifica_duplicado"] == "1"]
    sin = [f for f in filas if f["verifica_duplicado"] == "0" and int(f["n"]) == 100000]
    if not con or not sin:
        return
    fig, ax = plt.subplots(figsize=(6.5, 4.4))
    rapido = float(sin[-1]["t_construccion_us"]) / 1000
    lento = float(con[-1]["t_construccion_us"]) / 1000
    barras = ax.bar(["sin comprobar\n(lo que hace el motor)", "comprobando\nel duplicado"], [rapido, lento],
                    color=[COLOR["hash_extensible"], "#b0b0b0"], width=0.55)
    ax.bar_label(barras, fmt="%.0f ms", fontsize=11, padding=3)
    ax.set_ylim(0, lento * 1.2)
    ax.set_ylabel("tiempo de construccion (ms)")
    ax.set_title("Hash extensible, 100 000 filas sobre Founded\n(53 claves distintas: 1887 filas por clave)",
                 fontsize=11)
    ax.annotate(f"{lento / rapido:.0f}x mas lento", xy=(1, lento), xytext=(0.45, lento * 0.75),
                fontsize=11, color="#444444",
                arrowprops=dict(arrowstyle="->", color="#444444", lw=1.2))
    ax.grid(True, axis="y", alpha=0.25, linewidth=0.6)
    guardar(fig, "fig5_costo_dedupe.png")

def main():
    print("generando figuras en", AQUI)
    heap = ultimo_por_n(leer("heap_bench.csv"), lambda f: f.get("barajado") == "0")
    seq = ultimo_por_n(leer("sequential_bench.csv"))
    bpa = ultimo_por_n(leer("bplus_agrupado.csv"), lambda f: f.get("modo") == "insercion")
    indices = leer("indices_secundarios.csv")

    figura_archivos(heap, seq)
    figura_clave_primaria(heap, seq, bpa)
    figura_indices(indices, "Number of employees", "fig3_indices_alta_cardinalidad.png",
                   "clave con alta cardinalidad (Number of employees: ~10 filas por clave)")
    figura_indices(indices, "Founded", "fig4_indices_baja_cardinalidad.png",
                   "clave con baja cardinalidad (Founded: ~1887 filas por clave)")
    figura_dedupe(indices)

    if not any(AQUI.glob("fig*.png")):
        print("no se genero ninguna figura: faltan los CSV de benchmarks")
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
