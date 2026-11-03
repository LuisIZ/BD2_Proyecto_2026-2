"""Indices IVF y HNSW de las tablas del motor, construidos y mantenidos con FAISS.

    from multimedia.similitud import IndicesFaiss, partir_lote, reescribir
    indices = IndicesFaiss()
    for parte in partir_lote(sql):
        parte, busquedas = reescribir(parte, motor.catalogo(), indices)   # antes de mandarla
        respuestas = motor.ejecutar(parte)
        indices.sincronizar(motor.catalogo())                            # si la parte escribio

El motor no enlaza FAISS. Con CREATE INDEX ... USING IVF|HNSW anota el indice en
su catalogo y vuelca la tabla en <indice>.faiss.cambios; despues, cada INSERT,
DELETE y COPY confirmado agrega ahi sus filas. La API lee ese archivo, aplica los
cambios al indice de FAISS, guarda el indice una sola vez y recien entonces borra
el archivo. Si algo falla antes de borrarlo, los cambios se vuelven a aplicar la
proxima vez (insertar es idempotente porque primero quita la pk).

Para SIMILAR_TO(...) USING IVF|HNSW, la API busca en FAISS y agrega a la
consulta CANDIDATES (pk, ...): el motor lee esas filas, recalcula la distancia y
arma la respuesta. El lote se parte antes de cada una de esas consultas
(partir_lote), para que la busqueda vea lo que hicieron las sentencias anteriores.
"""

import os
import re
import time
from pathlib import Path

import numpy as np

from multimedia.indices import METRICAS, IndiceVectorial

RAIZ = Path(__file__).resolve().parents[2]
TIPOS_VECTORIALES = ("IVF", "HNSW")


def tipo_registro(dimension):
    """Un registro del archivo de cambios: [uint8 'I' | 'D'][int32 pk][K float32]."""
    return np.dtype([("op", "u1"), ("pk", "<i4"), ("v", "<f4", (dimension,))])


def leer_cambios(ruta, dimension):
    """Todos los registros de una vez; vacio si el motor no dejo cambios."""
    tipo = tipo_registro(dimension)
    if not os.path.exists(ruta):
        return np.empty(0, dtype=tipo)
    tamano = os.path.getsize(ruta)
    if tamano % tipo.itemsize:
        raise ValueError(f"{ruta} esta incompleto: {tamano} bytes no es multiplo de {tipo.itemsize}")
    return np.fromfile(ruta, dtype=tipo)


def tramos(cambios):
    """Registros seguidos con la misma operacion: un insertar o un eliminar por tramo, en orden."""
    if not len(cambios):
        return
    cortes = np.flatnonzero(np.diff(cambios["op"])) + 1
    for tramo in np.split(cambios, cortes):
        yield chr(tramo["op"][0]), tramo


def parametros(indice):
    """Metrica y parametros del catalogo con los tipos que espera IndiceVectorial."""
    valores = dict(indice["parametros"])
    metrica = valores.pop("metrica")
    return metrica, {k: float(v) if k == "umbral_reconstruccion" else int(v) for k, v in valores.items()}


def dimension(tabla, indice):
    return next(c["tam"] for c in tabla["columnas"] if c["nombre"].lower() == indice["columna"].lower())


