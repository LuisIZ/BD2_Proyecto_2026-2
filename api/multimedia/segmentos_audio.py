import argparse
import csv
import multiprocessing
import os
import time
from pathlib import Path

import faiss
import librosa
import numpy as np

from multimedia.extraer import K_DEFAULT, MUESTRA_DESCRIPTORES_DEFAULT, _aplicar_tfidf, _histograma, _indice_vocabulario
from multimedia.extraer_audio import CARPETA_AUDIO, DURACION_MAXIMA, N_MFCC_DEFAULT, RAIZ, extraer_descriptores_audio

HOP_DEFAULT = 5.0
DURACION_TRAMO_DEFAULT = 10.0
TOLERANCIA = 0.5
CSV_CANCIONES = RAIZ / "datos" / "audios_1k.csv"
AUDIOS_DEFAULT = Path(os.environ.get("BD2_DATOS", r"C:\datos_bd2")) / "audio" / "genres_original"


def inicios_de_tramos(total, duracion, hop):
    if duracion <= 0 or hop <= 0:
        raise ValueError("duracion y hop deben ser mayores que cero")
    if total <= duracion + TOLERANCIA:
        return [0.0]
    # un tramo cada hop segundos mientras quepa entero (con media seg. de tolerancia)
    inicios = []
    inicio = 0.0
    while inicio + duracion <= total + TOLERANCIA:
        inicios.append(inicio)
        inicio += hop
    return inicios


def normalizar(descriptores):
    # cada coeficiente queda con promedio 0 y desviacion 1 dentro del tramo: se quita el volumen
    if len(descriptores) == 0:
        return descriptores
    return ((descriptores - descriptores.mean(axis=0)) / (descriptores.std(axis=0) + 1e-6)).astype(np.float32)


def descriptores_de_tramos(ruta, duracion=DURACION_TRAMO_DEFAULT, hop=HOP_DEFAULT, n_mfcc=N_MFCC_DEFAULT,
                           maximo=DURACION_MAXIMA):
    total = min(librosa.get_duration(path=os.fspath(ruta)), maximo)
    tramos = []
    for inicio in inicios_de_tramos(total, duracion, hop):
        descriptores = extraer_descriptores_audio(ruta, n_mfcc=n_mfcc, offset=inicio, duracion=duracion)
        tramos.append((inicio, min(inicio + duracion, total), normalizar(descriptores)))
    return tramos


def _descriptores_worker(tarea):
    return descriptores_de_tramos(*tarea)


def rutas_vocabulario(carpeta=CARPETA_AUDIO, k=K_DEFAULT):
    vocabulario = os.environ.get("BD2_VOCABULARIO_COPYRIGHT", Path(carpeta) / f"vocabulario_copyright_{k}.npy")
    idf = os.environ.get("BD2_IDF_COPYRIGHT", Path(carpeta) / f"idf_copyright_{k}.npy")
    return Path(vocabulario), Path(idf)


def cargar_vocabulario(carpeta=CARPETA_AUDIO, k=K_DEFAULT, n_mfcc=N_MFCC_DEFAULT):
    ruta_vocabulario, ruta_idf = rutas_vocabulario(carpeta, k)
    if not ruta_vocabulario.is_file() or not ruta_idf.is_file():
        raise FileNotFoundError(f"falta {ruta_vocabulario} o {ruta_idf}: corre python -m multimedia.segmentos_audio")
    vocabulario, indice = _indice_vocabulario(np.load(ruta_vocabulario, allow_pickle=False), n_mfcc)
    return indice, np.load(ruta_idf, allow_pickle=False), len(vocabulario)


def vectores(descriptores, indice, idf, k):
    histogramas = np.vstack([_histograma(d, indice, k) for d in descriptores])
    return _aplicar_tfidf(histogramas, idf)


def entrenar_vocabulario(descriptores, k, muestra, seed=2026):
    todos = np.vstack([d for d in descriptores if len(d)])
    if len(todos) < k:
        raise ValueError(f"solo hay {len(todos)} ventanas; se necesitan al menos k={k}")
    if len(todos) > muestra:
        todos = todos[np.random.default_rng(seed).choice(len(todos), muestra, replace=False)]
    kmeans = faiss.Kmeans(todos.shape[1], k, niter=25, seed=seed, verbose=False)
    kmeans.train(np.ascontiguousarray(todos, dtype=np.float32))
    return np.ascontiguousarray(kmeans.centroids, dtype=np.float32)


