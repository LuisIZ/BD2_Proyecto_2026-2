"""Benchmark del indice HNSW (FAISS) frente a la busqueda por fuerza bruta.

    python api/multimedia/bench_hnsw.py                          # vectores sinteticos
    python api/multimedia/bench_hnsw.py --vectores C:/datos_bd2/vectores.npy

Dos experimentos, con las mismas consultas y vectores que bench_ivf.py para que
los CSV se puedan comparar fila a fila:

1. Barrido de ef_busqueda. Para n = 1k, 10k y 100k mide la construccion, el
   k-NN con k = 10 promediado en 100 consultas, el tamano del indice, el
   Recall@10 contra la fuerza bruta y los vectores comparados por consulta
   (las distancias que calcula el grafo, faiss.cvar.hnsw_stats.ndis).
2. Borrado logico. Con n = 10k se borra una fraccion creciente de pk y se mide
   el k-NN sobre los vivos, una vez sin reconstruir nunca (umbral 1) y otra con
   el umbral por defecto (30 %). Muestra por que conviene reconstruir: los nodos
   borrados se siguen recorriendo y ocupan lugares del ef_busqueda. En estas
   filas construccion_ms es lo que tarda eliminar, incluida la reconstruccion.

Salida: datos/resultados/hnsw_bench.csv
"""

import argparse
import csv
import os
import sys
import time

import faiss
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from multimedia.bench_ivf import medir, sinteticos  # noqa: E402
from multimedia.indices import PARAMETROS, IndiceVectorial  # noqa: E402
from multimedia.test_indices import fuerza_bruta, recall  # noqa: E402

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAMANOS = (1_000, 10_000, 100_000)
EFS = (10, 16, 32, 64, 128, 256)
FRACCIONES = (0.0, 0.1, 0.2, 0.3, 0.5, 0.7, 0.9)
M = 32
EF_CONSTRUCCION = 100  # el mismo de docs/comparacion_vectorial
EF_BORRADO = 64
K = 10


