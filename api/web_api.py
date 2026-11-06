"""API local para React. Arranque: python -m uvicorn api.web_api:app."""

import csv
import hashlib
import io
import os
import re
import sys
import threading
from contextlib import asynccontextmanager
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Literal

import cv2
import numpy as np
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

from api.motor_cli import ErrorMotor, MotorSQL, RAIZ

# los modulos de api/multimedia se importan como multimedia.*, igual que en sus pruebas
sys.path.insert(0, str(RAIZ / "api"))
from multimedia.similitud import IndicesFaiss, partir_lote, reescribir  # noqa: E402

# Serializa las llamadas de este cliente; no implementa transacciones en el motor.
lock = threading.Lock()
motor: MotorSQL | None = None
DB = os.environ.get("BD2_DB", "datos/db")
# IVF y HNSW de FAISS: el motor solo los registra y anota los cambios
indices_faiss = IndicesFaiss()
# respuestas del motor despues de las cuales puede haber cambios para FAISS
ESCRIBEN = {"insert", "delete", "copy", "create_index", "commit"}


@asynccontextmanager
async def lifespan(app):
    global motor
    motor = MotorSQL(db=DB)
    yield


app = FastAPI(title="Minigestor BD2", lifespan=lifespan)


class Consulta(BaseModel):
    sql: str = Field(min_length=1, max_length=64000)


class Importacion(BaseModel):
    nombre: str = Field(pattern=r"^[A-Za-z_][A-Za-z0-9_]{0,49}$")
    organizacion: Literal["HEAP", "SEQUENTIAL", "BPLUS"] = "HEAP"
    dataset: str | None = None
    contenido: str | None = Field(default=None, max_length=32 * 1024 * 1024)
    # columna que será clave primaria; vacío deja que el motor tome la primera INT
    clave: str | None = Field(default=None, pattern=r"^[A-Za-z_][A-Za-z0-9_]{0,63}$")
    # columnas con índice B+ no agrupado
    indices: list[str] = Field(default_factory=list, max_length=8)


class Vistazo(BaseModel):
    dataset: str | None = None
    contenido: str | None = Field(default=None, max_length=32 * 1024 * 1024)


def sin_comentarios(sql):
    """Quita comentarios `-- ...` hasta fin de línea; el parser del motor no los acepta.
    Respeta las comillas simples para no tocar literales como 'a--b'."""
    salida = []
    en_cadena = False
    i = 0
    while i < len(sql):
        c = sql[i]
        if en_cadena:
            salida.append(c)
            if c == "'":
                en_cadena = False
        elif c == "'":
            en_cadena = True
            salida.append(c)
        elif c == "-" and sql.startswith("--", i):
            fin = sql.find("\n", i)
            i = len(sql) if fin == -1 else fin
            continue
        else:
            salida.append(c)
        i += 1
    return "".join(salida)


def ejecutar(sql):
    try:
        respuestas = motor.ejecutar(sin_comentarios(sql))
        if not isinstance(respuestas, list):
            raise ErrorMotor(respuestas.get("error", "Respuesta inesperada del motor"))
        return respuestas
    except (ErrorMotor, OSError) as error:
        raise HTTPException(500, str(error)) from error


def sincronizar_faiss(respuestas):
    """Aplica a FAISS lo que el lote cambio en tablas con IVF o HNSW; una escritura por indice."""
    if not any(r.get("ok") and r.get("tipo") in ESCRIBEN for r in respuestas):
        return []
    try:
        return indices_faiss.sincronizar(motor.catalogo())
    except (ErrorMotor, OSError) as error:
        return [{"error": str(error)}]


def preparar_similitud(sql):
    """SIMILAR_TO ... USING IVF|HNSW: busca en FAISS y agrega CANDIDATES (pk, ...) al SQL."""
    if "similar_to" not in sql.lower():
        return sql, []
    try:
        return reescribir(sql, motor.catalogo(), indices_faiss, extraer=vector_de_archivo)
    except (ErrorMotor, OSError, ValueError, RuntimeError) as error:
        raise HTTPException(400, f"SIMILAR_TO: {error}") from error


@app.get("/api/health")
def health():
    return {"ok": motor is not None, "db": DB}


@app.get("/api/catalogo")
def catalogo():
    with lock:
        try:
            resultado = motor.catalogo()
            if not resultado.get("ok"):
                raise ErrorMotor(resultado.get("error", "No se pudo leer el catálogo"))
            estadisticas = {t["tabla"].lower(): t for t in motor.tablas()}
            for tabla in resultado["tablas"]:
                tabla["estadisticas"] = estadisticas.get(tabla["nombre"].lower(), {})
                for indice in tabla["indices"]:
                    ruta = RAIZ / indice.get("archivo", "")
                    indice["bytes"] = ruta.stat().st_size if ruta.is_file() else None
            return resultado
        except (ErrorMotor, OSError) as error:
            raise HTTPException(500, str(error)) from error


