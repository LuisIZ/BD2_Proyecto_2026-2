"""Genera productos_{1k,10k,100k}.csv desde Amazon Berkeley Objects (ABO).

    python tools/dataset/preparar.py                       # ABO en $BD2_DATOS/abo
    python tools/dataset/preparar.py --abo C:/datos_bd2/abo --salida datos

Lee los 16 listings/metadata/listings_*.json.gz uno por uno y deja un registro por
item_id (el listing con mas texto en ingles). Solo entran productos cuya imagen
principal esta en images/metadata/images.csv.gz. Los subconjuntos son anidados
(1k esta dentro de 10k y 10k dentro de 100k) porque salen del mismo orden
aleatorio con semilla fija, asi que un id es el mismo producto en los tres CSV.

El lector de CSV del motor lee una linea por registro y mide los VARCHAR en bytes,
por eso los textos van sin saltos de linea y recortados en bytes UTF-8.
"""

import argparse
import csv
import glob
import gzip
import html
import json
import os
import random
import re

# columna -> largo maximo en bytes (el VARCHAR del CREATE TABLE)
COLUMNAS = {
    "id": None,
    "item_id": 20,
    "nombre": 200,
    "tipo": 50,
    "categoria": 200,
    "marca": 80,
    "color": 40,
    "descripcion": 1000,
    "imagen": 80,
}

TAMANOS = {"1k": 1_000, "10k": 10_000, "100k": 100_000}

ETIQUETA_HTML = re.compile(r"<[^>]+>")
ESPACIOS = re.compile(r"\s+")


def limpiar(texto):
    """Quita etiquetas HTML, entidades y saltos de linea."""
    texto = html.unescape(ETIQUETA_HTML.sub(" ", str(texto)))
    return ESPACIOS.sub(" ", texto).strip()


def recortar(texto, maximo):
    """Recorta a `maximo` bytes UTF-8 sin partir un caracter."""
    datos = texto.encode("utf-8")
    if len(datos) <= maximo:
        return texto
    return datos[:maximo].decode("utf-8", errors="ignore").rstrip()


def es_ingles(etiqueta):
    return (etiqueta or "").startswith("en_")


def valores_en(campo, idioma_ingles=True):
    """Valores del campo multilingue en ingles, primero en_US.

    Con idioma_ingles=False devuelve todos (para completar con otros idiomas)."""
    elegidos = [v for v in campo or [] if not idioma_ingles or es_ingles(v.get("language_tag"))]
    elegidos.sort(key=lambda v: v.get("language_tag") != "en_US")
    return [limpiar(v["value"]) for v in elegidos if v.get("value") not in (None, "")]


def primero(campo, idioma_ingles=True):
    valores = valores_en(campo, idioma_ingles)
    return valores[0] if valores else ""


def color_estandar(campo, idioma_ingles=True):
    """El valor estandarizado del color (standardized_values); si no hay, el valor tal cual."""
    elegidos = [v for v in campo or [] if not idioma_ingles or es_ingles(v.get("language_tag"))]
    elegidos.sort(key=lambda v: v.get("language_tag") != "en_US")
    for v in elegidos:
        if v.get("standardized_values"):
            return limpiar(v["standardized_values"][0])
    return limpiar(elegidos[0]["value"]) if elegidos else ""


def a_fila(listing, idioma_ingles=True):
    nombre = primero(listing.get("item_name"), idioma_ingles)
    partes = [nombre]
    partes += valores_en(listing.get("bullet_point"), idioma_ingles)
    partes += valores_en(listing.get("product_description"), idioma_ingles)
    nodos = listing.get("node") or []
    tipos = listing.get("product_type") or []
    return {
        "item_id": listing["item_id"],
        "nombre": nombre,
        "tipo": limpiar(tipos[0].get("value") or "") if tipos else "",
        "categoria": limpiar(nodos[0].get("node_name") or "") if nodos else "",
        "marca": primero(listing.get("brand"), idioma_ingles),
        "color": color_estandar(listing.get("color"), idioma_ingles),
        "descripcion": " ".join(p for p in partes if p),
        "imagen_id": listing.get("main_image_id", ""),
    }


