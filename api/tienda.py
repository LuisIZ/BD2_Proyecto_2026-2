"""API de la tienda (Parte 5, opcion B): catalogo, ficha, recomendaciones y sedes cercanas.

Cada dato sale de consultas SQL al motor, sobre las tablas de docs/tienda.sql. El motor no
tiene JOIN ni OR: las compras, el inventario y las sedes se cruzan aqui, en Python. Las
recomendaciones juntan 4 listas (texto, imagen, metadata y compras) con Reciprocal Rank
Fusion (Cormack, Clarke y Buttcher, 2009). El contrato de las respuestas esta en
web/src/tienda/types.ts.
"""

import importlib
import math
import re
import time
from collections import Counter

from fastapi import APIRouter, HTTPException, Query

router = APIRouter(prefix="/api/tienda")

RRF_K = 60
CANDIDATOS = 20
POR_PAGINA = 12
TIPOS_EN_FILTRO = 40
FUENTES = ("texto", "imagen", "metadata", "compras")
COLUMNAS_RESUMEN = "id, nombre, tipo, marca, color, precio, imagen"
RADIO_TIERRA_M = 6_371_000

# tipos de producto con su cantidad, por base: el GROUP BY recorre la tabla entera
_tipos_por_base = {}


def api():
    """api.web_api (el motor, su lock y la reescritura de SIMILAR_TO). Se importa al usarlo
    porque web_api incluye este router al final de su modulo."""
    return importlib.import_module("api.web_api")


def literal(texto):
    """Texto entre comillas simples para el SQL del motor ('' escapa la comilla)."""
    return "'" + str(texto).replace("'", "''") + "'"


def correr(sql):
    """Ejecuta un lote en el motor; cada sentencia responde por separado con ok o error."""
    with api().lock:
        return api().ejecutar(sql)


def filas_de(respuesta):
    if not respuesta.get("ok"):
        raise HTTPException(500, respuesta.get("error", "el motor no respondio"))
    return [dict(zip(respuesta["columnas"], fila)) for fila in respuesta["filas"]]


def resumen(fila):
    return {
        "id": int(fila["id"]),
        "nombre": fila["nombre"],
        "tipo": fila["tipo"],
        "marca": fila["marca"],
        "color": fila["color"],
        "precio": float(fila["precio"]),
        "imagen": fila["imagen"],
    }


def producto_o_404(respuesta, id):
    filas = filas_de(respuesta)
    if not filas:
        raise HTTPException(404, f"No hay un producto con id {id}.")
    return filas[0]


def resumenes(ids):
    """id -> tarjeta de cada producto, con una sentencia por id en un solo lote (B+ por la PK)."""
    if not ids:
        return {}
    respuestas = correr(";\n".join(f"SELECT {COLUMNAS_RESUMEN} FROM productos WHERE id = {i}" for i in ids))
    return {int(f["id"]): resumen(f) for r in respuestas for f in filas_de(r)}


def tipos_con_cantidad():
    base = api().motor.db
    if base not in _tipos_por_base:
        filas = filas_de(correr("SELECT tipo, COUNT(*) FROM productos GROUP BY tipo")[0])
        conteo = sorted(((f["tipo"], int(f["COUNT(*)"])) for f in filas), key=lambda x: (-x[1], x[0]))
        _tipos_por_base[base] = [{"tipo": t, "productos": n} for t, n in conteo]
    return _tipos_por_base[base]


# --- catalogo y ficha ---


@router.get("/productos")
def productos(q: str = "", tipo: str = "", pagina: int = Query(1, ge=1)):
    """Busca con BM25 sobre la descripcion, o lista por tipo. El motor no tiene OFFSET:
    se piden todas las coincidencias y la pagina se corta aqui; sin filtros se pagina por id."""
    q, tipo = q.strip(), tipo.strip()
    inicio = (pagina - 1) * POR_PAGINA
    if q:
        filtro = f" AND tipo = {literal(tipo)}" if tipo else ""
        consulta = (f"SELECT {COLUMNAS_RESUMEN}, SCORE() AS relevancia FROM productos "
                    f"WHERE MATCH(descripcion, {literal(q)}){filtro} USING BM25 ORDER BY relevancia DESC")
    elif tipo:
        consulta = f"SELECT {COLUMNAS_RESUMEN} FROM productos WHERE tipo = {literal(tipo)}"
    else:
        consulta = f"SELECT {COLUMNAS_RESUMEN} FROM productos WHERE id BETWEEN {inicio + 1} AND {inicio + POR_PAGINA}"

    respuesta = correr(consulta)[0]
    if not respuesta.get("ok"):
        raise HTTPException(400, respuesta.get("error"))
    filas = [resumen(f) for f in filas_de(respuesta)]
    tipos = tipos_con_cantidad()
    if q or tipo:
        total, pagina_filas = len(filas), filas[inicio:inicio + POR_PAGINA]
    else:
        total, pagina_filas = sum(t["productos"] for t in tipos), filas
    return {
        "q": q,
        "tipo": tipo,
        "pagina": pagina,
        "por_pagina": POR_PAGINA,
        "total": total,
        "productos": pagina_filas,
        "tipos": tipos[:TIPOS_EN_FILTRO],
        "consulta": consulta + ";",
    }