def procesar_tramos(csv_canciones=CSV_CANCIONES, raiz_audios=AUDIOS_DEFAULT, hop=HOP_DEFAULT,
                    duracion=DURACION_TRAMO_DEFAULT, salida=CARPETA_AUDIO, procesos=None,
                    n_mfcc=N_MFCC_DEFAULT, limite=None, k=K_DEFAULT, muestra=MUESTRA_DESCRIPTORES_DEFAULT):
    inicio_total = time.perf_counter()
    with open(csv_canciones, encoding="utf-8-sig", newline="") as archivo:
        filas = list(csv.DictReader(archivo))
    if not filas or not {"id", "audio", "genero"}.issubset(filas[0]):
        raise ValueError("el CSV debe incluir las columnas id, audio y genero")
    if limite is not None:
        filas = filas[:limite]
    rutas = [Path(raiz_audios) / fila["audio"] for fila in filas]
    faltantes = [ruta for ruta in rutas if not ruta.is_file()]
    if faltantes:
        raise FileNotFoundError(f"faltan {len(faltantes)} audios; ejemplos: {faltantes[:5]}")

    # 1. mfcc normalizados de cada tramo, repartidos entre procesos
    if procesos is not None and procesos <= 0:
        raise ValueError("procesos debe ser mayor que cero")
    procesos = min(len(rutas), procesos or max(1, min(8, multiprocessing.cpu_count() - 1)))
    tareas = [(ruta, duracion, hop, n_mfcc) for ruta in rutas]
    if procesos <= 1:
        por_cancion = [_descriptores_worker(tarea) for tarea in tareas]
    else:
        # un hilo por proceso: si cada uno usa todos los nucleos se estorban entre ellos
        for variable in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
            os.environ.setdefault(variable, "1")
        with multiprocessing.Pool(procesos) as pool:
            por_cancion = list(pool.imap(_descriptores_worker, tareas, chunksize=4))
    tramos = [(i, inicio, fin, d) for i, lista in enumerate(por_cancion) for inicio, fin, d in lista]

    # 2. vocabulario propio de los tramos, idf de los tramos y vectores
    salida = Path(salida)
    salida.mkdir(parents=True, exist_ok=True)
    ruta_vocabulario, ruta_idf = rutas_vocabulario(salida, k)
    if ruta_vocabulario.is_file():
        vocabulario = np.load(ruta_vocabulario, allow_pickle=False)
    else:
        vocabulario = entrenar_vocabulario([d for *_, d in tramos], k, muestra)
        np.save(ruta_vocabulario, vocabulario, allow_pickle=False)
    vocabulario, indice = _indice_vocabulario(vocabulario, n_mfcc)
    histogramas = np.vstack([_histograma(d, indice, len(vocabulario)) for *_, d in tramos])
    df = np.count_nonzero(histogramas, axis=0)
    idf = (np.log((len(histogramas) + 1.0) / (df + 1.0)) + 1.0).astype(np.float32)
    np.save(ruta_idf, idf, allow_pickle=False)
    matriz = _aplicar_tfidf(histogramas, idf)

    # 3. los dos csv que carga docs/copyright.sql
    with (salida / "canciones.csv").open("w", encoding="utf-8", newline="") as archivo:
        escritor = csv.writer(archivo, lineterminator="\n")
        escritor.writerow(["id", "archivo", "genero"])
        for fila in filas:
            escritor.writerow([int(fila["id"]), fila["audio"], fila["genero"]])
    with (salida / "segmentos.csv").open("w", encoding="utf-8", newline="") as archivo:
        escritor = csv.writer(archivo, lineterminator="\n")
        escritor.writerow(["id", "cancion_id", "inicio", "fin", "v"])
        for n, ((i, inicio, fin, _), vector) in enumerate(zip(tramos, matriz), 1):
            v = "VECTOR(" + ",".join(f"{float(x):.8g}" for x in vector) + ")"
            escritor.writerow([n, int(filas[i]["id"]), f"{inicio:.2f}", f"{fin:.2f}", v])
    tiempo = time.perf_counter() - inicio_total
    print(f"{len(filas):,} canciones, {len(tramos):,} tramos en {tiempo:.2f} s con {procesos} procesos")
    return len(tramos), tiempo


def main():
    parser = argparse.ArgumentParser(description="Parte los audios en tramos y guarda el vector de cada uno")
    parser.add_argument("--hop", type=float, default=HOP_DEFAULT, help="segundos entre el inicio de dos tramos")
    parser.add_argument("--duracion", type=float, default=DURACION_TRAMO_DEFAULT, help="segundos de cada tramo")
    parser.add_argument("--csv", default=os.fspath(CSV_CANCIONES), help="CSV con columnas id, audio y genero")
    parser.add_argument("--audios", default=os.fspath(AUDIOS_DEFAULT), help="raiz de los audios del CSV")
    parser.add_argument("--salida", default=os.fspath(CARPETA_AUDIO))
    parser.add_argument("--procesos", type=int)
    parser.add_argument("--limite", type=int)
    parser.add_argument("--k", type=int, default=K_DEFAULT)
    args = parser.parse_args()
    procesar_tramos(args.csv, args.audios, args.hop, args.duracion, args.salida, args.procesos,
                    limite=args.limite, k=args.k)


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
