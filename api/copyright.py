from fastapi import APIRouter, HTTPException, Request
from fastapi.concurrency import run_in_threadpool

router = APIRouter()

UMBRAL = 90.0
TRAMOS_MINIMOS = 2
K_TRAMO = 5
HOP = 5.0
DURACION_TRAMO = 10.0


def literal(vector):
    return "VECTOR(" + ",".join(f"{float(x):.8g}" for x in vector) + ")"


def consulta_de_tramo(vector):
    return (f"SELECT id, cancion_id, inicio, fin, SIMILARITY_SCORE() AS similitud FROM segmentos "
            f"WHERE SIMILAR_TO({literal(vector)}, k = {K_TRAMO}) USING HNSW;")


def tramos_del_audio(ruta):
    import librosa

    from multimedia.segmentos_audio import cargar_vocabulario, descriptores_de_tramos, vectores

    indice, idf, k = cargar_vocabulario()
    lista = descriptores_de_tramos(ruta, DURACION_TRAMO, HOP)
    matriz = vectores([d for *_, d in lista], indice, idf, k)
    return librosa.get_duration(path=str(ruta)), [(i, f, v) for (i, f, _), v in zip(lista, matriz)]


def agrupar(tramos, umbral=UMBRAL, minimos=TRAMOS_MINIMOS):
    # cuenta en cuantos tramos aparece cada cancion por encima del umbral
    por_cancion = {}
    for tramo in tramos:
        vistas = set()
        for c in tramo["coincidencias"]:
            if not c["supera_umbral"] or c["cancion_id"] in vistas:
                continue
            vistas.add(c["cancion_id"])
            datos = por_cancion.setdefault(c["cancion_id"], {
                "cancion_id": c["cancion_id"], "archivo": c["archivo"], "genero": c["genero"],
                "tramos": 0, "porcentaje": 0.0, "similitud_maxima": 0.0,
            })
            datos["tramos"] += 1
            datos["similitud_maxima"] = max(datos["similitud_maxima"], c["similitud"])
    # con un solo tramo en el audio basta que coincida ese
    necesarios = min(minimos, len(tramos)) if tramos else minimos
    canciones = [c for c in por_cancion.values() if c["tramos"] >= necesarios]
    for c in canciones:
        c["porcentaje"] = round(100.0 * c["tramos"] / len(tramos), 1)
    canciones.sort(key=lambda c: (-c["tramos"], -c["similitud_maxima"], c["cancion_id"]))
    veredicto = "posible_copyright" if canciones else "sin_coincidencias"
    return canciones, veredicto


def analizar_audio(ruta, umbral=UMBRAL):
    # un lote con un SIMILAR_TO por tramo y al final los nombres de las canciones
    from api import web_api

    duracion, vectores = tramos_del_audio(ruta)
    con_sonido = [i for i, (_, _, v) in enumerate(vectores) if v.any()]
    consultas = [consulta_de_tramo(vectores[i][2]) for i in con_sonido]
    consultas.append("SELECT id, archivo, genero FROM canciones;")
    try:
        resultados = web_api.consultas(web_api.Consulta(sql="\n".join(consultas)))["resultados"]
    except HTTPException as error:
        raise HTTPException(400, f"{error.detail}. Corre docs/copyright.sql antes de analizar.") from error
    fallidas = [r["error"] for r in resultados if not r.get("ok")]
    if fallidas or len(resultados) != len(consultas):
        raise HTTPException(400, f"{fallidas[0] if fallidas else 'respuesta incompleta del motor'}. "
                                 "Corre docs/copyright.sql antes de analizar.")

    canciones = {int(f[0]): (f[1], f[2]) for f in resultados[-1]["filas"]}
    filas_por_tramo = dict(zip(con_sonido, (r["filas"] for r in resultados[:-1])))
    tramos = []
    for i, (inicio, fin, _) in enumerate(vectores):
        coincidencias = []
        for _, cancion_id, inicio_original, fin_original, score in filas_por_tramo.get(i, []):
            similitud = round(100.0 * float(score), 1)
            archivo, genero = canciones.get(int(cancion_id), ("", ""))
            coincidencias.append({
                "cancion_id": int(cancion_id), "archivo": archivo, "genero": genero,
                "inicio_original": float(inicio_original), "fin_original": float(fin_original),
                "similitud": similitud, "supera_umbral": similitud >= umbral,
            })
        coincidencias.sort(key=lambda c: -c["similitud"])
        tramos.append({"inicio": round(inicio, 2), "fin": round(fin, 2), "coincidencias": coincidencias})

    detectadas, veredicto = agrupar(tramos, umbral)
    return {
        "duracion": round(duracion, 2), "umbral": umbral, "veredicto": veredicto,
        "tramos": tramos, "canciones": detectadas, "consultas": consultas,
    }


@router.post("/api/copyright/analizar")
async def analizar(request: Request, nombre: str):
    from api import web_api

    if not nombre.lower().endswith(tuple(web_api.EXTENSIONES_AUDIO)):
        raise HTTPException(400, "el archivo debe ser un audio: " + ", ".join(sorted(web_api.EXTENSIONES_AUDIO)))
    try:
        archivo = web_api.guardar_subida(await request.body(), nombre)
    except ValueError as error:
        raise HTTPException(400, str(error)) from error
    resultado = await run_in_threadpool(analizar_audio, web_api.SUBIDAS / archivo)
    return {"archivo": archivo, **resultado}
