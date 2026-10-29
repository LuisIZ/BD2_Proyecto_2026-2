"""Benchmark del indice IVF (FAISS) frente a la busqueda por fuerza bruta.

    python api/multimedia/bench_ivf.py                          # vectores sinteticos
    python api/multimedia/bench_ivf.py --vectores C:/datos_bd2/vectores.npy

Para n = 1k, 10k y 100k mide la construccion, el k-NN con k = 10 promediado en
100 consultas, el tamano del indice y el Recall@10 contra la fuerza bruta, con
varios nprobe para ver el compromiso entre recall y tiempo. Tambien cuenta los
vectores comparados por consulta (los de las listas revisadas, mas los nlist
centroides), que es lo que el IVF se ahorra frente a la fuerza bruta.

Mientras no este vectores.npy (#57) se usan vectores sinteticos de dimension 256
agrupados alrededor de 100 centros con semilla fija, porque los histogramas de
palabras visuales de imagenes parecidas tambien forman grupos.

Salida: datos/resultados/ivf_bench.csv
"""

import argparse
import csv
import math
import os
import sys
import time

import faiss
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from multimedia.indices import IndiceVectorial  # noqa: E402
from multimedia.test_indices import fuerza_bruta, recall  # noqa: E402

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAMANOS = (1_000, 10_000, 100_000)
NPROBES = (1, 2, 4, 8, 16, 32)
K = 10


def sinteticos(n, dimension=256, grupos=100, semilla=2026):
    rng = np.random.default_rng(semilla)
    centros = rng.standard_normal((grupos, dimension)) * 3
    x = centros[rng.integers(0, grupos, n)] + rng.standard_normal((n, dimension))
    return np.abs(x).astype(np.float32)  # los histogramas TF-IDF no tienen componentes negativas


def medir(consultas, buscar):
    """Tiempo promedio por consulta en ms y los resultados."""
    inicio = time.perf_counter()
    resultados = [buscar(c) for c in consultas]
    return (time.perf_counter() - inicio) * 1000 / len(consultas), resultados


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--vectores", help="vectores.npy float32 (n, K) en el orden de los id")
    parser.add_argument("--metrica", default="cosine", choices=("euclidean", "inner_product", "cosine"))
    parser.add_argument("--consultas", type=int, default=100)
    parser.add_argument("--salida", default=os.path.join(RAIZ, "datos", "resultados", "ivf_bench.csv"))
    args = parser.parse_args()

    if args.vectores:
        todos = np.load(args.vectores).astype(np.float32)
        origen = os.path.basename(args.vectores)
    else:
        todos = sinteticos(max(TAMANOS))
        origen = "sinteticos"
    faiss.omp_set_num_threads(1)  # un hilo, para comparar con la fuerza bruta en igualdad
    rng = np.random.default_rng(7)

    filas = []
    for n in TAMANOS:
        if n > len(todos):
            print(f"se omite n = {n}: solo hay {len(todos)} vectores")
            continue
        vectores = todos[:n]  # subconjuntos anidados, como los CSV
        ids = np.arange(1, n + 1)
        consultas = vectores[rng.choice(n, args.consultas, replace=False)]
        base = {"origen": origen, "n": n, "dimension": vectores.shape[1], "metrica": args.metrica}

        t_bruta, exactos = medir(consultas, lambda c: fuerza_bruta(vectores, ids, c, K, args.metrica))
        filas.append({**base, "estructura": "fuerza_bruta", "nlist": "", "nprobe": "", "construccion_ms": 0,
                      "consulta_ms": t_bruta, "recall10": 1.0, "vectores_comparados": n,
                      "bytes": vectores.nbytes})

        nlist = round(math.sqrt(n))  # regla usual: nlist ~ raiz de n
        inicio = time.perf_counter()
        indice = IndiceVectorial("IVF", args.metrica, nlist=nlist).construir(vectores, ids)
        t_construccion = (time.perf_counter() - inicio) * 1000
        for nprobe in (p for p in NPROBES if p <= nlist):
            indice.parametros["nprobe"] = nprobe
            faiss.cvar.indexIVF_stats.reset()
            t_ivf, aproximados = medir(consultas, lambda c: indice.knn(c, K))
            comparados = faiss.cvar.indexIVF_stats.ndis / len(consultas) + nlist
            r = np.mean([recall(a, e) for a, e in zip(aproximados, exactos)])
            filas.append({**base, "estructura": "ivf", "nlist": nlist, "nprobe": nprobe,
                          "construccion_ms": t_construccion, "consulta_ms": t_ivf, "recall10": r,
                          "vectores_comparados": comparados, "bytes": indice.tamano_en_disco()})
            print(f"n={n:>6} nlist={nlist:>3} nprobe={nprobe:>2}: {t_ivf:7.3f} ms (bruta {t_bruta:7.3f} ms) "
                  f"recall@10={r:.2f} comparados={comparados:,.0f}")

    os.makedirs(os.path.dirname(args.salida), exist_ok=True)
    with open(args.salida, "w", encoding="utf-8", newline="") as f:
        columnas = ["origen", "estructura", "n", "dimension", "metrica", "nlist", "nprobe", "construccion_ms",
                    "consulta_ms", "recall10", "vectores_comparados", "bytes"]
        escritor = csv.DictWriter(f, columnas, lineterminator="\n")
        escritor.writeheader()
        for fila in filas:
            escritor.writerow({c: round(v, 4) if isinstance(v, float) else v for c, v in fila.items()})
    print("resultados en", args.salida)


if __name__ == "__main__":
    main()
