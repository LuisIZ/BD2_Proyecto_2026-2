"""Extracción SIFT → palabras visuales FAISS → histogramas TF-IDF.

El entrenamiento conserva como máximo ``muestra_descriptores`` descriptores; los
descriptores de cada imagen se descartan después de cuantizar su histograma.
"""

import argparse
import csv
import hashlib
import multiprocessing
import os
import random
import tarfile
import time
from pathlib import Path

import cv2
import faiss
import numpy as np

K_DEFAULT = 256
MAX_DESCRIPTORES_POR_IMAGEN = 500
MUESTRA_DESCRIPTORES_DEFAULT = 1_000_000
RAIZ = Path(__file__).resolve().parents[2]
VOCABULARIOS = RAIZ / "datos" / "productos"

_WORKER_VOCABULARIO = None
_WORKER_INDICE = None
_WORKER_K = 0
_WORKER_MAX_DESCRIPTORES = 0


def _leer_imagen(ruta):
    imagen = cv2.imread(os.fspath(ruta), cv2.IMREAD_GRAYSCALE)
    if imagen is None:
        raise ValueError(f"no se pudo leer la imagen: {ruta}")
    return imagen


def extraer_descriptores(ruta, max_descriptores=MAX_DESCRIPTORES_POR_IMAGEN):
    """Devuelve hasta N descriptores SIFT float32 de 128 dimensiones."""
    if max_descriptores <= 0:
        raise ValueError("max_descriptores debe ser mayor que cero")
    imagen = _leer_imagen(ruta)
    sift = cv2.SIFT_create(nfeatures=max_descriptores)
    _, descriptores = sift.detectAndCompute(imagen, None)
    if descriptores is None:
        return np.empty((0, 128), dtype=np.float32)
    return np.ascontiguousarray(descriptores[:max_descriptores], dtype=np.float32)


def _indice_vocabulario(vocabulario):
    vocabulario = np.ascontiguousarray(vocabulario, dtype=np.float32)
    if vocabulario.ndim != 2 or vocabulario.shape[1] != 128 or vocabulario.shape[0] == 0:
        raise ValueError("el vocabulario debe tener forma (K, 128) y K > 0")
    indice = faiss.IndexFlatL2(128)
    indice.add(vocabulario)
    return vocabulario, indice


def _histograma(descriptores, indice, k):
    histograma = np.zeros(k, dtype=np.float32)
    if len(descriptores):
        _, etiquetas = indice.search(np.ascontiguousarray(descriptores, dtype=np.float32), 1)
        histograma = np.bincount(etiquetas[:, 0], minlength=k).astype(np.float32)
    return histograma


def _aplicar_tfidf(histogramas, idf):
    histogramas = np.asarray(histogramas, dtype=np.float32)
    idf = np.asarray(idf, dtype=np.float32)
    if histogramas.ndim != 2 or histogramas.shape[1] != len(idf):
        raise ValueError("las dimensiones de histogramas e IDF no coinciden")
    tf = np.zeros_like(histogramas)
    presentes = histogramas > 0
    tf[presentes] = 1.0 + np.log(histogramas[presentes])
    vectores = tf * idf[None, :]
    normas = np.linalg.norm(vectores, axis=1, keepdims=True)
    np.divide(vectores, normas, out=vectores, where=normas > 0)
    return vectores.astype(np.float32, copy=False)


def _k_configurado():
    return int(os.environ.get("BD2_K", K_DEFAULT))


def _ruta_vocabulario(k=None):
    k = _k_configurado() if k is None else k
    return VOCABULARIOS / f"vocabulario_{k}.npy"


def _ruta_idf(k=None):
    k = _k_configurado() if k is None else k
    return VOCABULARIOS / f"idf_{k}.npy"


def _cargar_artifacts(vocabulario=None, idf=None):
    if vocabulario is None:
        ruta = Path(os.environ.get("BD2_VOCABULARIO", _ruta_vocabulario()))
        if not ruta.is_file():
            raise FileNotFoundError(f"no existe el vocabulario visual: {ruta}")
        vocabulario = np.load(ruta, allow_pickle=False)
    if idf is None:
        ruta = Path(os.environ.get("BD2_IDF_VISUAL", _ruta_idf(len(vocabulario))))
        if not ruta.is_file():
            raise FileNotFoundError(f"no existe el IDF visual de la coleccion: {ruta}")
        idf = np.load(ruta, allow_pickle=False)
    vocabulario, indice = _indice_vocabulario(vocabulario)
    idf = np.asarray(idf, dtype=np.float32)
    if idf.shape != (vocabulario.shape[0],):
        raise ValueError("el vector IDF debe tener exactamente K componentes")
    return vocabulario, indice, idf


