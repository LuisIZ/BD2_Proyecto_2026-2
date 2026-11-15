import argparse
import csv
import os
import shutil
import sys
import tempfile
import time
from pathlib import Path

import librosa
import numpy as np
import soundfile as sf

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ))
sys.path.insert(0, str(RAIZ / "api"))

from api import copyright, web_api  # noqa: E402
from api.motor_cli import MotorSQL  # noqa: E402
from multimedia.segmentos_audio import AUDIOS_DEFAULT, CSV_CANCIONES  # noqa: E402

SR = 22050
SALIDA = RAIZ / "docs" / "copyright" / "evaluacion.csv"
UMBRALES = range(50, 100)
POSITIVOS = {"recorte": 30, "volumen -6 dB": 10, "volumen +6 dB": 10, "ruido 20 dB": 15,
             "velocidad +5 %": 10, "velocidad -5 %": 10, "mezcla": 15}
NEGATIVOS = {"sintetico": 20, "clip sin sus tramos": 30}


def recorte(senal, segundos, rng):
    inicio = int(rng.uniform(0, len(senal) / SR - segundos) * SR)
    return senal[inicio:inicio + int(segundos * SR)]


def alterar(tipo, senal, otra, rng):
    if tipo == "recorte":
        return recorte(senal, rng.uniform(10, 20), rng)
    parte = recorte(senal, 15, rng)
    if tipo == "volumen -6 dB":
        return parte * 0.5
    if tipo == "volumen +6 dB":
        return np.clip(parte * 2.0, -1, 1)
    if tipo == "ruido 20 dB":
        rms = np.sqrt(np.mean(parte ** 2))
        return parte + rng.standard_normal(len(parte)) * rms / 10
    if tipo in ("velocidad +5 %", "velocidad -5 %"):
        # cambiar la frecuencia de muestreo y reproducir a 22 050 Hz acelera o frena (y cambia el tono)
        factor = 1.05 if "+" in tipo else 0.95
        return librosa.resample(parte, orig_sr=SR, target_sr=int(SR / factor))
    if tipo == "mezcla":
        return 0.5 * parte + 0.5 * recorte(otra, 15, rng)
    raise ValueError(tipo)


def sintetico(rng):
    t = np.arange(15 * SR) / SR
    forma = rng.integers(4)
    if forma == 0:
        # una nota al azar cada medio segundo
        notas = rng.uniform(100, 2000, 5)
        paso = SR // 2
        return np.concatenate([0.4 * np.sin(2 * np.pi * rng.choice(notas) * t[:paso]) for _ in range(30)])
    if forma == 1:
        return 0.3 * rng.standard_normal(len(t))
    if forma == 2:
        f0 = rng.uniform(100, 500)
        return 0.4 * np.sin(2 * np.pi * (f0 * t + rng.uniform(20, 80) * t ** 2))
    acorde = sum(np.sin(2 * np.pi * f * t) for f in rng.uniform(150, 800, 3)) / 3
    return 0.5 * acorde * (0.6 + 0.4 * np.sin(2 * np.pi * rng.uniform(1, 4) * t))


def casos(filas, rng):
    # (tipo, senal, canciones esperadas, cancion excluida)
    elegidas = rng.permutation(len(filas))
    usadas = 0

    def clip():
        nonlocal usadas
        fila = filas[elegidas[usadas]]
        usadas += 1
        senal, _ = librosa.load(AUDIOS_DEFAULT / fila["audio"], sr=SR, duration=30.0)
        return int(fila["id"]), senal

    for tipo, n in POSITIVOS.items():
        for _ in range(n):
            cid, senal = clip()
            esperadas = {cid}
            otra = None
            if tipo == "mezcla":
                otro_id, otra = clip()
                esperadas.add(otro_id)
            yield tipo, alterar(tipo, senal, otra, rng), esperadas, None
    for _ in range(NEGATIVOS["sintetico"]):
        yield "sintetico", sintetico(rng), set(), None
    for _ in range(NEGATIVOS["clip sin sus tramos"]):
        cid, senal = clip()
        yield "clip sin sus tramos", recorte(senal, 15, rng), set(), cid


def similitudes(ruta, excluida):
    # por tramo, la lista (cancion, similitud) que devuelve el motor, sin la cancion excluida
    r = copyright.analizar_audio(ruta, umbral=0.0)
    return [[(c["cancion_id"], c["similitud"]) for c in t["coincidencias"] if c["cancion_id"] != excluida]
            for t in r["tramos"]]


