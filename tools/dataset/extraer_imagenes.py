"""Copia la imagen principal de cada producto de un CSV desde images/small/ de ABO.

    python tools/dataset/extraer_imagenes.py datos/productos_100k.csv
    python tools/dataset/extraer_imagenes.py datos/productos_1k.csv --destino C:/datos_bd2/imagenes_1k --zip

Solo se copian las imagenes de los productos elegidos (no las 398 212 del archivo),
con la misma ruta relativa que la columna `imagen` (los dos primeros caracteres
son la subcarpeta). Con --zip ademas se arma un .zip para compartirlo por Drive.
Nada de esto se versiona.
"""

import argparse
import csv
import os
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    datos = os.environ.get("BD2_DATOS", r"C:\datos_bd2")
    parser.add_argument("csv", help="productos_{1k,10k,100k}.csv")
    parser.add_argument("--abo", default=os.path.join(datos, "abo"), help="carpeta con images/small/")
    parser.add_argument("--destino", default=os.path.join(datos, "imagenes"))
    parser.add_argument("--zip", action="store_true", help="crear <destino>.zip al terminar")
    args = parser.parse_args()

    origen = os.path.join(args.abo, "images", "small")
    copiadas = existentes = 0
    faltan = []
    with open(args.csv, encoding="utf-8", newline="") as f:
        for fila in csv.DictReader(f):
            desde = os.path.join(origen, fila["imagen"])
            hacia = os.path.join(args.destino, fila["imagen"])
            if os.path.exists(hacia):
                existentes += 1
                continue
            if not os.path.exists(desde):
                faltan.append(fila["id"])
                continue
            os.makedirs(os.path.dirname(hacia), exist_ok=True)
            shutil.copyfile(desde, hacia)
            copiadas += 1

    print(f"{copiadas} imagenes copiadas y {existentes} que ya estaban en {args.destino}")
    if faltan:
        print(f"faltan {len(faltan)} imagenes en {origen}, ids: {faltan[:10]}")
    if args.zip:
        print("zip:", shutil.make_archive(args.destino, "zip", args.destino))


if __name__ == "__main__":
    main()