def buscar(indice, consultas):
    """Tiempo por consulta en ms, resultados y distancias calculadas por consulta."""
    faiss.cvar.hnsw_stats.reset()
    tiempo, resultados = medir(consultas, lambda c: indice.knn(c, K))
    return tiempo, resultados, faiss.cvar.hnsw_stats.ndis / len(consultas)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--vectores", help="vectores.npy float32 (n, K) en el orden de los id")
    parser.add_argument("--metrica", default="cosine", choices=("euclidean", "inner_product", "cosine"))
    parser.add_argument("--consultas", type=int, default=100)
    parser.add_argument("--n-borrado", type=int, default=10_000, help="vectores del experimento de borrado")
    parser.add_argument("--salida", default=os.path.join(RAIZ, "datos", "resultados", "hnsw_bench.csv"))
    args = parser.parse_args()

    if args.vectores:
        todos = np.load(args.vectores).astype(np.float32)
        origen = os.path.basename(args.vectores)
    else:
        todos = sinteticos(max(TAMANOS))
        origen = "sinteticos"
    faiss.omp_set_num_threads(1)  # un hilo, para comparar con la fuerza bruta en igualdad
    rng = np.random.default_rng(7)  # misma semilla que bench_ivf.py: mismas consultas

    filas = []
    for n in TAMANOS:
        if n > len(todos):
            print(f"se omite n = {n}: solo hay {len(todos)} vectores")
            continue
        vectores = todos[:n]
        ids = np.arange(1, n + 1)
        consultas = vectores[rng.choice(n, args.consultas, replace=False)]
        base = {"origen": origen, "experimento": "ef_busqueda", "n": n, "dimension": vectores.shape[1],
                "metrica": args.metrica, "fraccion_borrada": 0, "reconstrucciones": 0}

        t_bruta, exactos = medir(consultas, lambda c: fuerza_bruta(vectores, ids, c, K, args.metrica))
        filas.append({**base, "estructura": "fuerza_bruta", "construccion_ms": 0, "consulta_ms": t_bruta,
                      "recall10": 1.0, "vectores_comparados": n, "bytes": vectores.nbytes})

        inicio = time.perf_counter()
        indice = IndiceVectorial("HNSW", args.metrica, M=M, ef_construccion=EF_CONSTRUCCION).construir(vectores, ids)
        t_construccion = (time.perf_counter() - inicio) * 1000
        for ef in EFS:
            indice.parametros["ef_busqueda"] = ef
            t_hnsw, aproximados, comparados = buscar(indice, consultas)
            r = np.mean([recall(a, e) for a, e in zip(aproximados, exactos)])
            filas.append({**base, "estructura": "hnsw", "M": M, "ef_construccion": EF_CONSTRUCCION,
                          "ef_busqueda": ef, "construccion_ms": t_construccion, "consulta_ms": t_hnsw,
                          "recall10": r, "vectores_comparados": comparados, "bytes": indice.tamano_en_disco()})
            print(f"n={n:>6} ef_busqueda={ef:>3}: {t_hnsw:7.3f} ms (bruta {t_bruta:7.3f} ms) "
                  f"recall@10={r:.2f} comparados={comparados:,.0f}")

    n = min(args.n_borrado, len(todos))
    vectores = todos[:n]
    ids = np.arange(1, n + 1)
    consultas = vectores[rng.choice(n, args.consultas, replace=False)]
    orden_borrado = rng.permutation(ids)  # las pk se borran en orden aleatorio
    for umbral in (1.0, PARAMETROS["HNSW"]["umbral_reconstruccion"]):
        indice = IndiceVectorial("HNSW", args.metrica, M=M, ef_construccion=EF_CONSTRUCCION,
                                 ef_busqueda=EF_BORRADO, umbral_reconstruccion=umbral).construir(vectores, ids)
        borradas = 0
        for fraccion in FRACCIONES:
            hasta = round(fraccion * n)
            inicio = time.perf_counter()
            indice.eliminar(orden_borrado[borradas:hasta])
            t_eliminar = (time.perf_counter() - inicio) * 1000
            borradas = hasta
            vivos = ~np.isin(ids, orden_borrado[:hasta])
            exactos = [fuerza_bruta(vectores[vivos], ids[vivos], c, K, args.metrica) for c in consultas]
            t_hnsw, aproximados, comparados = buscar(indice, consultas)
            r = np.mean([recall(a, e) for a, e in zip(aproximados, exactos)])
            filas.append({"origen": origen, "experimento": f"borrado_umbral_{umbral:g}", "estructura": "hnsw",
                          "n": n, "dimension": vectores.shape[1], "metrica": args.metrica, "M": M,
                          "ef_construccion": EF_CONSTRUCCION, "ef_busqueda": EF_BORRADO,
                          "fraccion_borrada": fraccion, "reconstrucciones": indice.reconstrucciones,
                          "construccion_ms": t_eliminar, "consulta_ms": t_hnsw, "recall10": r,
                          "vectores_comparados": comparados, "bytes": indice.tamano_en_disco()})
            print(f"umbral={umbral:g} borrado={fraccion:.0%}: eliminar {t_eliminar:8.2f} ms, {t_hnsw:6.3f} ms "
                  f"recall@10={r:.2f} comparados={comparados:,.0f} reconstrucciones={indice.reconstrucciones}")

    os.makedirs(os.path.dirname(args.salida), exist_ok=True)
    with open(args.salida, "w", encoding="utf-8", newline="") as f:
        # las de ivf_bench.csv, con M y ef en lugar de nlist y nprobe, mas las del borrado
        columnas = ["origen", "experimento", "estructura", "n", "dimension", "metrica", "M", "ef_construccion",
                    "ef_busqueda", "fraccion_borrada", "reconstrucciones", "construccion_ms", "consulta_ms",
                    "recall10", "vectores_comparados", "bytes"]
        escritor = csv.DictWriter(f, columnas, lineterminator="\n", restval="")
        escritor.writeheader()
        for fila in filas:
            escritor.writerow({c: round(v, 4) if isinstance(v, float) else v for c, v in fila.items()})
    print("resultados en", args.salida)


if __name__ == "__main__":
    main()
