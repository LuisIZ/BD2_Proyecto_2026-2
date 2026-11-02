"""Precision@10 y Recall@10 de TF-IDF, BM25 (motor) y GIN (PostgreSQL) con juicios de relevancia.

    python docs/comparacion_texto/relevancia.py

Un producto es relevante para una consulta si tiene el mismo `tipo` y su `categoria` termina
en la misma ruta (consultas_relevancia.csv). Ninguno de los dos campos está en la descripción
indexada, así que los juicios no favorecen a ninguna técnica. La ruta se compara por el final
porque cada marketplace de Amazon tiene su propio árbol (por ejemplo, Barstools aparece con
dos rutas distintas).

Entradas, sobre datos/productos_10k.csv:
  datos/resultados/texto_ranking.csv  (make bench-texto)
  datos/resultados/gin_ranking.csv    (datos/resultados/gin_bench.sql; opcional)
Salida: datos/resultados/texto_relevancia.csv
"""

import csv
from collections import defaultdict
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
RESULTADOS = RAIZ / "datos" / "resultados"
K = 10


def leer(ruta):
    with open(ruta, encoding="utf-8", newline="") as archivo:
        return list(csv.DictReader(archivo))


def main():
    productos = leer(RAIZ / "datos" / "productos_10k.csv")
    consultas = leer(Path(__file__).resolve().parent / "consultas_relevancia.csv")
    relevantes = {
        c["consulta"]: {
            p["id"] for p in productos
            if p["tipo"] == c["tipo"] and p["categoria"].endswith(c["categoria_termina_en"])
        }
        for c in consultas
    }

    # tecnica -> consulta -> ids del top 10 en orden
    rankings = defaultdict(lambda: defaultdict(list))
    for fila in leer(RESULTADOS / "texto_ranking.csv"):
        rankings[fila["tecnica"]][fila["consulta"]].append(fila["id"])
    gin = RESULTADOS / "gin_ranking.csv"
    if gin.exists():
        for fila in sorted(leer(gin), key=lambda f: (f["consulta"], int(f["posicion"]))):
            rankings["gin"][fila["consulta"]].append(fila["id"])
    else:
        print(f"falta {gin.relative_to(RAIZ)}: se evalúan solo TF-IDF y BM25")

    filas = []
    for tecnica, por_consulta in rankings.items():
        for c in consultas:
            rel = relevantes[c["consulta"]]
            aciertos = len(set(por_consulta[c["consulta"]][:K]) & rel)
            filas.append({
                "tecnica": tecnica,
                "consulta": c["consulta"],
                "texto": c["texto"],
                "relevantes": len(rel),
                "aciertos": aciertos,
                # si devuelve menos de 10, las posiciones vacías cuentan como no relevantes
                "p10": aciertos / K,
                "r10": aciertos / len(rel),
                # con más de 10 relevantes, el recall en 10 posiciones no puede llegar a 1
                "r10_max": min(K, len(rel)) / len(rel),
            })

    salida = RESULTADOS / "texto_relevancia.csv"
    with open(salida, "w", encoding="utf-8", newline="") as archivo:
        escritor = csv.DictWriter(archivo, list(filas[0]), lineterminator="\n")
        escritor.writeheader()
        for fila in filas:
            escritor.writerow({k: round(v, 4) if isinstance(v, float) else v for k, v in fila.items()})

    print(f"{'técnica':8} {'P@10':>6} {'R@10':>6}  (R@10 máximo posible {sum(f['r10_max'] for f in filas if f['tecnica'] == filas[0]['tecnica']) / len(consultas):.3f})")
    for tecnica in rankings:
        mias = [f for f in filas if f["tecnica"] == tecnica]
        print(f"{tecnica:8} {sum(f['p10'] for f in mias) / len(mias):6.3f} {sum(f['r10'] for f in mias) / len(mias):6.3f}")
    print("resultados en", salida.relative_to(RAIZ))


if __name__ == "__main__":
    main()