class IndicesFaiss:
    """Los indices abiertos quedan en memoria; se vuelven a leer solo si el archivo cambio."""

    def __init__(self, raiz=RAIZ):
        self.raiz = Path(raiz)
        self._abiertos = {}  # ruta -> (IndiceVectorial, mtime del archivo)

    def ruta(self, indice):
        return self.raiz / indice["archivo"]

    @staticmethod
    def ruta_cambios(ruta):
        return ruta.with_name(ruta.name + ".cambios")

    def abrir(self, indice, dim):
        """El indice al dia, con los cambios pendientes aplicados. Devuelve (IndiceVectorial, resumen)."""
        inicio = time.perf_counter()
        ruta = self.ruta(indice)
        ruta_cambios = self.ruta_cambios(ruta)
        metrica, params = parametros(indice)
        cambios = leer_cambios(ruta_cambios, dim)
        resumen = {"indice": indice["nombre"], "tipo": indice["tipo"], "construido": False, "insertados": 0, "eliminados": 0}

        if ruta.exists():
            abierto = self._abiertos.get(ruta)
            if abierto and abierto[1] == ruta.stat().st_mtime_ns:
                vectorial = abierto[0]
            else:
                vectorial = IndiceVectorial(indice["tipo"], metrica, **params).cargar(os.fspath(ruta))
            for op, tramo in tramos(cambios):
                ids = tramo["pk"].astype(np.int64)
                if op == "I":
                    vectorial.eliminar(ids)  # por si este tramo ya se habia aplicado
                    vectorial.insertar(tramo["v"], ids)
                    resumen["insertados"] += len(ids)
                else:
                    resumen["eliminados"] += vectorial.eliminar(ids)
        else:
            # recien creado: CREATE INDEX volco la tabla entera; se construye con las filas vivas
            vivos = {}
            for op, tramo in tramos(cambios):
                if op == "I":
                    vivos.update(zip(tramo["pk"].tolist(), tramo["v"]))
                else:
                    for pk in tramo["pk"].tolist():
                        vivos.pop(pk, None)
            ids = np.fromiter(vivos, dtype=np.int64, count=len(vivos))
            x = np.stack(list(vivos.values())) if vivos else np.empty((0, dim), dtype=np.float32)
            vectorial = IndiceVectorial(indice["tipo"], metrica, **params).construir(x, ids)
            resumen["construido"] = True
            resumen["insertados"] = len(ids)

        # un solo guardado por lote: FAISS reescribe el archivo entero
        if len(cambios) or resumen["construido"]:
            vectorial.guardar(os.fspath(ruta))
            ruta_cambios.unlink(missing_ok=True)
        self._abiertos[ruta] = (vectorial, ruta.stat().st_mtime_ns)
        resumen["vectores"] = vectorial.cantidad()
        resumen["ms"] = round((time.perf_counter() - inicio) * 1000, 3)
        return vectorial, resumen

    def sincronizar(self, catalogo):
        """Aplica los cambios pendientes de todos los IVF y HNSW del catalogo del motor.

        Un indice que falla no frena a los demas: su error va en el resumen y sus
        cambios quedan en el archivo para el proximo intento."""
        resumenes = []
        vigentes = set()
        for tabla in catalogo["tablas"]:
            for indice in tabla["indices"]:
                if indice["tipo"] not in TIPOS_VECTORIALES:
                    continue
                ruta = self.ruta(indice)
                vigentes.add(ruta)
                if ruta.exists() and not self.ruta_cambios(ruta).exists():
                    continue
                try:
                    resumenes.append(self.abrir(indice, dimension(tabla, indice))[1])
                except (OSError, ValueError, RuntimeError) as error:
                    resumenes.append({"indice": indice["nombre"], "tipo": indice["tipo"], "error": str(error)})
        # los de tablas borradas se sueltan de la memoria
        for ruta in set(self._abiertos) - vigentes:
            del self._abiertos[ruta]
        return resumenes


# --- SIMILAR_TO ... USING IVF|HNSW ---

# los mismos tokens que el lexer del motor (motor/consultas/parser_sql.cpp)
_TOKEN = re.compile(r"""
    (?P<espacio>\s+)
  | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<numero>-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)
  | (?P<texto>'(?:[^']|'')*'|"(?:[^"]|"")*")
  | (?P<simbolo><=|>=|!=|<>|[(),*=<>;])
""", re.VERBOSE)


def tokens(sql):
    """(tipo, texto, inicio, fin) sin espacios; None si hay algo que el motor tampoco entiende."""
    salida, i = [], 0
    while i < len(sql):
        m = _TOKEN.match(sql, i)
        if not m:
            return None
        if m.lastgroup != "espacio":
            salida.append((m.lastgroup, m.group(), m.start(), m.end()))
        i = m.end()
    return salida


