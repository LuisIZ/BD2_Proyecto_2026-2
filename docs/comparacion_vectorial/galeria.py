"""Galería de 3 éxitos y 3 errores de la búsqueda por similitud de imágenes (consulta + top 5).

    python docs/comparacion_vectorial/galeria.py

Usa los vectores SIFT + TF-IDF de los 100k productos (api/multimedia/extraer.py) y las
miniaturas de C:/datos_bd2/imagenes (tools/dataset/extraer_imagenes.py). La consulta es la
imagen de un producto y el top 5 sale por coseno sin contar al propio producto. Se usa la
búsqueda exacta (Flat) para ver la calidad de los descriptores, no la del índice: IVF y HNSW
devuelven los mismos vecinos cuando su Recall@10 es 1 (ver README.md).

Éxito: al menos 4 de los 5 vecinos tienen el mismo `tipo` que la consulta. Error: ninguno.
Se eligen consultas de tipos distintos para que no salgan solo fundas de celular.

Salida: docs/comparacion_vectorial/galeria/exito_<i>.png y error_<i>.png
"""

import csv
import os
from pathlib import Path

import faiss
import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.image import imread  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent.parent
SALIDA = Path(__file__).resolve().parent / "galeria"
IMAGENES = Path(os.environ.get("BD2_DATOS", r"C:\datos_bd2")) / "imagenes"
VECTORES = RAIZ / "datos" / "productos" / "vectores_100000.npy"
TOP = 5
CANDIDATAS = 400
VERDE, ROJO = "#1baf7a", "#d64545"


def leer_productos():
    with open(RAIZ / "datos" / "productos_100k.csv", encoding="utf-8", newline="") as archivo:
        return list(csv.DictReader(archivo))


def vecinos(vectores, consultas):
    """Top 5 por coseno de cada consulta, sin la propia fila."""
    x = np.ascontiguousarray(vectores, dtype=np.float32)
    faiss.normalize_L2(x)
    indice = faiss.IndexFlatIP(x.shape[1])
    indice.add(x)
    similitudes, posiciones = indice.search(x[consultas], TOP + 1)
    salida = []
    for consulta, sims, pos in zip(consultas, similitudes, posiciones):
        pares = [(int(p), 1 - float(s)) for p, s in zip(pos, sims) if p != consulta][:TOP]
        salida.append(pares)
    return salida


def elegir(productos, consultas, resultados):
    """3 éxitos y 3 errores de tipos distintos, en el orden aleatorio de las candidatas."""
    exitos, errores, tipos = [], [], set()
    for consulta, pares in zip(consultas, resultados):
        tipo = productos[consulta]["tipo"]
        iguales = sum(productos[p]["tipo"] == tipo for p, _ in pares)
        if tipo in tipos:
            continue
        if iguales >= 4 and len(exitos) < 3:
            exitos.append((consulta, pares))
            tipos.add(tipo)
        elif iguales == 0 and len(errores) < 3:
            errores.append((consulta, pares))
            tipos.add(tipo)
    return exitos, errores


def imagen(producto):
    return imread(IMAGENES / producto["imagen"])


def dibujar(productos, consulta, pares, titulo, archivo):
    fig, ejes = plt.subplots(1, TOP + 1, figsize=(13, 3.1))
    tipo = productos[consulta]["tipo"]
    casos = [(consulta, None)] + pares
    for i, (eje, (p, distancia)) in enumerate(zip(ejes, casos)):
        producto = productos[p]
        eje.imshow(imagen(producto))
        eje.set_xticks([])
        eje.set_yticks([])
        if i == 0:
            color, texto = "#202923", f"CONSULTA\n{producto['tipo']}"
        else:
            color = VERDE if producto["tipo"] == tipo else ROJO
            texto = f"#{i}  dist {distancia:.3f}\n{producto['tipo']}"
        for borde in eje.spines.values():
            borde.set_edgecolor(color)
            borde.set_linewidth(4)
        eje.set_title(texto, fontsize=8, color=color)
        eje.set_xlabel(producto["nombre"][:34], fontsize=7)
    fig.suptitle(titulo, x=0.01, ha="left", fontweight="bold")
    fig.tight_layout()
    fig.savefig(SALIDA / archivo, dpi=110)
    plt.close(fig)


def main():
    SALIDA.mkdir(parents=True, exist_ok=True)
    productos = leer_productos()
    vectores = np.load(VECTORES)
    # productos[i] es la fila i del CSV y la fila i de los vectores (extraer.py respeta el orden)
    consultas = np.random.default_rng(7).choice(len(vectores), CANDIDATAS, replace=False)
    exitos, errores = elegir(productos, consultas, vecinos(vectores, consultas))
    for i, (consulta, pares) in enumerate(exitos, 1):
        dibujar(productos, consulta, pares, f"Éxito {i}: al menos 4 de los 5 vecinos son del tipo de la consulta",
                f"exito_{i}.png")
    for i, (consulta, pares) in enumerate(errores, 1):
        dibujar(productos, consulta, pares, f"Error {i}: ningún vecino es del tipo de la consulta",
                f"error_{i}.png")
    print(f"{len(exitos)} éxitos y {len(errores)} errores en {SALIDA}")


if __name__ == "__main__":
    main()
