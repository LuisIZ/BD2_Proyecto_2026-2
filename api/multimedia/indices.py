"""Indices IVF y HNSW para vectores, sobre FAISS (IndexIVFFlat e IndexHNSWFlat).

    from multimedia.indices import IndiceVectorial
    indice = IndiceVectorial("HNSW", "cosine", M=32, ef_busqueda=64)
    indice.construir(vectores, ids)         # vectores: numpy float32 (n, K)
    indice.knn(consulta, 10)                # [(id, distancia), ...] del mas cercano al mas lejano
    indice.insertar(nuevos, ids_nuevos)     # sin reconstruir
    indice.eliminar(ids_borrados)           # devuelve cuantos estaban

Las distancias siguen la convencion del motor (motor/vectorial/vector.h):
menor = mas parecido. Euclidiana sin elevar al cuadrado, producto punto como
-a.b y coseno como 1 - cos, con los vectores normalizados (faiss.normalize_L2).

El IVF guarda las pk en sus listas y borra de verdad (remove_ids). El HNSW no
sabe borrar, porque quitar un nodo deja a sus vecinos mal conectados: el
borrado es logico. FAISS numera los vectores del HNSW por orden de llegada
(posicion interna); el adaptador guarda posicion -> pk y las posiciones
borradas, y la busqueda las salta con un IDSelector. Cuando las borradas pasan
umbral_reconstruccion se reconstruye el grafo solo con los vivos.
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

# valores por defecto de FAISS, salvo nlist, nprobe y el umbral (el 30 % de la
# reorganizacion del archivo secuencial)
PARAMETROS = {
    "IVF": {"nlist": 100, "nprobe": 8},
    "HNSW": {"M": 32, "ef_construccion": 40, "ef_busqueda": 16, "umbral_reconstruccion": 0.30},
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
        if self.tipo == "HNSW" and not 0 < self.parametros["umbral_reconstruccion"] <= 1:
            raise ValueError("umbral_reconstruccion debe estar entre 0 y 1")
        self.indice = None
        self.ruta = None
        # solo HNSW
        self.pks = np.empty(0, dtype=np.int64)   # posicion interna -> pk
        self.borrados = np.empty(0, dtype=bool)  # posiciones con borrado logico
        self.posiciones = {}                     # pk vivo -> posicion interna
        self.reconstrucciones = 0
        self._selector = None

    def _preparar(self, vectores):
        """float32 contiguo de forma (n, K); el coseno trabaja sobre vectores normalizados."""
        x = np.array(vectores, dtype=np.float32, order="C", ndmin=2)
        if self.metrica == "COSENO":
            faiss.normalize_L2(x)
        return x

    def _metrica_faiss(self):
        # el coseno es producto interno entre vectores normalizados
        return faiss.METRIC_L2 if self.metrica == "EUCLIDIANA" else faiss.METRIC_INNER_PRODUCT

    def _exigir_indice(self):
        if self.indice is None:
            raise RuntimeError("el indice no esta construido ni cargado")

    @staticmethod
    def _validar_ids(ids, n):
        ids = np.asarray(ids, dtype=np.int64).reshape(-1)
        if len(ids) != n:
            raise ValueError(f"hay {n} vectores y {len(ids)} ids")
        if len(np.unique(ids)) != len(ids):
            raise ValueError("hay ids repetidos")
        return ids

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

    def _crear_hnsw(self, dimension):
        indice = faiss.IndexHNSWFlat(dimension, self.parametros["M"], self._metrica_faiss())
        # se fija antes de insertar: cuantos candidatos se revisan al conectar cada nodo
        indice.hnsw.efConstruction = self.parametros["ef_construccion"]
        self.pks = np.empty(0, dtype=np.int64)
        self.borrados = np.empty(0, dtype=bool)
        self.posiciones = {}
        self._selector = None
        return indice

    def _agregar_hnsw(self, x, ids):
        inicio = self.indice.ntotal
        self.indice.add(x)  # las posiciones nuevas son inicio, inicio + 1, ...
        self.pks = np.concatenate([self.pks, ids])
        self.borrados = np.concatenate([self.borrados, np.zeros(len(ids), dtype=bool)])
        self.posiciones.update(zip(ids.tolist(), range(inicio, inicio + len(ids))))

    def construir(self, vectores, ids):
        """ids son las claves primarias de la tabla, en el mismo orden que vectores."""
        x = self._preparar(vectores)
        ids = self._validar_ids(ids, len(x))
        if self.tipo == "IVF":
            self.indice = self._crear_ivf(x)
            self.indice.add_with_ids(x, ids)
        else:
            self.indice = self._crear_hnsw(x.shape[1])
            self._agregar_hnsw(x, ids)
            self.reconstrucciones = 0
        self.ruta = None
        return self

    def insertar(self, vectores, ids):
        """Agrega vectores sin reconstruir; los ids no pueden estar ya en el indice."""
        self._exigir_indice()
        x = self._preparar(vectores)
        if x.shape[1] != self.indice.d:
            raise ValueError(f"el indice tiene dimension {self.indice.d} y el vector {x.shape[1]}")
        ids = self._validar_ids(ids, len(x))
        if self.tipo == "IVF":
            # va a la lista de su centroide mas cercano, sin reentrenar; las pk unicas las asegura el motor
            self.indice.add_with_ids(x, ids)
        else:
            repetidos = [pk for pk in ids.tolist() if pk in self.posiciones]
            if repetidos:
                raise ValueError(f"el id {repetidos[0]} ya esta en el indice")
            self._agregar_hnsw(x, ids)
        self.ruta = None

    def eliminar(self, ids):
        """Quita los ids del indice y devuelve cuantos estaban; los que no estan se ignoran."""
        self._exigir_indice()
        ids = np.asarray(ids, dtype=np.int64).reshape(-1)
        if self.tipo == "IVF":
            quitados = int(self.indice.remove_ids(ids))
        else:
            posiciones = [self.posiciones.pop(pk) for pk in ids.tolist() if pk in self.posiciones]
            self.borrados[posiciones] = True
            self._selector = None
            quitados = len(posiciones)
            if self.fraccion_borrada() > self.parametros["umbral_reconstruccion"]:
                self._reconstruir()
        if quitados:
            self.ruta = None
        return quitados

    def fraccion_borrada(self):
        """Posiciones del HNSW con borrado logico sobre el total (0 en IVF)."""
        return float(self.borrados.mean()) if len(self.borrados) else 0.0

    def cantidad(self):
        """Vectores vivos en el indice."""
        if self.indice is None:
            return 0
        return self.indice.ntotal if self.tipo == "IVF" else len(self.posiciones)

    def _reconstruir(self):
        """Vuelve a construir el HNSW solo con los vivos; las posiciones quedan compactas."""
        vivas = np.flatnonzero(~self.borrados)
        dimension = self.indice.d
        # los vectores guardados ya pasaron por _preparar (normalizados si es coseno)
        x = self.indice.reconstruct_batch(vivas) if len(vivas) else np.empty((0, dimension), dtype=np.float32)
        pks = self.pks[vivas]
        self.indice = self._crear_hnsw(dimension)
        self._agregar_hnsw(x, pks)
        self.reconstrucciones += 1

    def _parametros_busqueda(self):
        """ef_busqueda de cada consulta y, si hay borrados, el filtro que salta esas posiciones."""
        params = faiss.SearchParametersHNSW()
        params.efSearch = self.parametros["ef_busqueda"]
        if self.borrados.any():
            if self._selector is None:
                lote = faiss.IDSelectorBatch(np.flatnonzero(self.borrados).astype(np.int64))
                # se guardan los dos: FAISS solo tiene punteros y Python no debe liberarlos
                self._selector = (lote, faiss.IDSelectorNot(lote))
            params.sel = self._selector[1]
        return params

    def knn(self, consulta, k):
        self._exigir_indice()
        x = self._preparar(consulta)
        if self.tipo == "IVF":
            self.indice.nprobe = self.parametros["nprobe"]
            distancias, etiquetas = self.indice.search(x, k)
        else:
            distancias, etiquetas = self.indice.search(x, k, params=self._parametros_busqueda())
        if self.metrica == "EUCLIDIANA":
            distancias = np.sqrt(np.maximum(distancias, 0))  # FAISS devuelve la L2 al cuadrado
        elif self.metrica == "PRODUCTO_PUNTO":
            distancias = -distancias  # FAISS devuelve a.b (mayor es mejor)
        else:
            distancias = 1 - distancias  # a.b entre normalizados = cos
        # FAISS rellena con -1 si encuentra menos de k vectores
        pares = [(int(e), float(d)) for e, d in zip(etiquetas[0], distancias[0]) if e != -1]
        if self.tipo == "HNSW":
            pares = [(int(self.pks[e]), d) for e, d in pares]  # posicion interna -> pk
        return pares

    def guardar(self, ruta):
        self._exigir_indice()
        if self.tipo == "IVF":
            faiss.write_index(self.indice, ruta)
        else:
            # FAISS no conoce las pk ni los borrados: van en el mismo archivo que el grafo
            with open(ruta, "wb") as f:
                np.savez(f, indice=faiss.serialize_index(self.indice), pks=self.pks, borrados=self.borrados)
        self.ruta = ruta

    def cargar(self, ruta):
        """M y ef_construccion salen del archivo; ef_busqueda y nprobe, del constructor."""
        if self.tipo == "IVF":
            indice = faiss.read_index(ruta)
        else:
            with np.load(ruta) as datos:
                indice = faiss.deserialize_index(datos["indice"])
                pks, borrados = datos["pks"], datos["borrados"]
        if indice.metric_type != self._metrica_faiss():
            raise ValueError(f"el indice de {ruta} no usa la metrica {self.metrica.lower()}")
        self.indice = indice
        if self.tipo == "HNSW":
            self.parametros["M"] = indice.hnsw.nb_neighbors(1)
            self.parametros["ef_construccion"] = indice.hnsw.efConstruction
            self.pks, self.borrados = pks, borrados
            self.posiciones = {int(pk): p for p, pk in enumerate(pks.tolist()) if not borrados[p]}
            self._selector = None
        self.ruta = ruta
        return self

    def tamano_en_disco(self):
        """Bytes del archivo del indice (o los que ocuparia si aun no se guardo)."""
        if self.ruta and os.path.exists(self.ruta):
            return os.path.getsize(self.ruta)
        if self.indice is None:
            return 0
        return int(faiss.serialize_index(self.indice).nbytes) + self.pks.nbytes + self.borrados.nbytes