def separar_sentencias(sql):
    """Parte el lote por ';' fuera de comillas, como motor::sql::separar_sentencias."""
    partes, actual, comilla = [], [], None
    for c in sql:
        if comilla:
            actual.append(c)
            if c == comilla:
                comilla = None
        elif c in "'\"":
            comilla = c
            actual.append(c)
        elif c == ";":
            partes.append("".join(actual))
            actual = []
        else:
            actual.append(c)
    partes.append("".join(actual))
    return [s.strip() for s in partes if s.strip()]


def _es(token, palabra):
    return token is not None and token[0] == "ident" and token[1].upper() == palabra


def _simbolo(token, simbolo):
    return token is not None and token[0] == "simbolo" and token[1] == simbolo


def analizar(sql):
    """Lo que la API necesita de una sentencia con SIMILAR_TO; None si no lo tiene o no se entiende.

    Solo se leen las piezas que importan (tabla, argumentos de SIMILAR_TO, USING y
    WITH METRIC): la sintaxis completa la valida el motor."""
    t = tokens(sql)
    if not t:
        return None

    def sig(i):
        return t[i] if i < len(t) else None

    i = next((i for i, x in enumerate(t) if _es(x, "SIMILAR_TO") and _simbolo(sig(i + 1), "(")), None)
    if i is None:
        return None
    consulta = {
        "tabla": next((sig(j + 1)[1] for j, x in enumerate(t) if _es(x, "FROM") and sig(j + 1) and sig(j + 1)[0] == "ident"), None),
        "explain": _es(sig(0), "EXPLAIN") and not (_es(sig(1), "ANALYZE") or _es(sig(1), "ANALYSE")),
        "columna": None, "archivo": None, "vector": None, "tramo": None, "k": 10,
        "indice": None, "fin_using": None, "candidatos": False, "metrica": "COSENO",
    }

    # SIMILAR_TO([col,] 'archivo' | VECTOR(...) [, k = n])
    j = i + 2
    if sig(j) and sig(j)[0] == "ident" and not (_es(sig(j), "VECTOR") and _simbolo(sig(j + 1), "(")):
        consulta["columna"] = sig(j)[1]
        if not _simbolo(sig(j + 1), ","):
            return None
        j += 2
    if sig(j) and sig(j)[0] == "texto":
        comilla = sig(j)[1][0]
        consulta["archivo"] = sig(j)[1][1:-1].replace(comilla * 2, comilla)
        consulta["tramo"] = (sig(j)[2], sig(j)[3])
        j += 1
    elif _es(sig(j), "VECTOR"):
        inicio = sig(j)[2]
        j += 2
        componentes = []
        while sig(j) and sig(j)[0] == "numero":
            componentes.append(float(sig(j)[1]))
            j += 2 if _simbolo(sig(j + 1), ",") else 1
        if not componentes or not _simbolo(sig(j), ")"):
            return None
        consulta["vector"] = np.array(componentes, dtype=np.float32)
        consulta["tramo"] = (inicio, sig(j)[3])
        j += 1
    else:
        return None
    if _simbolo(sig(j), ",") and _es(sig(j + 1), "K") and _simbolo(sig(j + 2), "=") and sig(j + 3) and sig(j + 3)[0] == "numero":
        consulta["k"] = int(sig(j + 3)[1])

    for j in range(i, len(t)):
        if _es(t[j], "USING") and (_es(sig(j + 1), "IVF") or _es(sig(j + 1), "HNSW")):
            consulta["indice"] = sig(j + 1)[1].upper()
            consulta["fin_using"] = sig(j + 1)[3]
            consulta["candidatos"] = _es(sig(j + 2), "CANDIDATES")
        if _es(t[j], "WITH") and _es(sig(j + 1), "METRIC") and _simbolo(sig(j + 2), "=") and sig(j + 3):
            nombre = sig(j + 3)[1].strip("'\"").upper()
            consulta["metrica"] = METRICAS.get(nombre, nombre)
    return consulta


