"""Junta productos y vectores en un CSV que COPY carga en una tabla con VECTOR(256).

    python tools/dataset/preparar_catalogo.py --n 10000

Lee datos/productos_100k.csv (o el mas chico que alcance) y datos/productos/vectores_100000.npy,
que estan en el mismo orden, y escribe datos/productos/catalogo_<n>.csv con id, nombre y v.
"""

import argparse
import csv
from pathlib import Path

import numpy as np

RAIZ = Path(__file__).resolve().parents[2]
LARGO_NOMBRE = 80


def recortar(texto):
    return texto.encode("utf-8")[:LARGO_NOMBRE].decode("utf-8", "ignore")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--n", type=int, default=10000, help="cantidad de productos")
    parser.add_argument("--vectores", default=RAIZ / "datos" / "productos" / "vectores_100000.npy")
    args = parser.parse_args()

    vectores = np.load(args.vectores, allow_pickle=False, mmap_mode="r")
    candidatos = [("1k", 1000), ("10k", 10000), ("100k", 100000)]
    ruta = next((RAIZ / "datos" / f"productos_{nombre}.csv" for nombre, filas in candidatos if filas >= args.n), None)
    if ruta is None or not ruta.is_file():
        raise SystemExit(f"no hay un CSV de productos con {args.n} filas")
    if args.n > len(vectores):
        raise SystemExit(f"solo hay {len(vectores)} vectores")

    salida = RAIZ / "datos" / "productos" / f"catalogo_{args.n}.csv"
    with open(ruta, encoding="utf-8", newline="") as entrada, open(salida, "w", encoding="utf-8", newline="") as archivo:
        escritor = csv.writer(archivo)
        escritor.writerow(["id", "nombre", "v"])
        for fila, vector in zip(csv.DictReader(entrada), vectores[: args.n]):
            literal = "VECTOR(" + ",".join(f"{x:.8g}" for x in vector.tolist()) + ")"
            escritor.writerow([fila["id"], recortar(fila["nombre"]), literal])
    print(f"{args.n} productos en {salida}")


if __name__ == "__main__":
    main()
