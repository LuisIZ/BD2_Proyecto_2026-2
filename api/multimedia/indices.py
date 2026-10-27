"""Indices IVF y HNSW para vectores, sobre FAISS (IndexIVFFlat e IndexHNSWFlat).

    from multimedia.indices import IndiceVectorial
    indice = IndiceVectorial("HNSW", "cosine", M=32, ef_busqueda=64)
    indice.construir(vectores, ids)         # vectores: numpy float32 (n, K)
    indice.knn(consulta, 10)                # [(id, distancia), ...] del mas cercano al mas lejano

Las distancias siguen la convencion del motor (motor/vectorial/vector.h):
menor = mas parecido. Euclidiana sin elevar al cuadrado, producto punto como
-a.b y coseno como 1 - cos, con los vectores normalizados (faiss.normalize_L2).
"""

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
        self.indice = None

    def construir(self, vectores, ids):
        """ids son las claves primarias de la tabla, en el mismo orden que vectores."""
        raise NotImplementedError("no implementado")

    def knn(self, consulta, k):
        raise NotImplementedError("no implementado")

    def guardar(self, ruta):
        raise NotImplementedError("no implementado")

    def cargar(self, ruta):
        raise NotImplementedError("no implementado")

    def tamano_en_disco(self):
        raise NotImplementedError("no implementado")