def partir_lote(sql):
    """Parte el lote antes de cada SIMILAR_TO ... USING que este fuera de una transaccion.

    La API busca en FAISS antes de mandar cada parte al motor; asi la busqueda ve lo que las
    sentencias anteriores del mismo lote crearon, insertaron o borraron. Dentro de una
    transaccion no se parte, porque el motor deshace la transaccion abierta al terminar cada
    lote: ahi la busqueda ve lo confirmado antes del BEGIN, igual que FAISS solo ve lo confirmado."""
    partes, actual, en_transaccion = [], [], False
    for sentencia in separar_sentencias(sql):
        consulta = analizar(sentencia)
        if actual and not en_transaccion and consulta and consulta["indice"] and not consulta["candidatos"]:
            partes.append(actual)
            actual = []
        actual.append(sentencia)
        t = tokens(sentencia)
        primera = t[0][1].upper() if t else ""
        if primera in ("BEGIN", "START"):
            en_transaccion = True
        elif primera in ("COMMIT", "END", "ROLLBACK"):
            en_transaccion = False
    if actual:
        partes.append(actual)
    return [";\n".join(parte) for parte in partes]


def _columna_vector(tabla, columna):
    """La columna VECTOR de SIMILAR_TO: la nombrada o, si no se nombra, la unica de la tabla."""
    vectores = [c for c in tabla["columnas"] if c["tipo"] == "VECTOR"]
    if columna:
        return next((c for c in vectores if c["nombre"].lower() == columna.lower()), None)
    return vectores[0] if len(vectores) == 1 else None


def literal_vector(vector):
    return "VECTOR(" + ", ".join(f"{float(x):.9g}" for x in vector) + ")"


def reescribir(sql, catalogo, indices, extraer=None):
    """Prepara un lote para el motor. En cada SIMILAR_TO:

    - con un archivo y una funcion `extraer`, cambia 'archivo' por el VECTOR(...) de la imagen;
    - con USING IVF|HNSW (salvo en un EXPLAIN sin ANALYZE, que no ejecuta), busca los k
      vecinos en FAISS y agrega CANDIDATES (pk, ...) despues de USING.

    Lo que no se puede resolver (no hay tabla, columna o indice, o la metrica no es la del
    indice) se deja igual, para que el motor responda con su propio error. Devuelve el lote y
    un resumen por cada busqueda en FAISS."""
    sentencias, busquedas = [], []
    for sentencia in separar_sentencias(sql):
        consulta = analizar(sentencia)
        if consulta is None:
            sentencias.append(sentencia)
            continue
        cambios = []  # (inicio, fin, texto nuevo) sobre la sentencia original
        vector = consulta["vector"]
        if consulta["archivo"] is not None and extraer is not None:
            vector = np.asarray(extraer(consulta["archivo"]), dtype=np.float32)
            cambios.append((*consulta["tramo"], literal_vector(vector)))
        tabla = next((x for x in catalogo["tablas"] if consulta["tabla"] and x["nombre"].lower() == consulta["tabla"].lower()), None)
        columna = _columna_vector(tabla, consulta["columna"]) if tabla else None
        if consulta["indice"] and not consulta["candidatos"] and not consulta["explain"] and vector is not None and columna:
            indice = next((x for x in tabla["indices"]
                           if x["tipo"] == consulta["indice"] and x["columna"].lower() == columna["nombre"].lower()), None)
            if indice and indice["parametros"]["metrica"] == consulta["metrica"] and len(vector) == columna["tam"]:
                vectorial, resumen = indices.abrir(indice, columna["tam"])
                inicio = time.perf_counter()
                pks = [pk for pk, _ in vectorial.knn(vector, consulta["k"])]
                busquedas.append({
                    "indice": indice["nombre"], "tipo": indice["tipo"], "k": consulta["k"], "candidatos": len(pks),
                    "vectores": vectorial.cantidad(), "ms": round((time.perf_counter() - inicio) * 1000, 3),
                    "parametros": {k: v for k, v in indice["parametros"].items() if k in ("ef_busqueda", "nprobe")},
                    "cambios_aplicados": resumen["insertados"] + resumen["eliminados"],
                })
                cambios.append((consulta["fin_using"], consulta["fin_using"], " CANDIDATES (" + ", ".join(map(str, pks)) + ")"))
        for inicio, fin, nuevo in sorted(cambios, reverse=True):
            sentencia = sentencia[:inicio] + nuevo + sentencia[fin:]
        sentencias.append(sentencia)
    return ";\n".join(sentencias), busquedas