def extract_features(ruta) -> np.ndarray:
    """Extrae un vector TF-IDF normalizado de dimensión K para una imagen."""
    vocabulario, indice, idf = _cargar_artifacts()
    descriptores = extraer_descriptores(ruta)
    histograma = _histograma(descriptores, indice, vocabulario.shape[0])[None, :]
    return _aplicar_tfidf(histograma, idf)[0]


def _iniciar_worker(vocabulario, max_descriptores):
    global _WORKER_VOCABULARIO, _WORKER_INDICE, _WORKER_K, _WORKER_MAX_DESCRIPTORES
    _WORKER_VOCABULARIO, _WORKER_INDICE = _indice_vocabulario(vocabulario)
    _WORKER_K = _WORKER_VOCABULARIO.shape[0]
    _WORKER_MAX_DESCRIPTORES = max_descriptores


def _histograma_archivado(tarea):
    ruta, ruta_cache = tarea
    ruta = Path(ruta)
    ruta_cache = Path(ruta_cache) if ruta_cache else None
    if ruta_cache is not None and ruta_cache.is_file():
        histograma = np.load(ruta_cache, allow_pickle=False)
        if histograma.shape == (_WORKER_K,):
            return histograma

    descriptores = extraer_descriptores(ruta, _WORKER_MAX_DESCRIPTORES)
    histograma = _histograma(descriptores, _WORKER_INDICE, _WORKER_K)
    if ruta_cache is not None:
        ruta_cache.parent.mkdir(parents=True, exist_ok=True)
        temporal = ruta_cache.with_suffix(ruta_cache.suffix + f".{os.getpid()}.tmp")
        with temporal.open("wb") as archivo:
            np.save(archivo, histograma, allow_pickle=False)
        os.replace(temporal, ruta_cache)
    return histograma


def _cache_path(ruta, cache_dir, hash_vocabulario):
    if cache_dir is None:
        return ""
    ruta = Path(ruta)
    try:
        stat = ruta.stat()
        identificador = f"{ruta.resolve()}:{stat.st_size}:{stat.st_mtime_ns}:{hash_vocabulario}"
    except OSError as exc:
        raise FileNotFoundError(f"no se pudo consultar la imagen {ruta}: {exc}") from exc
    nombre = hashlib.sha256(identificador.encode("utf-8")).hexdigest() + ".npy"
    return os.fspath(Path(cache_dir) / nombre)


def calcular_vectores(imagenes, vocabulario, procesos=None, cache_dir=None,
                      max_descriptores=MAX_DESCRIPTORES_POR_IMAGEN):
    """Calcula histogramas por imagen sin retener descriptores entre imágenes."""
    rutas = [os.fspath(ruta) for ruta in imagenes]
    if not rutas:
        raise ValueError("la coleccion no contiene imagenes")
    if max_descriptores <= 0:
        raise ValueError("max_descriptores debe ser mayor que cero")
    vocabulario, _ = _indice_vocabulario(vocabulario)
    huella_vocabulario = hashlib.sha256(vocabulario.tobytes()).hexdigest()
    tareas = [(ruta, _cache_path(ruta, cache_dir, huella_vocabulario)) for ruta in rutas]
    if procesos is not None and procesos <= 0:
        raise ValueError("procesos debe ser mayor que cero")
    procesos = min(len(rutas), procesos or max(1, min(8, multiprocessing.cpu_count() - 1)))
    if procesos <= 1:
        _iniciar_worker(vocabulario, max_descriptores)
        histogramas = [_histograma_archivado(tarea) for tarea in tareas]
    else:
        with multiprocessing.Pool(
            processes=procesos,
            initializer=_iniciar_worker,
            initargs=(vocabulario, max_descriptores),
        ) as pool:
            histogramas = list(pool.imap(_histograma_archivado, tareas, chunksize=1))
    matriz = np.vstack(histogramas).astype(np.float32, copy=False)
    df = np.count_nonzero(matriz, axis=0).astype(np.float32)
    idf = np.log((len(matriz) + 1.0) / (df + 1.0)) + 1.0
    return matriz, idf.astype(np.float32)