@app.post("/api/consultas")
def consultas(consulta: Consulta):
    if not sin_comentarios(consulta.sql).strip():
        raise HTTPException(400, "Escribe una consulta SQL.")
    resultados, faiss = [], []
    with lock:
        # cada SIMILAR_TO ... USING busca en FAISS despues de que el motor corrio lo anterior
        for parte in partir_lote(sin_comentarios(consulta.sql)):
            sql, busquedas = preparar_similitud(parte)
            respuestas = ejecutar(sql)
            resultados += respuestas
            faiss += busquedas + sincronizar_faiss(respuestas)
    return {"resultados": resultados, "faiss": faiss}


@app.get("/api/datasets")
def datasets():
    return {"archivos": sorted(p.name for p in (RAIZ / "datos").glob("*.csv"))}


def identificador_valido(original):
    """Mismo saneo que hace el motor al leer la cabecera del CSV."""
    s = "".join(c if c.isalnum() and c.isascii() else "_" for c in original)
    return s if s and not s[0].isdigit() else "c_" + s


def es_entero(texto):
    if not texto or len(texto) > 11:
        return False
    cuerpo = texto[1:] if texto[0] == "-" else texto
    if not cuerpo.isdigit():
        return False
    return -(2**31) <= int(texto) <= 2**31 - 1


@app.post("/api/columnas")
def columnas(datos: Vistazo):
    """Cabecera y tipos inferidos de un CSV, para poder elegir clave e índices
    antes de cargarlo. Mira hasta 2000 filas: alcanza para distinguir INT de
    texto sin leer un archivo de 100 MB."""
    if (datos.dataset is None) == (datos.contenido is None):
        raise HTTPException(400, "Indica un archivo del proyecto o un CSV.")
    if datos.dataset is not None:
        if datos.dataset not in datasets()["archivos"]:
            raise HTTPException(400, "El CSV no pertenece a datos/.")
        with (RAIZ / "datos" / datos.dataset).open(encoding="utf-8-sig", newline="") as archivo:
            filas = [f for _, f in zip(range(2001), csv.reader(archivo))]
    else:
        filas = [f for _, f in zip(range(2001), csv.reader(datos.contenido.lstrip("﻿").splitlines()))]
    filas = [f for f in filas if f]
    if len(filas) < 2:
        raise HTTPException(400, "El CSV no tiene filas de datos.")

    cabecera, cuerpo = filas[0], filas[1:]
    salida, usados = [], set()
    for i, titulo in enumerate(cabecera):
        nombre = identificador_valido(titulo)
        while nombre.lower() in usados:
            nombre += "_"
        usados.add(nombre.lower())
        valores = [f[i] for f in cuerpo if i < len(f)]
        entera = bool(valores) and all(es_entero(v) for v in valores)
        unica = entera and len(set(valores)) == len(valores)
        salida.append({
            "nombre": nombre,
            "original": titulo,
            "tipo": "INT" if entera else "VARCHAR",
            "ancho": 0 if entera else max((len(v) for v in valores), default=1),
            # candidata a clave primaria: entera y sin repetir en la muestra
            "unica": unica,
        })
    return {"columnas": salida, "muestra": len(cuerpo)}


@app.post("/api/importar")
def importar(datos: Importacion):
    if (datos.dataset is None) == (datos.contenido is None):
        raise HTTPException(400, "Selecciona un archivo del proyecto o un CSV local.")
    temporal = None
    try:
        if datos.dataset is not None:
            disponibles = datasets()["archivos"]
            if datos.dataset not in disponibles:
                raise HTTPException(400, "El CSV no pertenece a datos/.")
            ruta = RAIZ / "datos" / datos.dataset
        else:
            if not datos.contenido.strip():
                raise HTTPException(400, "El CSV está vacío.")
            carpeta = RAIZ / ".build" / "cargas_web"
            carpeta.mkdir(parents=True, exist_ok=True)
            with NamedTemporaryFile(mode="w", encoding="utf-8", newline="", suffix=".csv", dir=carpeta, delete=False) as archivo:
                archivo.write(datos.contenido.lstrip('\ufeff'))
                temporal = Path(archivo.name)
            ruta = temporal
        # Identificadores validados por Pydantic; ruta generada localmente, nunca SQL del navegador.
        for columna in datos.indices:
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,63}", columna):
                raise HTTPException(400, f"Nombre de columna inválido: {columna}")
        literal = ruta.as_posix().replace("'", "''")
        sql = f"CREATE TABLE {datos.nombre} FROM FILE '{literal}' USING {datos.organizacion}"
        if datos.clave:
            sql += f" PRIMARY KEY {datos.clave}"
        if datos.indices:
            sql += f" INDEX ({', '.join(datos.indices)})"
        sql += ";"
        with lock:
            return {"resultados": ejecutar(sql)}
    finally:
        if temporal is not None:
            temporal.unlink(missing_ok=True)


