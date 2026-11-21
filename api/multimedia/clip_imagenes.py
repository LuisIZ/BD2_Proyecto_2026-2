"""Vectores CLIP de las fotos de los productos, para "Buscar por foto" en la Tienda.

Es un camino aparte de SIFT (extraer.py): el catalogo de la Parte 4 y sus indices HNSW/IVF no cambian.
CLIP (openai/clip-vit-base-patch32) convierte cada foto en un vector de 512 numeros que captura lo que
es el objeto (una funda negra, un taburete), no solo sus puntos de interes. Los vectores quedan
normalizados, asi que el producto punto es el coseno y la busqueda es una multiplicacion de matrices.

Construir los vectores del catalogo (una vez, unos minutos en CPU):
    python -m api.multimedia.clip_imagenes
"""

import argparse
import csv
import time
from pathlib import Path

import numpy as np

RAIZ = Path(__file__).resolve().parents[2]
MODELO = "openai/clip-vit-base-patch32"
SALIDA = RAIZ / ".build" / "clip_catalogo.npz"  # ids (n,) y vectores (n, 512)

_modelo = None


def disponible():
    """True si PyTorch y transformers estan instalados."""
    try:
        import torch  # noqa: F401
        import transformers  # noqa: F401
    except ImportError:
        return False
    return True


def cargar_modelo():
    global _modelo
    if _modelo is None:
        import torch
        from transformers import CLIPModel, CLIPProcessor

        torch.set_num_threads(max(1, torch.get_num_threads()))
        modelo = CLIPModel.from_pretrained(MODELO).eval()
        _modelo = (modelo, CLIPProcessor.from_pretrained(MODELO))
    return _modelo


def vectores_de_imagenes(imagenes):
    """Lista de imagenes PIL -> matriz (n, 512) float32 normalizada (norma 1)."""
    import torch

    modelo, procesador = cargar_modelo()
    with torch.no_grad():
        entrada = procesador(images=imagenes, return_tensors="pt")
        salida = modelo.get_image_features(**entrada)
        salida = salida.pooler_output if hasattr(salida, "pooler_output") else salida
    vectores = salida.detach().cpu().numpy().astype(np.float32)
    return vectores / np.maximum(np.linalg.norm(vectores, axis=1, keepdims=True), 1e-12)


def vector_de_archivo(ruta):
    from PIL import Image

    with Image.open(ruta) as imagen:
        return vectores_de_imagenes([imagen.convert("RGB")])[0]


_indice = None


def cargar_indice():
    """(ids, vectores) del catalogo, o None si todavia no se construyo."""
    global _indice
    if _indice is None and SALIDA.is_file():
        datos = np.load(SALIDA)
        _indice = (datos["ids"], datos["vectores"])
    return _indice


def vecinos(vector, k):
    """Los k productos mas parecidos: lista de (id, coseno), de mayor a menor."""
    ids, matriz = cargar_indice()
    puntajes = matriz @ vector
    mejores = np.argsort(-puntajes)[:k]
    return [(int(ids[i]), float(puntajes[i])) for i in mejores]


def construir(lote=32, limite=None):
    """Lee las fotos del catalogo (las mismas que sirve /api/imagenes/{id}) y guarda sus vectores."""
    from PIL import Image

    from api import web_api  # rutas_imagenes y CARPETAS_IMAGENES

    rutas = web_api.rutas_imagenes()
    # rutas_imagenes lee el CSV mas grande (100k); el catalogo de la tienda son los de productos_10k.csv
    csv_catalogo = RAIZ / "datos" / "productos_10k.csv"
    if csv_catalogo.is_file():
        with csv_catalogo.open(encoding="utf-8", newline="") as archivo:
            catalogo = {int(fila["id"]) for fila in csv.DictReader(archivo)}
        rutas = {i: r for i, r in rutas.items() if i in catalogo}
    ids_catalogo = sorted(rutas)[:limite] if limite else sorted(rutas)
    ids, vectores = [], []
    inicio = time.time()
    for desde in range(0, len(ids_catalogo), lote):
        imagenes, usados = [], []
        for id_ in ids_catalogo[desde:desde + lote]:
            for carpeta in web_api.CARPETAS_IMAGENES:
                ruta = (carpeta / rutas[id_]).resolve()
                if ruta.is_relative_to(carpeta.resolve()) and ruta.is_file():
                    try:
                        with Image.open(ruta) as foto:
                            imagenes.append(foto.convert("RGB"))
                        usados.append(id_)
                    except OSError:
                        pass
                    break
        if imagenes:
            vectores.append(vectores_de_imagenes(imagenes))
            ids.extend(usados)
        hechas = min(desde + lote, len(ids_catalogo))
        if (desde // lote) % 10 == 0 or hechas == len(ids_catalogo):
            print(f"{hechas}/{len(ids_catalogo)} fotos, {time.time() - inicio:.0f} s", flush=True)
    SALIDA.parent.mkdir(parents=True, exist_ok=True)
    np.savez(SALIDA, ids=np.array(ids, dtype=np.int64), vectores=np.concatenate(vectores))
    print(f"guardado {SALIDA} con {len(ids)} vectores")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--lote", type=int, default=32)
    parser.add_argument("--limite", type=int, help="solo las primeras n fotos (para probar)")
    argumentos = parser.parse_args()
    construir(argumentos.lote, argumentos.limite)
