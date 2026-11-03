"""Indices IVF y HNSW de las tablas del motor, construidos y mantenidos con FAISS.

    from multimedia.similitud import IndicesFaiss
    indices = IndicesFaiss()
    indices.sincronizar(motor.catalogo())   # despues de cada lote que escribe

El motor no enlaza FAISS. Con CREATE INDEX ... USING IVF|HNSW anota el indice en
su catalogo y vuelca la tabla en <indice>.faiss.cambios; despues, cada INSERT,
DELETE y COPY confirmado agrega ahi sus filas. La API lee ese archivo, aplica los
cambios al indice de FAISS, guarda el indice una sola vez y recien entonces borra
el archivo. Si algo falla antes de borrarlo, los cambios se vuelven a aplicar la
proxima vez (insertar es idempotente porque primero quita la pk).
"""

import os
import time
from pathlib import Path

import numpy as np

from multimedia.indices import IndiceVectorial

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