@app.get("/api/experimentos")
def experimentos():
    archivos = []
    for ruta in sorted((RAIZ / "datos" / "resultados").glob("*.csv")):
        with ruta.open(encoding="utf-8-sig", newline="") as archivo:
            lector = csv.DictReader(archivo)
            archivos.append({"nombre": ruta.name, "columnas": lector.fieldnames or [], "filas": list(lector)})
    return {"archivos": archivos}


# --- miniaturas de productos (Partes 3 y 4) ---

# imagenes que copia tools/dataset/extraer_imagenes.py, de la carpeta mas completa a la menor
DATOS = Path(os.environ.get("BD2_DATOS", r"C:\datos_bd2"))
CARPETAS_IMAGENES = [DATOS / "imagenes", DATOS / "imagenes_10k", DATOS / "imagenes_1k"]
_rutas_imagenes = None
_lock_imagenes = threading.Lock()


def rutas_imagenes():
    """id del producto -> ruta de su imagen, leida del CSV de productos mas grande que haya.
    Los CSV son anidados (1k dentro de 10k dentro de 100k): un id es el mismo producto en los tres."""
    global _rutas_imagenes
    # la galeria pide varias miniaturas a la vez: el CSV se lee una sola vez y el
    # diccionario se publica ya completo, para que ninguna peticion lo vea a medias
    with _lock_imagenes:
        if _rutas_imagenes is None:
            candidatos = [RAIZ / "datos" / f"productos_{n}.csv" for n in ("100k", "10k", "1k")]
            ruta = next((c for c in candidatos if c.is_file()), None)
            rutas = {}
            if ruta:
                with ruta.open(encoding="utf-8", newline="") as archivo:
                    rutas = {int(fila["id"]): fila["imagen"] for fila in csv.DictReader(archivo)}
            _rutas_imagenes = rutas
    return _rutas_imagenes


@app.get("/api/imagenes/{id}")
def imagen(id: int):
    relativa = rutas_imagenes().get(id)
    if relativa is None:
        raise HTTPException(404, f"No hay un producto con id {id}.")
    for carpeta in CARPETAS_IMAGENES:
        ruta = (carpeta / relativa).resolve()
        # la ruta sale del CSV, pero igual no se sirve nada fuera de la carpeta de imagenes
        if ruta.is_relative_to(carpeta.resolve()) and ruta.is_file():
            return FileResponse(ruta, headers={"Cache-Control": "public, max-age=86400"})
    raise HTTPException(404, f"Falta la imagen {relativa} en {DATOS}. Ver docs/dataset.md.")


# audios de canciones (Parte 4)

# audios de GTZAN que se copian a mano a esta carpeta
AUDIOS = DATOS / "audio" / "genres_original"
_rutas_audios = None


def rutas_audios():
    #id de la cancion -> ruta del audio dentro de AUDIOS, leida de datos/audios_1k.csv
    global _rutas_audios
    if _rutas_audios is None:
        ruta = RAIZ / "datos" / "audios_1k.csv"
        rutas = {}
        if ruta.is_file():
            with ruta.open(encoding="utf-8", newline="") as archivo:
                rutas = {int(fila["id"]): fila["audio"] for fila in csv.DictReader(archivo)}
        _rutas_audios = rutas
    return _rutas_audios


@app.get("/api/audios/{id}")
def audio(id: int):
    relativa = rutas_audios().get(id)
    if relativa is None:
        raise HTTPException(404, f"No hay una cancion con id {id}.")
    ruta = (AUDIOS / relativa).resolve()
    if ruta.is_relative_to(AUDIOS.resolve()) and ruta.is_file():
        return FileResponse(ruta, media_type="audio/wav", headers={"Cache-Control": "public, max-age=86400"})
    raise HTTPException(404, f"Falta el audio {relativa} en {AUDIOS}. Ver docs/extraccion_audio.md.")


# --- imagenes y audios de consulta para SIMILAR_TO('archivo') (Parte 4) ---

SUBIDAS = RAIZ / ".build" / "subidas"
EXTENSIONES_IMAGEN = {".jpg", ".jpeg", ".png", ".webp", ".bmp"}
EXTENSIONES_AUDIO = {".wav", ".mp3", ".flac", ".ogg"}
MAX_SUBIDA = 10 * 1024 * 1024
MAX_SUBIDA_AUDIO = 50 * 1024 * 1024