@router.get("/productos/{id}")
def producto(id: int):
    consulta = ("SELECT id, item_id, nombre, tipo, categoria, marca, color, descripcion, imagen, precio "
                f"FROM productos WHERE id = {id}")
    fila = producto_o_404(correr(consulta)[0], id)
    return {**resumen(fila), "item_id": fila["item_id"], "categoria": fila["categoria"], "descripcion": fila["descripcion"]}


# --- recomendaciones ---


def rrf(listas, k=RRF_K):
    """Reciprocal Rank Fusion: cada id suma 1 / (k + puesto) por cada lista donde aparece
    (puesto desde 1). Devuelve [(id, score, {fuente: puesto})] del mayor score al menor;
    los empates se rompen por el mejor puesto y luego por id."""
    scores, puestos = {}, {}
    for fuente, ids in listas.items():
        for puesto, id in enumerate(ids, start=1):
            if fuente in puestos.setdefault(id, {}):
                continue  # repetido dentro de la misma lista: cuenta su primer puesto
            puestos[id][fuente] = puesto
            scores[id] = scores.get(id, 0.0) + 1.0 / (k + puesto)
    orden = sorted(scores, key=lambda i: (-scores[i], min(puestos[i].values()), i))
    return [(i, scores[i], puestos[i]) for i in orden]


def sin_repetidos(ids, excluir, limite=CANDIDATOS):
    vistos, salida = {excluir}, []
    for i in ids:
        if i not in vistos:
            vistos.add(i)
            salida.append(i)
        if len(salida) == limite:
            break
    return salida


def lista_metadata(filas, marca, color):
    """Mismo tipo y misma marca o color (el motor no tiene OR: se filtra aqui).
    Primero los que coinciden en las dos, luego en marca y luego en color."""
    def puntos(f):
        return 2 * (bool(marca) and f["marca"] == marca) + (bool(color) and f["color"] == color)
    elegidos = [f for f in filas if puntos(f) > 0]
    return [int(f["id"]) for f in sorted(elegidos, key=lambda f: (-puntos(f), int(f["id"])))]


def lista_compras(producto_id, compradores, compras_por_usuario):
    """Quienes compraron X tambien compraron Y: cuenta en cuantos compradores aparece cada Y."""
    veces = Counter()
    for usuario in compradores:
        veces.update({p for p in compras_por_usuario.get(usuario, []) if p != producto_id})
    return [p for p, _ in sorted(veces.items(), key=lambda x: (-x[1], x[0]))]


