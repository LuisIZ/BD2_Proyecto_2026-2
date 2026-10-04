"""API local para React. Arranque: python -m uvicorn api.web_api:app."""

import csv
import os
import re
import threading
from contextlib import asynccontextmanager
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Literal

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

from api.motor_cli import ErrorMotor, MotorSQL, RAIZ

# Serializa las llamadas de este cliente; no implementa transacciones en el motor.
lock = threading.Lock()
motor: MotorSQL | None = None
DB = os.environ.get("BD2_DB", "datos/db")


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
    # columnas con índice B+ no agrupado; solo se admiten sobre tablas HEAP
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
    with lock:
        return {"resultados": ejecutar(consulta.sql)}


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
    if datos.indices and datos.organizacion != "HEAP":
        raise HTTPException(400, "Los índices secundarios solo se admiten sobre tablas HEAP.")
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