def ruta_en(nombre, carpetas):
    """La ruta de 'nombre' en la primera carpeta que lo tenga, sin salir de ella; None si no esta."""
    for carpeta in carpetas:
        ruta = (carpeta / nombre).resolve()
        if ruta.is_relative_to(carpeta.resolve()) and ruta.is_file():
            return ruta
    return None


def vector_de_imagen(nombre):
    """El mismo vector que extraer.py guardo para cada producto: SIFT, palabras visuales y TF-IDF.
    Se busca en las subidas y en las imagenes del dataset (ruta relativa del CSV de productos)."""
    from multimedia.extraer import extract_features  # carga OpenCV y el vocabulario solo si hace falta

    ruta = ruta_en(nombre, [SUBIDAS, *CARPETAS_IMAGENES])
    if ruta is None:
        raise FileNotFoundError(f"no se encontro la imagen {nombre}: subela con POST /api/multimedia/subir")
    return extract_features(ruta)


def vector_de_audio(nombre):
    """El mismo vector que extraer_audio.py guardo para cada cancion: MFCC, palabras acusticas y TF-IDF.
    Se busca en las subidas y en los audios del dataset (ruta relativa de datos/audios_1k.csv)."""
    from multimedia.extraer_audio import extract_features_audio  # carga librosa solo si hace falta

    ruta = ruta_en(nombre, [SUBIDAS, AUDIOS])
    if ruta is None:
        raise FileNotFoundError(f"no se encontro el audio {nombre}: subelo con POST /api/multimedia/subir")
    return extract_features_audio(ruta)


def vector_de_archivo(nombre):
    """SIMILAR_TO('nombre'): la extension dice si es un audio o una imagen."""
    es_audio = Path(nombre).suffix.lower() in EXTENSIONES_AUDIO
    vector = vector_de_audio(nombre) if es_audio else vector_de_imagen(nombre)
    # un vector cero no se parece a nada: las k filas saldrian al azar
    if not np.any(vector):
        raise ValueError(f"no se pudieron extraer caracteristicas de {nombre}: " +
                         ("el audio esta vacio" if es_audio else "la imagen no tiene puntos SIFT"))
    return vector


def validar_audio(contenido):
    import soundfile  # viene con librosa

    try:
        soundfile.info(io.BytesIO(contenido))
    except RuntimeError as error:
        raise ValueError("el archivo no es un audio que se pueda leer") from error


def guardar_subida(contenido, nombre):
    """Guarda el archivo con el hash de su contenido como nombre: subir dos veces el mismo no duplica."""
    extension = Path(nombre).suffix.lower()
    es_audio = extension in EXTENSIONES_AUDIO
    if not es_audio and extension not in EXTENSIONES_IMAGEN:
        admitidas = ", ".join(sorted(EXTENSIONES_IMAGEN | EXTENSIONES_AUDIO))
        raise ValueError(f"extension no admitida: {extension or '(ninguna)'} ({admitidas})")
    limite = MAX_SUBIDA_AUDIO if es_audio else MAX_SUBIDA
    if not contenido or len(contenido) > limite:
        raise ValueError(f"el archivo debe pesar entre 1 byte y {limite // (1024 * 1024)} MB")
    if es_audio:
        validar_audio(contenido)
    elif cv2.imdecode(np.frombuffer(contenido, dtype=np.uint8), cv2.IMREAD_GRAYSCALE) is None:
        raise ValueError("el archivo no es una imagen que OpenCV pueda leer")
    SUBIDAS.mkdir(parents=True, exist_ok=True)
    archivo = hashlib.sha1(contenido).hexdigest()[:16] + extension
    (SUBIDAS / archivo).write_bytes(contenido)
    return archivo


@app.post("/api/multimedia/subir")
async def subir(request: Request, nombre: str):
    """El cuerpo es la imagen o el audio tal cual (fetch con body: archivo); devuelve el nombre para SIMILAR_TO."""
    try:
        archivo = guardar_subida(await request.body(), nombre)
    except ValueError as error:
        raise HTTPException(400, str(error)) from error
    return {"archivo": archivo, "consulta": f"SIMILAR_TO('{archivo}', k=10)"}


@app.get("/api/multimedia/subidas/{archivo}")
def subida(archivo: str):
    ruta = (SUBIDAS / archivo).resolve()
    if not ruta.is_relative_to(SUBIDAS.resolve()) or not ruta.is_file():
        raise HTTPException(404, f"No hay un archivo subido con el nombre {archivo}.")
    return FileResponse(ruta, headers={"Cache-Control": "public, max-age=86400"})
