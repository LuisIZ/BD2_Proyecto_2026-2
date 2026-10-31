"""Compara FAISS Flat exacto, IVF y HNSW sobre vectores TF-IDF de imágenes."""

import argparse
import csv
import html
import math
import os
import sys
import tempfile
import time
from pathlib import Path

import faiss
import numpy as np

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, os.fspath(RAIZ / "api"))
from multimedia.indices import IndiceVectorial

TAMANOS_DEFAULT = (1_000, 10_000)
METRICAS_DEFAULT = ("euclidean", "cosine")
K = 10


def _preparar(vectores, metrica):
    x = np.ascontiguousarray(vectores, dtype=np.float32)
    if metrica == "cosine":
        faiss.normalize_L2(x)
    return x


def _consultas(vectores, cantidad, seed):
    rng = np.random.default_rng(seed)
    indices = rng.choice(len(vectores), size=min(cantidad, len(vectores)), replace=False)
    ruido = rng.normal(0, 0.01, (len(indices), vectores.shape[1])).astype(np.float32)
    return np.ascontiguousarray(vectores[indices] + ruido, dtype=np.float32)


def _recall(obtenidos, exactos):
    valores = []
    for obtenido, exacto in zip(obtenidos, exactos):
        conjunto = set(exacto)
        valores.append(len(conjunto.intersection(obtenido)) / len(conjunto) if conjunto else 1.0)
    return float(np.mean(valores)) if valores else 1.0


def _tamano_disco(indice):
    with tempfile.TemporaryDirectory() as carpeta:
        ruta = os.path.join(carpeta, "indice.faiss")
        faiss.write_index(indice, ruta)
        return os.path.getsize(ruta)


