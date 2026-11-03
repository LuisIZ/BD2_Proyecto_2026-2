import argparse
import csv
import multiprocessing
import os
import time
from functools import partial
from pathlib import Path

import librosa
import numpy as np

from multimedia.extraer import (
    K_DEFAULT,
    MUESTRA_DESCRIPTORES_DEFAULT,
    RAIZ,
    _aplicar_tfidf,
    _histograma,
    _indice_vocabulario,
    calcular_vectores,
    cargar_o_entrenar_vocabulario,
    guardar_vectores,
)

N_MFCC_DEFAULT = 13
FRECUENCIA_MUESTREO = 22050
DURACION_MAXIMA = 30.0
VENTANA = 2048
SALTO = 512
MAX_VENTANAS_POR_AUDIO = 1500
CARPETA_AUDIO = RAIZ / "datos" / "audio"


def extraer_descriptores_audio(ruta, max_descriptores=MAX_VENTANAS_POR_AUDIO, n_mfcc=N_MFCC_DEFAULT):
    if max_descriptores <= 0:
        raise ValueError("max_descriptores debe ser mayor que cero")
    senal, _ = librosa.load(os.fspath(ruta), sr=FRECUENCIA_MUESTREO, mono=True, duration=DURACION_MAXIMA)
    if len(senal) == 0:
        return np.empty((0, n_mfcc), dtype=np.float32)
    # un vector de n_mfcc coeficientes por cada ventana de tiempo
    mfcc = librosa.feature.mfcc(y=senal, sr=FRECUENCIA_MUESTREO, n_mfcc=n_mfcc, n_fft=VENTANA, hop_length=SALTO)
    return np.ascontiguousarray(mfcc.T[:max_descriptores], dtype=np.float32)


def _ruta_vocabulario(k):
    return CARPETA_AUDIO / f"vocabulario_audio_{k}.npy"


def _ruta_idf(k):
    return CARPETA_AUDIO / f"idf_audio_{k}.npy"


def _cargar_artifacts(n_mfcc):
    k = int(os.environ.get("BD2_K_AUDIO", K_DEFAULT))
    ruta_vocabulario = Path(os.environ.get("BD2_VOCABULARIO_AUDIO", _ruta_vocabulario(k)))
    if not ruta_vocabulario.is_file():
        raise FileNotFoundError(f"no existe el vocabulario de audio: {ruta_vocabulario}")
    vocabulario, indice = _indice_vocabulario(np.load(ruta_vocabulario, allow_pickle=False), n_mfcc)
    ruta_idf = Path(os.environ.get("BD2_IDF_AUDIO", _ruta_idf(len(vocabulario))))
    if not ruta_idf.is_file():
        raise FileNotFoundError(f"no existe el IDF de audio de la coleccion: {ruta_idf}")
    idf = np.asarray(np.load(ruta_idf, allow_pickle=False), dtype=np.float32)
    if idf.shape != (len(vocabulario),):
        raise ValueError("el vector IDF debe tener exactamente K componentes")
    return vocabulario, indice, idf


def extract_features_audio(ruta, n_mfcc=N_MFCC_DEFAULT) -> np.ndarray:
    vocabulario, indice, idf = _cargar_artifacts(n_mfcc)
    descriptores = extraer_descriptores_audio(ruta, n_mfcc=n_mfcc)
    histograma = _histograma(descriptores, indice, len(vocabulario))[None, :]
    return _aplicar_tfidf(histograma, idf)[0]


def procesar_audios(csv_audios, raiz_audios, k=K_DEFAULT, n_mfcc=N_MFCC_DEFAULT, limite=None, procesos=None,
                    vocabulario_path=None, idf_path=None, cache_dir=None, salida=None,
                    muestra_descriptores=MUESTRA_DESCRIPTORES_DEFAULT):
    inicio = time.perf_counter()
    if limite is not None and limite <= 0:
        raise ValueError("limite debe ser mayor que cero")
    with open(csv_audios, encoding="utf-8-sig", newline="") as archivo:
        filas = list(csv.DictReader(archivo))
    if not filas or not {"id", "audio"}.issubset(filas[0]):
        raise ValueError("el CSV debe incluir las columnas id y audio")
    if limite is not None:
        filas = filas[:limite]
    ids = [int(fila["id"]) for fila in filas]
    rutas = [Path(raiz_audios) / fila["audio"] for fila in filas]
    faltantes = [ruta for ruta in rutas if not ruta.is_file()]
    if faltantes:
        raise FileNotFoundError(f"faltan {len(faltantes)} audios; ejemplos: {faltantes[:5]}")

    #con mfcc
    extractor = partial(extraer_descriptores_audio, n_mfcc=n_mfcc)
    idf_path = Path(idf_path or _ruta_idf(k))
    vocabulario = cargar_o_entrenar_vocabulario(
        rutas, k, vocabulario_path or _ruta_vocabulario(k), muestra_descriptores, procesos,
        MAX_VENTANAS_POR_AUDIO, extractor, n_mfcc,
    )
    histogramas, idf = calcular_vectores(
        rutas, vocabulario, procesos=procesos, cache_dir=cache_dir,
        max_descriptores=MAX_VENTANAS_POR_AUDIO, extractor=extractor, dimension=n_mfcc,
    )
    idf_path.parent.mkdir(parents=True, exist_ok=True)
    np.save(idf_path, idf, allow_pickle=False)
    vectores = _aplicar_tfidf(histogramas, idf)
    prefijo = Path(salida) if salida else CARPETA_AUDIO / f"vectores_audio_{len(ids)}"
    guardar_vectores(prefijo, ids, vectores)
    tiempo = time.perf_counter() - inicio
    print(f"total {len(ids):,} audios en {tiempo:.2f} s; vectores: {prefijo.with_suffix('.npy')}")
    return np.asarray(ids, dtype=np.int64), vectores, tiempo


def main():
    parser = argparse.ArgumentParser(description="Vectores TF-IDF de audio a partir de MFCC")
    parser.add_argument("--csv", required=True, help="CSV con columnas id y audio")
    parser.add_argument("--audios", required=True, help="raiz de los audios del CSV")
    parser.add_argument("--k", type=int, default=K_DEFAULT)
    parser.add_argument("--mfcc", type=int, default=N_MFCC_DEFAULT, help="coeficientes MFCC por ventana")
    parser.add_argument("--limite", type=int)
    parser.add_argument("--procesos", type=int)
    parser.add_argument("--muestra-descriptores", type=int, default=MUESTRA_DESCRIPTORES_DEFAULT)
    parser.add_argument("--vocabulario")
    parser.add_argument("--idf", help="ruta del archivo idf_audio_K.npy")
    parser.add_argument("--cache", default=os.fspath(CARPETA_AUDIO / "cache"))
    parser.add_argument("--salida")
    args = parser.parse_args()
    procesar_audios(
        args.csv, args.audios, args.k, args.mfcc, args.limite, args.procesos,
        args.vocabulario, args.idf, args.cache, args.salida, args.muestra_descriptores,
    )


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