def entrenar_vocabulario(imagenes, k=K_DEFAULT, muestra_descriptores=MUESTRA_DESCRIPTORES_DEFAULT,
                          procesos=None, seed=2026, max_descriptores=MAX_DESCRIPTORES_POR_IMAGEN):
    """Entrena FAISS K-means con una muestra acotada y aleatoria de descriptores SIFT."""
    rutas = [os.fspath(ruta) for ruta in imagenes]
    if not rutas:
        raise ValueError("la coleccion no contiene imagenes")
    if k <= 0 or muestra_descriptores < k:
        raise ValueError("se requiere k > 0 y una muestra con al menos k descriptores")
    if max_descriptores <= 0:
        raise ValueError("max_descriptores debe ser mayor que cero")
    random.Random(seed).shuffle(rutas)
    if procesos is not None and procesos <= 0:
        raise ValueError("procesos debe ser mayor que cero")
    procesos = min(len(rutas), procesos or max(1, min(8, multiprocessing.cpu_count() - 1)))
    muestra = []
    total = 0

    def agregar_muestra(descriptores):
        nonlocal total
        if len(descriptores) == 0 or total >= muestra_descriptores:
            return
        disponible = muestra_descriptores - total
        if len(descriptores) > disponible:
            indices = np.random.default_rng(seed + total).choice(len(descriptores), disponible, replace=False)
            descriptores = descriptores[indices]
        muestra.append(np.ascontiguousarray(descriptores, dtype=np.float32))
        total += len(descriptores)

    if procesos <= 1:
        for ruta in rutas:
            agregar_muestra(extraer_descriptores(ruta, max_descriptores))
            if total >= muestra_descriptores:
                break
    else:
        with multiprocessing.Pool(processes=procesos) as pool:
            for descriptores in pool.imap(
                _extraer_descriptores_worker,
                ((ruta, max_descriptores) for ruta in rutas),
                chunksize=1,
            ):
                agregar_muestra(descriptores)
                if total >= muestra_descriptores:
                    pool.terminate()
                    break
    if total < k:
        raise ValueError(f"solo se extrajeron {total} descriptores validos; se necesitan al menos k={k}")
    muestra_np = np.ascontiguousarray(np.vstack(muestra), dtype=np.float32)
    kmeans = faiss.Kmeans(128, k, niter=25, seed=seed, verbose=False)
    kmeans.train(muestra_np)
    return np.ascontiguousarray(kmeans.centroids, dtype=np.float32), total


def _extraer_descriptores_worker(tarea):
    ruta, max_descriptores = tarea
    return extraer_descriptores(ruta, max_descriptores)


def resolver_imagen(imagen, raiz_imagenes):
    """Resuelve imagen relativa bajo layouts extraídos comunes del tar ABO."""
    imagen = Path(imagen)
    raiz = Path(raiz_imagenes)
    if imagen.is_absolute() and imagen.is_file():
        return imagen
    candidatos = (
        raiz / imagen,
        raiz / "images" / "small" / imagen,
        raiz / "small" / imagen,
        raiz / "images" / imagen,
    )
    for candidato in candidatos:
        if candidato.is_file():
            return candidato
    raise FileNotFoundError(f"no se encontro {imagen} bajo {raiz}; extrae images/small del tar ABO")


def extraer_imagenes_tar(csv_productos, archivo_tar, destino, limite=None):
    """Extrae solo las rutas solicitadas por el CSV sin desempaquetar el tar completo."""
    if limite is not None and limite <= 0:
        raise ValueError("limite debe ser mayor que cero")
    destino = Path(destino)
    with open(csv_productos, encoding="utf-8-sig", newline="") as archivo:
        filas = csv.DictReader(archivo)
        if not filas.fieldnames or "imagen" not in filas.fieldnames:
            raise ValueError("el CSV debe incluir la columna imagen")
        rutas = set()
        for fila in filas:
            ruta = (fila.get("imagen") or "").replace("\\", "/").lstrip("/")
            if ruta and ".." not in Path(ruta).parts:
                rutas.add(ruta)
            if limite and filas.line_num >= limite + 1:
                break
    faltantes = set(rutas)
    por_nombre = {}
    for ruta in rutas:
        por_nombre.setdefault(Path(ruta).name, set()).add(ruta)
    extraidas = 0
    with tarfile.open(archivo_tar, "r:*") as tar:
        for miembro in tar:
            nombre = miembro.name.replace("\\", "/")
            prefijo = "images/small/"
            if not miembro.isfile() or not nombre.startswith(prefijo):
                continue
            ruta_relativa = nombre[len(prefijo):]
            destino_relativo = ruta_relativa if ruta_relativa in faltantes else ""
            if not destino_relativo:
                candidatos = por_nombre.get(Path(ruta_relativa).name, set()).intersection(faltantes)
                if len(candidatos) != 1:
                    continue
                destino_relativo = next(iter(candidatos))
            origen = tar.extractfile(miembro)
            if origen is None:
                raise OSError(f"no se pudo leer el miembro del tar: {miembro.name}")
            salida = destino / destino_relativo
            salida.parent.mkdir(parents=True, exist_ok=True)
            with origen, salida.open("wb") as salida_archivo:
                while True:
                    bloque = origen.read(1024 * 1024)
                    if not bloque:
                        break
                    salida_archivo.write(bloque)
            faltantes.remove(destino_relativo)
            extraidas += 1
            if not faltantes:
                break
    return extraidas, sorted(faltantes)