def detectadas(tramos, umbral):
    marcados = [{"coincidencias": [{"cancion_id": cid, "archivo": "", "genero": "", "similitud": s,
                                     "supera_umbral": s >= umbral} for cid, s in t]} for t in tramos]
    canciones, _ = copyright.agrupar(marcados)
    return {c["cancion_id"] for c in canciones}


def medir(resultados, umbral):
    reportadas = correctas = encontradas = alarmas = 0
    positivos = sum(1 for _, esperadas, _ in resultados if esperadas)
    for _, esperadas, tramos in resultados:
        halladas = detectadas(tramos, umbral)
        reportadas += len(halladas)
        correctas += len(halladas & esperadas)
        if esperadas and halladas & esperadas:
            encontradas += 1
        if not esperadas and halladas:
            alarmas += 1
    precision = correctas / reportadas if reportadas else 1.0
    recall = encontradas / positivos
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {"umbral": umbral, "precision": round(precision, 3), "recall": round(recall, 3), "f1": round(f1, 3),
            "reportadas": reportadas, "correctas": correctas, "positivos_hallados": encontradas,
            "negativos_con_alarma": alarmas}


def main():
    parser = argparse.ArgumentParser(description="Evalua la deteccion de copyright con remixes de GTZAN")
    parser.add_argument("--semilla", type=int, default=2026)
    parser.add_argument("--salida", default=os.fspath(SALIDA))
    args = parser.parse_args()
    inicio = time.perf_counter()
    with open(CSV_CANCIONES, encoding="utf-8-sig", newline="") as archivo:
        filas = list(csv.DictReader(archivo))

    # base temporal con docs/copyright.sql (bloques 1 a 3)
    db = Path(tempfile.mkdtemp(prefix="copyright_", dir=RAIZ / ".build"))
    web_api.motor = MotorSQL(db=db.relative_to(RAIZ).as_posix())
    web_api.indices_faiss = web_api.IndicesFaiss()
    resultados = []
    try:
        carga = (RAIZ / "docs" / "copyright.sql").read_text(encoding="utf-8").split("-- [4]")[0]
        for r in web_api.consultas(web_api.Consulta(sql=carga))["resultados"]:
            if not r.get("ok"):
                raise RuntimeError(r.get("error"))
        ruta = db / "caso.wav"
        for n, (tipo, senal, esperadas, excluida) in enumerate(casos(filas, np.random.default_rng(args.semilla)), 1):
            sf.write(ruta, np.asarray(senal, dtype=np.float32), SR)
            resultados.append((tipo, esperadas, similitudes(ruta, excluida)))
            if n % 25 == 0:
                print(f"{n} casos en {time.perf_counter() - inicio:.0f} s")
    finally:
        shutil.rmtree(db, ignore_errors=True)

    filas_csv = [medir(resultados, u) for u in UMBRALES]
    salida = Path(args.salida)
    salida.parent.mkdir(parents=True, exist_ok=True)
    with salida.open("w", encoding="utf-8", newline="") as archivo:
        escritor = csv.DictWriter(archivo, fieldnames=list(filas_csv[0]), lineterminator="\n")
        escritor.writeheader()
        escritor.writerows(filas_csv)
    mejor = max(filas_csv, key=lambda f: (f["f1"], f["umbral"]))
    print(f"{len(resultados)} casos en {time.perf_counter() - inicio:.0f} s; escrito {salida}")
    print(f"mejor umbral {mejor['umbral']}: precision {mejor['precision']}, recall {mejor['recall']}, f1 {mejor['f1']}")

    # recall o alarmas por tipo de caso con el mejor umbral
    for tipo in list(POSITIVOS) + list(NEGATIVOS):
        del_tipo = [(e, t) for nombre, e, t in resultados if nombre == tipo]
        bien = sum(1 for e, t in del_tipo if (detectadas(t, mejor["umbral"]) & e) if e)
        alarmas = sum(1 for e, t in del_tipo if not e and detectadas(t, mejor["umbral"]))
        print(f"  {tipo:20s} {len(del_tipo):3d} casos: " +
              (f"hallados {bien}" if POSITIVOS.get(tipo) else f"con alarma {alarmas}"))


if __name__ == "__main__":
    main()
