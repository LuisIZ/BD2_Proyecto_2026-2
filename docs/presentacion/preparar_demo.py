"""Arma el CSV de la demo de la Parte 4: los 10 000 productos de productos_10k.csv con su vector.

    python docs/presentacion/preparar_demo.py

Une datos/productos_10k.csv (id, nombre, tipo) con datos/productos/vectores_100000.csv, que escribe
api/multimedia/extraer.py, y deja datos/productos/demo_10k.csv con las columnas id,nombre,tipo,v,
listo para el COPY de "demo - p3 y p4 (dayane).sql". Los primeros 10 000 vectores son los mismos
productos de productos_10k.csv, porque los subconjuntos son anidados (ver docs/dataset.md).
"""

import argparse
import csv
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--productos", default=RAIZ / "datos" / "productos_10k.csv", type=Path)
    parser.add_argument("--vectores", default=RAIZ / "datos" / "productos" / "vectores_100000.csv", type=Path)
    parser.add_argument("--salida", default=RAIZ / "datos" / "productos" / "demo_10k.csv", type=Path)
    args = parser.parse_args()

    if not args.vectores.is_file():
        sys.exit(f"falta {args.vectores}: generarlo con api/multimedia/extraer.py (docs/comparacion_vectorial/README.md)")

    with open(args.productos, encoding="utf-8", newline="") as f:
        productos = {fila["id"]: fila for fila in csv.DictReader(f)}

    escritos = 0
    with open(args.vectores, encoding="utf-8", newline="") as entrada, \
            open(args.salida, "w", encoding="utf-8", newline="") as salida:
        escritor = csv.writer(salida, lineterminator="\n")
        escritor.writerow(["id", "nombre", "tipo", "v"])
        for fila in csv.DictReader(entrada):
            producto = productos.get(fila["id"])
            if producto is None:
                continue
            escritor.writerow([fila["id"], producto["nombre"], producto["tipo"], fila["vector"]])
            escritos += 1

    if escritos != len(productos):
        sys.exit(f"solo {escritos} de {len(productos)} productos tienen vector")
    print(f"{escritos} productos en {args.salida}")


if __name__ == "__main__":
    main()