def procesar_csv(csv_productos, raiz_imagenes, k=K_DEFAULT, limite=None, procesos=None,
                 vocabulario_path=None, cache_dir=None, salida=None, archivo_tar=None,
                 muestra_descriptores=MUESTRA_DESCRIPTORES_DEFAULT, idf_path=None):
    inicio_total = time.perf_counter()
    if limite is not None and limite <= 0:
        raise ValueError("limite debe ser mayor que cero")
    with open(csv_productos, encoding="utf-8-sig", newline="") as archivo:
        filas = list(csv.DictReader(archivo))
    if not filas or not {"id", "imagen"}.issubset(filas[0]):
        raise ValueError("el CSV debe incluir las columnas id e imagen")
    if limite is not None:
        filas = filas[:limite]
    ids, rutas = [], []
    raiz_imagenes = Path(raiz_imagenes)
    if archivo_tar:
        extraidos, faltantes = extraer_imagenes_tar(csv_productos, archivo_tar, raiz_imagenes, limite)
        if faltantes:
            raise FileNotFoundError(f"faltan {len(faltantes)} rutas en el tar; ejemplos: {faltantes[:5]}")
        print(f"imagenes extraidas del tar: {extraidos}")
    for fila in filas:
        ids.append(int(fila["id"]))
        rutas.append(resolver_imagen(fila["imagen"], raiz_imagenes))

    vocabulario_path = Path(vocabulario_path or _ruta_vocabulario(k))
    idf_path = Path(idf_path or _ruta_idf(k))
    if vocabulario_path.is_file():
        vocabulario = np.load(vocabulario_path, allow_pickle=False)
    else:
        vocabulario, muestra = entrenar_vocabulario(
            rutas, k=k, muestra_descriptores=muestra_descriptores, procesos=procesos
        )
        vocabulario_path.parent.mkdir(parents=True, exist_ok=True)
        np.save(vocabulario_path, vocabulario, allow_pickle=False)
        print(f"vocabulario entrenado con {muestra:,} descriptores: {vocabulario_path}")

    t_inicio = time.perf_counter()
    histogramas, idf = calcular_vectores(rutas, vocabulario, procesos=procesos, cache_dir=cache_dir)
    idf_path.parent.mkdir(parents=True, exist_ok=True)
    np.save(idf_path, idf, allow_pickle=False)
    vectores = _aplicar_tfidf(histogramas, idf)
    tiempo = time.perf_counter() - inicio_total
    prefijo = Path(salida) if salida else VOCABULARIOS / f"vectores_{len(filas)}"
    prefijo.parent.mkdir(parents=True, exist_ok=True)
    np.save(prefijo.with_suffix(".npy"), vectores, allow_pickle=False)
    with prefijo.with_suffix(".csv").open("w", encoding="utf-8", newline="") as archivo:
        escritor = csv.writer(archivo, lineterminator="\n")
        escritor.writerow(["id", "vector"])
        for id_fila, vector in zip(ids, vectores):
            escritor.writerow([id_fila, "VECTOR(" + ",".join(f"{float(x):.8g}" for x in vector) + ")"])
    print(f"histogramas calculados en {time.perf_counter() - t_inicio:.2f} s")
    print(f"total {len(ids):,} imagenes en {tiempo:.2f} s; vectores: {prefijo.with_suffix('.npy')}")
    return np.asarray(ids, dtype=np.int64), vectores, tiempo


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--csv", required=True, help="CSV productos_1k.csv o productos_10k.csv")
    parser.add_argument("--imagenes", required=True, help="raiz de imagenes extraidas")
    parser.add_argument("--archivo-tar", help="tar ABO opcional; extrae solo las imagenes del CSV")
    parser.add_argument("--k", type=int, default=K_DEFAULT)
    parser.add_argument("--limite", type=int)
    parser.add_argument("--procesos", type=int)
    parser.add_argument("--muestra-descriptores", type=int, default=MUESTRA_DESCRIPTORES_DEFAULT)
    parser.add_argument("--vocabulario")
    parser.add_argument("--idf", help="ruta del archivo idf_K.npy")
    parser.add_argument("--cache", default=os.fspath(VOCABULARIOS / "cache"))
    parser.add_argument("--salida")
    args = parser.parse_args()
    procesar_csv(
        args.csv, args.imagenes, args.k, args.limite, args.procesos,
        args.vocabulario, args.cache, args.salida, args.archivo_tar, args.muestra_descriptores,
        args.idf,
    )


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
