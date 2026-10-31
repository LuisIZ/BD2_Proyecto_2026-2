"""Indices IVF y HNSW para vectores, sobre FAISS (IndexIVFFlat e IndexHNSWFlat).

    from multimedia.indices import IndiceVectorial
    indice = IndiceVectorial("HNSW", "cosine", M=32, ef_busqueda=64)
    indice.construir(vectores, ids)         # vectores: numpy float32 (n, K)
    indice.knn(consulta, 10)                # [(id, distancia), ...] del mas cercano al mas lejano

Las distancias siguen la convencion del motor (motor/vectorial/vector.h):
menor = mas parecido. Euclidiana sin elevar al cuadrado, producto punto como
-a.b y coseno como 1 - cos, con los vectores normalizados (faiss.normalize_L2).
"""

import os

import faiss
import numpy as np

TIPOS = ("IVF", "HNSW")

METRICAS = {
    "EUCLIDIANA": "EUCLIDIANA", "EUCLIDEAN": "EUCLIDIANA", "L2": "EUCLIDIANA",
    "PRODUCTO_PUNTO": "PRODUCTO_PUNTO", "INNER_PRODUCT": "PRODUCTO_PUNTO", "DOT": "PRODUCTO_PUNTO", "IP": "PRODUCTO_PUNTO",
    "COSENO": "COSENO", "COSINE": "COSENO",
}

# valores por defecto de FAISS, salvo nlist y nprobe
PARAMETROS = {
    "IVF": {"nlist": 100, "nprobe": 8},
    "HNSW": {"M": 32, "ef_construccion": 40, "ef_busqueda": 16},
}


class IndiceVectorial:
    def __init__(self, tipo, metrica, **parametros):
        self.tipo = tipo.upper()
        if self.tipo not in TIPOS:
            raise ValueError(f"tipo de indice desconocido: {tipo}")
        if metrica.upper() not in METRICAS:
            raise ValueError(f"metrica desconocida: {metrica}")
        self.metrica = METRICAS[metrica.upper()]
        desconocidos = set(parametros) - set(PARAMETROS[self.tipo])
        if desconocidos:
            raise ValueError(f"parametros desconocidos para {self.tipo}: {sorted(desconocidos)}")
        self.parametros = {**PARAMETROS[self.tipo], **parametros}
        if self.tipo == "IVF" and (self.parametros["nlist"] <= 0 or self.parametros["nprobe"] <= 0):
            raise ValueError("nlist y nprobe deben ser mayores que cero")
        if self.tipo == "HNSW" and any(
            self.parametros[nombre] <= 0 for nombre in ("M", "ef_construccion", "ef_busqueda")
        ):
            raise ValueError("M, ef_construccion y ef_busqueda deben ser mayores que cero")
        self.indice = None
        self.ruta = None

    def _preparar(self, vectores):
        """float32 contiguo de forma (n, K); el coseno trabaja sobre vectores normalizados."""
        x = np.array(vectores, dtype=np.float32, order="C", ndmin=2)
        if self.metrica == "COSENO":
            faiss.normalize_L2(x)
        return x

    def _metrica_faiss(self):
        # el coseno es producto interno entre vectores normalizados
        return faiss.METRIC_L2 if self.metrica == "EUCLIDIANA" else faiss.METRIC_INNER_PRODUCT

    def _crear_ivf(self, x):
        nlist = self.parametros["nlist"]
        if nlist > len(x):
            raise ValueError(f"nlist = {nlist} necesita al menos {nlist} vectores y hay {len(x)}")
        dimension = x.shape[1]
        # el cuantizador grueso guarda los nlist centroides y decide a que lista va cada vector
        cuantizador = faiss.IndexFlatL2(dimension) if self.metrica == "EUCLIDIANA" else faiss.IndexFlatIP(dimension)
        indice = faiss.IndexIVFFlat(cuantizador, dimension, nlist, self._metrica_faiss())
        indice.cp.seed = 2026  # semilla fija del k-means (Lloyd)
        indice.train(x)
        return indice

    def construir(self, vectores, ids):
        """ids son las claves primarias de la tabla, en el mismo orden que vectores."""
        x = self._preparar(vectores)
        ids = np.asarray(ids, dtype=np.int64)
        if len(ids) != len(x):
            raise ValueError(f"hay {len(x)} vectores y {len(ids)} ids")
        if self.tipo == "IVF":
            self.indice = self._crear_ivf(x)
        else:
            dimension = x.shape[1]
            base = faiss.IndexHNSWFlat(dimension, self.parametros["M"], self._metrica_faiss())
            base.hnsw.efConstruction = self.parametros["ef_construccion"]
            base.hnsw.efSearch = self.parametros["ef_busqueda"]
            self.indice = faiss.IndexIDMap2(base)
        self.indice.add_with_ids(x, ids)
        self.ruta = None
        return self

    def knn(self, consulta, k):
        if self.indice is None:
            raise RuntimeError("el indice no esta construido ni cargado")
        if self.tipo == "IVF":
            self.indice.nprobe = self.parametros["nprobe"]
        else:
            faiss.downcast_index(self.indice.index).hnsw.efSearch = self.parametros["ef_busqueda"]
        distancias, ids = self.indice.search(self._preparar(consulta), k)
        if self.metrica == "EUCLIDIANA":
            distancias = np.sqrt(np.maximum(distancias, 0))  # FAISS devuelve la L2 al cuadrado
        elif self.metrica == "PRODUCTO_PUNTO":
            distancias = -distancias  # FAISS devuelve a.b (mayor es mejor)
        else:
            distancias = 1 - distancias  # a.b entre normalizados = cos
        # FAISS rellena con -1 si las listas revisadas tienen menos de k vectores
        return [(int(i), float(d)) for i, d in zip(ids[0], distancias[0]) if i != -1]

    def guardar(self, ruta):
        if self.indice is None:
            raise RuntimeError("el indice no esta construido")
        faiss.write_index(self.indice, ruta)
        self.ruta = ruta

    def cargar(self, ruta):
        self.indice = faiss.read_index(ruta)
        self.ruta = ruta
        return self

    def tamano_en_disco(self):
        """Bytes del archivo del indice (o los que ocuparia si aun no se guardo)."""
        if self.ruta and os.path.exists(self.ruta):
            return os.path.getsize(self.ruta)
        if self.indice is None:
            return 0
        return int(faiss.serialize_index(self.indice).nbytes)