def benchmark_dataset(vectores, ids=None, metrica="euclidean", consultas=50, k=K, seed=7):
    """Devuelve mediciones para una colección; Flat FAISS es el ground truth exacto."""
    vectores = np.ascontiguousarray(vectores, dtype=np.float32)
    if vectores.ndim != 2 or not len(vectores) or not vectores.shape[1]:
        raise ValueError("vectores debe tener forma (n, dimension) con n y dimension mayores que cero")
    if metrica not in ("euclidean", "cosine"):
        raise ValueError("metrica debe ser euclidean o cosine")
    ids = np.asarray(ids if ids is not None else np.arange(1, len(vectores) + 1), dtype=np.int64)
    if len(ids) != len(vectores) or len(np.unique(ids)) != len(ids):
        raise ValueError("se requiere un id unico por vector")
    k = min(k, len(vectores))
    consultas_np = _consultas(vectores, consultas, seed)
    x = _preparar(vectores, metrica)
    q = _preparar(consultas_np, metrica)
    dimension = x.shape[1]

    if metrica == "euclidean":
        exacto = faiss.IndexFlatL2(dimension)
    else:
        exacto = faiss.IndexFlatIP(dimension)
    inicio = time.perf_counter()
    exacto.add(x)
    flat_construccion_ms = (time.perf_counter() - inicio) * 1000
    inicio = time.perf_counter()
    _, posiciones = exacto.search(q, k)
    flat_consulta_ms = (time.perf_counter() - inicio) * 1000 / len(q)
    exactos = [ids[posiciones[i]].tolist() for i in range(len(q))]
    filas = [{
        "n": len(x), "dimension": dimension, "metrica": metrica, "indice": "secuencial_exact",
        "parametros": "IndexFlatL2" if metrica == "euclidean" else "IndexFlatIP + normalizacion",
        "construccion_ms": flat_construccion_ms, "consulta_ms": flat_consulta_ms,
        "recall_at_10": 1.0, "memoria_index_bytes": int(faiss.serialize_index(exacto).nbytes),
        "disco_index_bytes": _tamano_disco(exacto),
    }]

    nlist = min(max(1, int(math.sqrt(len(x)))), max(1, len(x) // 40))
    ivf = IndiceVectorial("IVF", metrica, nlist=nlist, nprobe=1)
    inicio = time.perf_counter()
    ivf.construir(vectores, ids)
    construccion_ivf = (time.perf_counter() - inicio) * 1000
    for nprobe in sorted({1, min(8, nlist), nlist}):
        ivf.parametros["nprobe"] = nprobe
        inicio = time.perf_counter()
        resultados = [ivf.knn(consulta, k) for consulta in consultas_np]
        tiempo = (time.perf_counter() - inicio) * 1000 / len(consultas_np)
        obtenidos = [[id_doc for id_doc, _ in resultado] for resultado in resultados]
        filas.append({
            "n": len(x), "dimension": dimension, "metrica": metrica, "indice": "IVF",
            "parametros": f"nlist={nlist}, nprobe={nprobe}", "construccion_ms": construccion_ivf,
            "consulta_ms": tiempo, "recall_at_10": _recall(obtenidos, exactos),
            "memoria_index_bytes": ivf.tamano_en_disco(),
            "disco_index_bytes": _tamano_disco(ivf.indice),
        })

    hnsw = IndiceVectorial("HNSW", metrica, M=32, ef_construccion=100, ef_busqueda=16)
    inicio = time.perf_counter()
    hnsw.construir(vectores, ids)
    construccion_hnsw = (time.perf_counter() - inicio) * 1000
    for ef in sorted({16, 64, min(128, len(x))}):
        hnsw.parametros["ef_busqueda"] = ef
        inicio = time.perf_counter()
        resultados = [hnsw.knn(consulta, k) for consulta in consultas_np]
        tiempo = (time.perf_counter() - inicio) * 1000 / len(consultas_np)
        obtenidos = [[id_doc for id_doc, _ in resultado] for resultado in resultados]
        filas.append({
            "n": len(x), "dimension": dimension, "metrica": metrica, "indice": "HNSW",
            "parametros": f"M=32, ef_construccion=100, ef_busqueda={ef}",
            "construccion_ms": construccion_hnsw, "consulta_ms": tiempo,
            "recall_at_10": _recall(obtenidos, exactos),
            "memoria_index_bytes": hnsw.tamano_en_disco(),
            "disco_index_bytes": _tamano_disco(hnsw.indice),
        })
    return filas


def vectores_sinteticos(n, dimension=128, seed=2026):
    rng = np.random.default_rng(seed)
    centros = rng.standard_normal((min(100, n), dimension)).astype(np.float32)
    asignaciones = rng.integers(0, len(centros), n)
    return np.ascontiguousarray(centros[asignaciones] + rng.normal(0, 0.15, (n, dimension)), dtype=np.float32)


def _graficar_svg(filas, destino, metrica, campo, titulo):
    series = {}
    for fila in filas:
        if fila["metrica"] != metrica or fila["indice"] == "secuencial_exact":
            continue
        series.setdefault(fila["indice"], {}).setdefault(fila["n"], []).append(float(fila[campo]))
    for fila in filas:
        if fila["metrica"] == metrica and fila["indice"] == "secuencial_exact":
            series.setdefault("secuencial_exact", {}).setdefault(fila["n"], []).append(float(fila[campo]))
    if not series:
        return
    width, height = 800, 440
    left, right, top, bottom = 75, 35, 40, 65
    ys = [value for datos in series.values() for grupo in datos.values() for value in grupo]
    ymax = max(ys) or 1.0
    ns = sorted({n for datos in series.values() for n in datos})
    xmin, xmax = math.log10(min(ns)), math.log10(max(ns)) if len(ns) > 1 else math.log10(min(ns) * 10)
    px = lambda n: left + (math.log10(n) - xmin) / (xmax - xmin) * (width - left - right)
    py = lambda y: height - bottom - y / ymax * (height - top - bottom)
    colores = {"secuencial_exact": "#555", "IVF": "#1677c8", "HNSW": "#e87519"}
    elementos = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
                 '<rect width="100%" height="100%" fill="white"/>',
                 f'<text x="{width/2}" y="24" text-anchor="middle" font-family="sans-serif" font-size="16">{titulo} ({metrica})</text>',
                 f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="#333"/>',
                 f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="#333"/>']
    for n in ns:
        x = px(n)
        elementos.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{height-bottom}" stroke="#ddd"/>')
        elementos.append(f'<text x="{x:.1f}" y="{height-bottom+24}" text-anchor="middle" font-family="sans-serif" font-size="12">{n:,}</text>')
    for nombre, datos in series.items():
        puntos = []
        for n in ns:
            if n in datos:
                puntos.append((px(n), py(float(np.mean(datos[n])))))
        elementos.append(f'<polyline fill="none" stroke="{colores[nombre]}" stroke-width="2" points="{" ".join(f"{x:.1f},{y:.1f}" for x,y in puntos)}"/>')
        for x, y in puntos:
            elementos.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{colores[nombre]}"/>')
    elementos.append(f'<text x="{width/2}" y="{height-12}" text-anchor="middle" font-family="sans-serif" font-size="12">documentos (escala log)</text>')
    elementos.append(f'<text x="18" y="{height/2}" transform="rotate(-90 18,{height/2})" text-anchor="middle" font-family="sans-serif" font-size="12">{campo}</text>')
    y_leyenda = top + 8
    for i, nombre in enumerate(series):
        x = left + i * 155
        elementos.append(f'<line x1="{x}" y1="{y_leyenda}" x2="{x+25}" y2="{y_leyenda}" stroke="{colores[nombre]}" stroke-width="3"/>')
        elementos.append(f'<text x="{x+30}" y="{y_leyenda+4}" font-family="sans-serif" font-size="11">{nombre}</text>')
    elementos.append("</svg>")
    destino.parent.mkdir(parents=True, exist_ok=True)
    destino.write_text("\n".join(elementos), encoding="utf-8")


def generar_galeria(vectores, ids, metrica, csv_productos, raiz_imagenes, destino,
                    cantidad_consultas=8, seed=7):
    """Escribe una galería HTML con vecinos exactos y aproximados, marcando aciertos/errores."""
    metadatos = {}
    with open(csv_productos, encoding="utf-8-sig", newline="") as archivo:
        lector = csv.DictReader(archivo)
        if not lector.fieldnames or not {"id", "nombre", "imagen"}.issubset(lector.fieldnames):
            raise ValueError("el CSV de productos requiere columnas id, nombre e imagen")
        for fila in lector:
            try:
                metadatos[int(fila["id"])] = (fila.get("nombre", ""), fila.get("imagen", ""))
            except (KeyError, ValueError):
                raise ValueError("el CSV de productos requiere columnas id, nombre e imagen")
    vectores = np.ascontiguousarray(vectores, dtype=np.float32)
    ids = np.asarray(ids, dtype=np.int64)
    if len(ids) != len(vectores):
        raise ValueError("se requiere un id por vector para crear la galeria")
    n = len(vectores)
    cantidad_consultas = min(cantidad_consultas, n)
    rng = np.random.default_rng(seed)
    posiciones = rng.choice(n, size=cantidad_consultas, replace=False)
    q = np.ascontiguousarray(
        vectores[posiciones] + rng.normal(0, 0.01, (cantidad_consultas, vectores.shape[1])).astype(np.float32)
    )
    x = _preparar(vectores, metrica)
    q_preparadas = _preparar(q, metrica)
    exacto = faiss.IndexFlatL2(x.shape[1]) if metrica == "euclidean" else faiss.IndexFlatIP(x.shape[1])
    exacto.add(x)
    _, exactos_pos = exacto.search(q_preparadas, min(K, n))
    exactos = [ids[pos].tolist() for pos in exactos_pos]

    nlist = min(max(1, int(math.sqrt(n))), max(1, n // 40))
    ivf = IndiceVectorial("IVF", metrica, nlist=nlist, nprobe=min(8, nlist)).construir(vectores, ids)
    hnsw = IndiceVectorial("HNSW", metrica, M=32, ef_construccion=100, ef_busqueda=64).construir(vectores, ids)

    destino = Path(destino)
    destino.parent.mkdir(parents=True, exist_ok=True)
    raiz_imagenes = Path(raiz_imagenes)

    def tarjeta_imagen(id_imagen, subtitulo):
        nombre, relativo = metadatos.get(int(id_imagen), (f"ID {id_imagen}", ""))
        archivo = raiz_imagenes / relativo
        if archivo.is_file():
            fuente = os.path.relpath(archivo, destino.parent).replace(os.sep, "/")
            return (f'<figure><img loading="lazy" src="{html.escape(fuente, quote=True)}" alt="">'
                    f'<figcaption>{html.escape(nombre)}<br>ID {int(id_imagen)} · {html.escape(subtitulo)}</figcaption></figure>')
        return f'<figure class="missing"><figcaption>{html.escape(nombre)}<br>ID {int(id_imagen)} · imagen no disponible</figcaption></figure>'

    partes = [
        "<!doctype html><html lang=\"es\"><meta charset=\"utf-8\">",
        f"<title>Galería de búsqueda {html.escape(metrica)}</title>",
        "<style>body{font:15px sans-serif;margin:2rem;color:#222}section{border-top:1px solid #ccc;padding:1rem 0}"
        ".fila{display:flex;gap:.6rem;overflow:auto}figure{width:150px;flex:none;margin:.2rem}"
        "img{width:145px;height:145px;object-fit:contain;background:#eee}figcaption{font-size:12px}"
        ".ok{color:#087a36}.error{color:#bd2d24}.missing{height:170px;background:#eee;display:grid;place-items:center}</style>",
        f"<h1>Galería de similitud — {html.escape(metrica)}</h1>",
        "<p>El ground truth se obtiene con FAISS Flat exacto; se muestran diez vecinos por método.</p>",
    ]
    for i, posicion in enumerate(posiciones):
        exactos_i = exactos[i]
        partes.append(f"<section><h2>Consulta ID {int(ids[posicion])}</h2><div class=\"fila\">"
                      + tarjeta_imagen(ids[posicion], "consulta") + "</div>")
        for nombre, indice in (("IVF", ivf), ("HNSW", hnsw)):
            aproximados = [id_doc for id_doc, _ in indice.knn(q[i], min(K, n))]
            recall = len(set(exactos_i).intersection(aproximados)) / len(exactos_i) if exactos_i else 1.0
            clase = "ok" if recall == 1.0 else "error"
            partes.append(f'<h3 class="{clase}">{nombre}: Recall@10 = {recall:.2f}</h3>'
                          '<h4>Vecinos devueltos</h4><div class="fila">')
            partes.extend(tarjeta_imagen(id_doc, "coincide" if id_doc in exactos_i else "error")
                          for id_doc in aproximados)
            partes.append('</div><h4>Ground truth exacto</h4><div class="fila">')
            partes.extend(tarjeta_imagen(id_doc, "exacto") for id_doc in exactos_i)
            partes.append("</div>")
        partes.append("</section>")
    partes.append("</html>")
    destino.write_text("\n".join(partes), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vectores", help="vectores.npy float32 (n, K); sin la opción se usan datos sintéticos")
    parser.add_argument("--ids", help="ids.npy con las claves primarias en el mismo orden")
    parser.add_argument("--tamanos", type=int, nargs="+", default=TAMANOS_DEFAULT)
    parser.add_argument("--metricas", nargs="+", choices=METRICAS_DEFAULT, default=METRICAS_DEFAULT)
    parser.add_argument("--consultas", type=int, default=50)
    parser.add_argument("--salida", default=os.fspath(RAIZ / "datos" / "resultados" / "vectorial_bench.csv"))
    parser.add_argument("--graficas", default=os.fspath(RAIZ / "docs" / "comparacion_vectorial" / "graficas"))
    parser.add_argument("--productos-csv", help="CSV productos_<n>.csv para la galeria")
    parser.add_argument("--imagenes", help="raiz de imagenes extraidas para la galeria")
    parser.add_argument("--galerias", default=os.fspath(RAIZ / "docs" / "comparacion_vectorial" / "galerias"))
    args = parser.parse_args()

    todos = np.load(args.vectores, allow_pickle=False).astype(np.float32) if args.vectores else vectores_sinteticos(max(args.tamanos))
    ids = np.load(args.ids, allow_pickle=False).astype(np.int64) if args.ids else np.arange(1, len(todos) + 1)
    if len(ids) != len(todos):
        parser.error("--ids debe tener un id por fila de --vectores")
    filas = []
    for n in args.tamanos:
        if n <= 0 or n > len(todos):
            parser.error(f"el tamaño {n} no es válido para {len(todos)} vectores")
        for metrica in args.metricas:
            filas.extend(benchmark_dataset(todos[:n], ids[:n], metrica, args.consultas))

    columnas = ["n", "dimension", "metrica", "indice", "parametros", "construccion_ms",
                "consulta_ms", "recall_at_10", "memoria_index_bytes", "disco_index_bytes"]
    salida = Path(args.salida)
    salida.parent.mkdir(parents=True, exist_ok=True)
    with salida.open("w", encoding="utf-8", newline="") as archivo:
        escritor = csv.DictWriter(archivo, fieldnames=columnas, lineterminator="\n")
        escritor.writeheader()
        escritor.writerows({col: round(fila[col], 5) if isinstance(fila[col], float) else fila[col]
                            for col in columnas} for fila in filas)
    graficas = Path(args.graficas)
    for metrica in args.metricas:
        for campo, nombre in (("construccion_ms", "construccion"), ("consulta_ms", "consulta")):
            _graficar_svg(filas, graficas / f"{nombre}_{metrica}.svg", metrica, campo, nombre)
        if args.productos_csv or args.imagenes:
            if not args.productos_csv or not args.imagenes:
                parser.error("--productos-csv y --imagenes se usan juntos")
            for n in args.tamanos:
                generar_galeria(
                    todos[:n], ids[:n], metrica, args.productos_csv, args.imagenes,
                    Path(args.galerias) / f"galeria_{n}_{metrica}.html", seed=7,
                )
    print(f"{len(filas)} mediciones guardadas en {salida}; graficas en {graficas}")


if __name__ == "__main__":
    main()
