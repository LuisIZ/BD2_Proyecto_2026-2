"""Corre un archivo .sql por bloques a traves de la API, como la web, y revisa sus "-- espera:".

    python tools/probar_sql.py docs/pruebas_partes_3_4.sql

Cada bloque empieza con una linea "-- [n] titulo" y se manda como un lote a /api/consultas
(con la reescritura de SIMILAR_TO y la sincronizacion de FAISS). La base se crea de cero en
.build/ y se borra al terminar. Expectativas, sobre la ultima sentencia del bloque:

    ok                  todas las sentencias del bloque terminan bien
    error "texto"       la ultima falla y su mensaje contiene el texto
    filas N             la ultima devuelve N filas
    ids a, b, c         la primera columna de la ultima, en ese orden
    contiene "texto"    el texto aparece en alguna celda (EXPLAIN, DESCRIBE...)
    igual N             la misma primera columna que el bloque N
    faiss "indice"      la API busco en ese indice o lo sincronizo
"""

import argparse
import re
import shutil
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RAIZ))

from fastapi import HTTPException  # noqa: E402

from api import web_api  # noqa: E402
from api.motor_cli import MotorSQL  # noqa: E402


def leer_bloques(texto):
    bloques = []
    for linea in texto.splitlines():
        cabecera = re.match(r"--\s*\[(\d+)\]\s*(.*)", linea)
        if cabecera:
            bloques.append({"n": int(cabecera[1]), "titulo": cabecera[2].strip(), "sql": [], "espera": []})
        elif bloques and linea.startswith("-- espera:"):
            bloques[-1]["espera"].append(linea.split(":", 1)[1].strip())
        elif bloques:
            bloques[-1]["sql"].append(linea)
    return bloques


def primera_columna(respuesta):
    return [str(fila[0]) for fila in respuesta.get("filas", [])]


def revisar(espera, respuesta, error, faiss, anteriores):
    """Devuelve el problema, o None si se cumple."""
    ultima = respuesta[-1] if respuesta else {}
    fallidas = [r["error"] for r in respuesta if not r.get("ok")]
    texto = re.match(r'(\w+)\s*"(.*)"$', espera)
    if espera == "ok":
        return error or (fallidas[0] if fallidas else None)
    if texto and texto[1] == "error":
        mensaje = error or (ultima.get("error") if not ultima.get("ok") else None)
        if not mensaje:
            return "se esperaba un error y no hubo"
        return None if texto[2] in mensaje else f"el error fue: {mensaje}"
    if error or fallidas:
        return f"fallo: {error or fallidas[0]}"
    if espera.startswith("filas "):
        n = int(espera.split()[1])
        return None if len(ultima["filas"]) == n else f"devolvio {len(ultima['filas'])} filas"
    if espera.startswith("ids "):
        esperado = [x.strip() for x in espera[4:].split(",")]
        return None if primera_columna(ultima) == esperado else f"devolvio {primera_columna(ultima)}"
    if espera.startswith("igual "):
        otro = anteriores.get(int(espera.split()[1]))
        return None if primera_columna(ultima) == otro else f"devolvio {primera_columna(ultima)} y el otro bloque {otro}"
    if texto and texto[1] == "contiene":
        celdas = "\n".join(str(c) for fila in ultima.get("filas", []) for c in fila)
        return None if texto[2] in celdas else "no aparece en el resultado"
    if texto and texto[1] == "faiss":
        return None if any(r.get("indice") == texto[2] and "error" not in r for r in faiss) else f"faiss: {faiss}"
    return f"expectativa desconocida: {espera}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("archivo")
    args = parser.parse_args()

    bloques = leer_bloques(Path(args.archivo).read_text(encoding="utf-8"))
    (RAIZ / ".build").mkdir(exist_ok=True)
    db = Path(tempfile.mkdtemp(prefix="probar_sql_", dir=RAIZ / ".build"))
    web_api.motor = MotorSQL(db=db.relative_to(RAIZ).as_posix())
    web_api.indices_faiss = web_api.IndicesFaiss()
    anteriores, fallas = {}, 0
    try:
        for bloque in bloques:
            sql = "\n".join(bloque["sql"]).strip()
            respuesta, error, faiss = [], None, []
            try:
                salida = web_api.consultas(web_api.Consulta(sql=sql))
                respuesta, faiss = salida["resultados"], salida["faiss"]
            except HTTPException as e:
                error = str(e.detail)
            if respuesta:
                anteriores[bloque["n"]] = primera_columna(respuesta[-1])
            problemas = [p for p in (revisar(e, respuesta, error, faiss, anteriores) for e in bloque["espera"]) if p]
            fallas += bool(problemas)
            estado = "FALLA" if problemas else "ok"
            print(f"[{bloque['n']:>2}] {estado:<5} {bloque['titulo']}")
            for problema in problemas:
                print(f"           {problema}")
    finally:
        shutil.rmtree(db, ignore_errors=True)
    print(f"\n{len(bloques) - fallas} de {len(bloques)} bloques bien")
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main())
