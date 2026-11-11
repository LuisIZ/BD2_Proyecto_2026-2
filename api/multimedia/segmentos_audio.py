import argparse
import csv
import multiprocessing
import os
import time
from pathlib import Path

import librosa
import numpy as np

from multimedia.extraer import _aplicar_tfidf, _histograma
from multimedia.extraer_audio import (
    CARPETA_AUDIO,
    DURACION_MAXIMA,
    N_MFCC_DEFAULT,
    RAIZ,
    _cargar_artifacts,
    extraer_descriptores_audio,
)

HOP_DEFAULT = 5.0
DURACION_TRAMO_DEFAULT = 10.0
TOLERANCIA = 0.5
CSV_CANCIONES = RAIZ / "datos" / "audios_1k.csv"
AUDIOS_DEFAULT = Path(os.environ.get("BD2_DATOS", r"C:\datos_bd2")) / "audio" / "genres_original"

_WORKER = {}


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


def _iniciar_worker(duracion, hop, n_mfcc):
    vocabulario, indice, idf = _cargar_artifacts(n_mfcc)
    _WORKER.update(k=len(vocabulario), indice=indice, idf=idf, duracion=duracion, hop=hop, n_mfcc=n_mfcc)


def vectores_de_tramos(ruta):
    # parte el audio en tramos y devuelve (inicio, fin, vector) de cada uno
    total = min(librosa.get_duration(path=os.fspath(ruta)), DURACION_MAXIMA)
    tramos = []
    for inicio in inicios_de_tramos(total, _WORKER["duracion"], _WORKER["hop"]):
        descriptores = extraer_descriptores_audio(
            ruta, n_mfcc=_WORKER["n_mfcc"], offset=inicio, duracion=_WORKER["duracion"]
        )
        histograma = _histograma(descriptores, _WORKER["indice"], _WORKER["k"])[None, :]
        vector = _aplicar_tfidf(histograma, _WORKER["idf"])[0]
        tramos.append((inicio, min(inicio + _WORKER["duracion"], total), vector))
    return tramos


def procesar_tramos(csv_canciones=CSV_CANCIONES, raiz_audios=AUDIOS_DEFAULT, hop=HOP_DEFAULT,
                    duracion=DURACION_TRAMO_DEFAULT, salida=CARPETA_AUDIO, procesos=None,
                    n_mfcc=N_MFCC_DEFAULT, limite=None):
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

    if procesos is not None and procesos <= 0:
        raise ValueError("procesos debe ser mayor que cero")
    procesos = min(len(rutas), procesos or max(1, min(8, multiprocessing.cpu_count() - 1)))
    if procesos <= 1:
        _iniciar_worker(duracion, hop, n_mfcc)
        por_cancion = [vectores_de_tramos(ruta) for ruta in rutas]
    else:
        with multiprocessing.Pool(procesos, initializer=_iniciar_worker, initargs=(duracion, hop, n_mfcc)) as pool:
            por_cancion = list(pool.imap(vectores_de_tramos, rutas, chunksize=4))

    salida = Path(salida)
    salida.mkdir(parents=True, exist_ok=True)
    with (salida / "canciones.csv").open("w", encoding="utf-8", newline="") as archivo:
        escritor = csv.writer(archivo, lineterminator="\n")
        escritor.writerow(["id", "archivo", "genero"])
        for fila in filas:
            escritor.writerow([int(fila["id"]), fila["audio"], fila["genero"]])
    total_tramos = 0
    with (salida / "segmentos.csv").open("w", encoding="utf-8", newline="") as archivo:
        escritor = csv.writer(archivo, lineterminator="\n")
        escritor.writerow(["id", "cancion_id", "inicio", "fin", "v"])
        for fila, tramos in zip(filas, por_cancion):
            for inicio, fin, vector in tramos:
                total_tramos += 1
                v = "VECTOR(" + ",".join(f"{float(x):.8g}" for x in vector) + ")"
                escritor.writerow([total_tramos, int(fila["id"]), f"{inicio:.2f}", f"{fin:.2f}", v])
    tiempo = time.perf_counter() - inicio_total
    print(f"{len(filas):,} canciones, {total_tramos:,} tramos en {tiempo:.2f} s con {procesos} procesos")
    return total_tramos, tiempo


def main():
    parser = argparse.ArgumentParser(description="Parte los audios en tramos y guarda el vector de cada uno")
    parser.add_argument("--hop", type=float, default=HOP_DEFAULT, help="segundos entre el inicio de dos tramos")
    parser.add_argument("--duracion", type=float, default=DURACION_TRAMO_DEFAULT, help="segundos de cada tramo")
    parser.add_argument("--csv", default=os.fspath(CSV_CANCIONES), help="CSV con columnas id, audio y genero")
    parser.add_argument("--audios", default=os.fspath(AUDIOS_DEFAULT), help="raiz de los audios del CSV")
    parser.add_argument("--salida", default=os.fspath(CARPETA_AUDIO))
    parser.add_argument("--procesos", type=int)
    parser.add_argument("--limite", type=int)
    args = parser.parse_args()
    procesar_tramos(args.csv, args.audios, args.hop, args.duracion, args.salida, args.procesos, limite=args.limite)


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