def leer_imagenes(abo):
    """image_id -> path de images/metadata/images.csv.gz."""
    rutas = {}
    with gzip.open(os.path.join(abo, "images", "metadata", "images.csv.gz"), "rt", encoding="utf-8") as f:
        for fila in csv.DictReader(f):
            rutas[fila["image_id"]] = fila["path"]
    return rutas


def leer_productos(abo, imagenes):
    """Un producto por item_id: el listing con mas texto en ingles.

    Devuelve (en_ingles, otros_idiomas) como dict item_id -> fila."""
    ingles, otros = {}, {}
    contador = {"listings": 0, "sin_imagen": 0}
    archivos = sorted(glob.glob(os.path.join(abo, "listings", "metadata", "listings_*.json.gz")))
    if not archivos:
        raise SystemExit(f"no hay listings_*.json.gz en {abo}/listings/metadata")
    for ruta in archivos:
        with gzip.open(ruta, "rt", encoding="utf-8") as f:
            for linea in f:
                listing = json.loads(linea)
                contador["listings"] += 1
                if listing.get("main_image_id") not in imagenes:
                    contador["sin_imagen"] += 1
                    continue
                item = listing["item_id"]
                if any(es_ingles(v.get("language_tag")) for v in listing.get("item_name") or []):
                    fila = a_fila(listing)
                    if item not in ingles or len(fila["descripcion"]) > len(ingles[item]["descripcion"]):
                        ingles[item] = fila
                else:
                    fila = a_fila(listing, idioma_ingles=False)
                    if fila["nombre"] and (item not in otros or len(fila["descripcion"]) > len(otros[item]["descripcion"])):
                        otros[item] = fila
    for item in ingles:
        otros.pop(item, None)
    return ingles, otros, contador


def ordenar(productos, semilla):
    """Orden aleatorio reproducible: se parte del orden por item_id."""
    lista = [productos[k] for k in sorted(productos)]
    random.Random(semilla).shuffle(lista)
    return lista


def escribir(ruta, filas, imagenes):
    with open(ruta, "w", encoding="utf-8", newline="") as f:
        escritor = csv.writer(f, lineterminator="\n")
        escritor.writerow(COLUMNAS)
        for i, fila in enumerate(filas, start=1):
            fila = {**fila, "id": i, "imagen": imagenes[fila["imagen_id"]]}
            escritor.writerow(fila[c] if m is None else recortar(fila[c], m) for c, m in COLUMNAS.items())


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    datos = os.environ.get("BD2_DATOS", r"C:\datos_bd2")
    parser.add_argument("--abo", default=os.path.join(datos, "abo"), help="carpeta con listings/ e images/")
    parser.add_argument("--salida", default="datos", help="carpeta de los CSV")
    parser.add_argument("--semilla", type=int, default=2026)
    args = parser.parse_args()

    imagenes = leer_imagenes(args.abo)
    ingles, otros, contador = leer_productos(args.abo, imagenes)
    print(f"listings leidos: {contador['listings']} (sin imagen principal: {contador['sin_imagen']})")
    print(f"productos unicos en ingles con imagen: {len(ingles)}; en otros idiomas: {len(otros)}")

    # primero los de ingles; si no alcanzan, se completa con otros idiomas
    orden = ordenar(ingles, args.semilla) + ordenar(otros, args.semilla)
    os.makedirs(args.salida, exist_ok=True)
    for nombre, n in TAMANOS.items():
        if n > len(orden):
            raise SystemExit(f"solo hay {len(orden)} productos, no alcanzan para {nombre}")
        ruta = os.path.join(args.salida, f"productos_{nombre}.csv")
        escribir(ruta, orden[:n], imagenes)
        de_otros = max(0, n - len(ingles))
        print(f"{ruta}: {n} filas ({de_otros} en otros idiomas), {os.path.getsize(ruta) / 2**20:.1f} MB")


if __name__ == "__main__":
    main()