@router.get("/productos/{id}/recomendaciones")
def recomendaciones(id: int, k: int = Query(10, ge=1, le=50)):
    inicio = time.perf_counter()
    # 1) el producto, su vector y quienes lo compraron
    base = correr(
        f"SELECT id, nombre, tipo, marca, color FROM productos WHERE id = {id};\n"
        f"SELECT v FROM catalogo WHERE id = {id};\n"
        f"SELECT usuario_id FROM compras WHERE producto_id = {id}"
    )
    fila = producto_o_404(base[0], id)
    vectores = filas_de(base[1]) if base[1].get("ok") else []
    compradores = [int(f["usuario_id"]) for f in filas_de(base[2])] if base[2].get("ok") else []

    # 2) las 4 consultas en un solo lote: cada una responde por separado
    consultas = {
        "texto": (f"SELECT id, SCORE() AS relevancia FROM productos WHERE MATCH(descripcion, {literal(fila['nombre'])}) "
                  f"USING BM25 ORDER BY relevancia DESC LIMIT {CANDIDATOS + 1}"),
        "metadata": f"SELECT id, marca, color FROM productos WHERE tipo = {literal(fila['tipo'])}",
    }
    mostrar = dict(consultas)
    mostrar["metadata"] += f"; -- y aqui: marca = {literal(fila['marca'])} o color = {literal(fila['color'])}"
    mostrar["compras"] = (f"SELECT usuario_id FROM compras WHERE producto_id = {id}; -- y por cada usuario u: "
                          "SELECT producto_id FROM compras WHERE usuario_id = u")
    errores = {}
    if vectores:
        vector = vectores[0]["v"]
        imagen = f"SELECT id, SIMILARITY_SCORE() FROM catalogo WHERE SIMILAR_TO({{}}, k = {CANDIDATOS + 1}) USING HNSW"
        mostrar["imagen"] = imagen.format(", ".join(vector.split(", ")[:3]) + ", ...)")
        try:
            # SIMILAR_TO ... USING HNSW: FAISS da los candidatos y el motor los lee (ver sql_multimedia.md)
            with api().lock:
                consultas["imagen"], _ = api().preparar_similitud(imagen.format(vector))
        except HTTPException as error:
            errores["imagen"] = str(error.detail)
    else:
        mostrar["imagen"] = "SELECT v FROM catalogo WHERE id = " + str(id)
        errores["imagen"] = f"el producto {id} no tiene vector en catalogo"

    nombres = [f for f in ("texto", "imagen", "metadata") if f in consultas]
    lote = [consultas[f] for f in nombres] + [f"SELECT producto_id FROM compras WHERE usuario_id = {u}" for u in compradores]
    respuestas = correr(";\n".join(lote))
    por_fuente = dict(zip(nombres, respuestas))
    compras_por_usuario = {}
    for usuario, r in zip(compradores, respuestas[len(nombres):]):
        if not r.get("ok"):
            errores["compras"] = r.get("error")
            break
        compras_por_usuario[usuario] = [int(f[0]) for f in r["filas"]]
    if not base[2].get("ok"):
        errores["compras"] = base[2].get("error")

    # 3) cada fuente da hasta 20 candidatos, sin el producto consultado ni repetidos
    listas = {}
    for fuente in FUENTES:
        r = por_fuente.get(fuente)
        if fuente in errores or (r is not None and not r.get("ok")):
            errores.setdefault(fuente, r.get("error") if r else "fallo la consulta")
            continue
        if fuente == "metadata":
            ids = lista_metadata(filas_de(r), fila["marca"], fila["color"])
        elif fuente == "compras":
            ids = lista_compras(id, compradores, compras_por_usuario)
        else:
            ids = [int(f[0]) for f in r["filas"]]
        listas[fuente] = sin_repetidos(ids, id)

    # 4) fusion RRF y las tarjetas del top k
    fusion = rrf(listas)[:k]
    tarjetas = resumenes([i for i, _, _ in fusion])
    avisos = [f"la fuente {f} fallo: {errores[f]}" for f in FUENTES if f in errores]
    avisos += [f"la fuente {f} no tiene candidatos" for f in FUENTES if f in listas and not listas[f]]
    return {
        "producto_id": id,
        "k": k,
        "rrf_k": RRF_K,
        "tiempo_ms": round((time.perf_counter() - inicio) * 1000, 1),
        "fuentes": [{
            "fuente": f,
            "consulta": mostrar[f] + ";",
            "candidatos": len(listas.get(f, [])),
            "ok": f not in errores,
            "error": errores.get(f),
        } for f in FUENTES],
        "avisos": avisos,
        "recomendaciones": [{
            **tarjetas[i],
            "score": round(score, 6),
            "fuentes": [f for f in FUENTES if f in puestos],
            "puestos": puestos,
        } for i, score, puestos in fusion if i in tarjetas],
    }


# --- sedes cercanas con stock ---


def leer_punto(texto):
    """'POINT(-12.149000, -77.021000)' -> (lat, lon)"""
    lat, lon = re.findall(r"-?\d+(?:\.\d+)?", texto)
    return float(lat), float(lon)


def haversine_m(a, b):
    """La misma distancia que usa el motor (motor/espacial/distancia.h), en metros."""
    lat1, lon1, lat2, lon2 = map(math.radians, (*a, *b))
    h = math.sin((lat2 - lat1) / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2
    return 2 * RADIO_TIERRA_M * math.asin(math.sqrt(h))


@router.get("/productos/{id}/sedes")
def sedes(id: int, lat: float = Query(..., ge=-90, le=90), lon: float = Query(..., ge=-180, le=180),
          k: int = Query(3, ge=1, le=50)):
    """Las k sedes mas cercanas que tienen stock: el inventario (B+ por producto_id) y las sedes
    ordenadas por distancia (R-Tree) se cruzan aqui, sin JOIN."""
    consultas = [
        f"SELECT sede_id, stock FROM inventario WHERE producto_id = {id} AND stock > 0",
        f"SELECT id, nombre, distrito, ubicacion FROM sedes ORDER BY distancia(ubicacion, POINT({lat}, {lon}))",
    ]
    existe, inventario, cercanas = correr(f"SELECT id FROM productos WHERE id = {id};\n" + ";\n".join(consultas))
    producto_o_404(existe, id)
    stock = {int(f["sede_id"]): int(f["stock"]) for f in filas_de(inventario)}
    salida = []
    for f in filas_de(cercanas):
        if int(f["id"]) not in stock:
            continue
        punto = leer_punto(f["ubicacion"])
        salida.append({
            "id": int(f["id"]),
            "nombre": f["nombre"],
            "distrito": f["distrito"],
            "lat": punto[0],
            "lon": punto[1],
            "distancia_m": round(haversine_m((lat, lon), punto)),
            "stock": stock[int(f["id"])],
        })
        if len(salida) == k:
            break
    return {
        "producto_id": id,
        "origen": {"lat": lat, "lon": lon},
        "k": k,
        "sedes": salida,
        "consultas": [c + ";" for c in consultas],
    }
