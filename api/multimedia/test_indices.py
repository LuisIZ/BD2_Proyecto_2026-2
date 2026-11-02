"""Pruebas de los indices IVF y HNSW contra la fuerza bruta.

    cd api && python -m unittest multimedia.test_indices -v
"""

import os
import tempfile
import unittest

import numpy as np

from multimedia.indices import IndiceVectorial

DIMENSION = 64
METRICAS = ("euclidean", "inner_product", "cosine")


def datos_agrupados(n, grupos=20, semilla=7):
    """Vectores alrededor de unos centros, como los descriptores de imagenes parecidas."""
    rng = np.random.default_rng(semilla)
    centros = rng.standard_normal((grupos, DIMENSION)) * 4
    x = centros[rng.integers(0, grupos, n)] + rng.standard_normal((n, DIMENSION))
    return x.astype(np.float32)


def fuerza_bruta(vectores, ids, consulta, k, metrica):
    """Las distancias de motor/vectorial/vector.h, recorriendo todos los vectores."""
    if metrica == "euclidean":
        d = np.linalg.norm(vectores - consulta, axis=1)
    elif metrica == "inner_product":
        d = -(vectores @ consulta)
    else:
        d = 1 - (vectores @ consulta) / (np.linalg.norm(vectores, axis=1) * np.linalg.norm(consulta))
    orden = np.argsort(d, kind="stable")[:k]
    return [(int(ids[i]), float(d[i])) for i in orden]


def recall(resultado, esperado):
    return len({i for i, _ in resultado} & {i for i, _ in esperado}) / len(esperado)


class PruebasIVF(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vectores = datos_agrupados(3000)
        cls.ids = np.arange(1, 3001) * 10  # ids distintos de la posicion, como una PK
        cls.consultas = datos_agrupados(20, semilla=99)

    def test_nprobe_igual_a_nlist_es_fuerza_bruta(self):
        for metrica in METRICAS:
            with self.subTest(metrica=metrica):
                indice = IndiceVectorial("IVF", metrica, nlist=30, nprobe=30).construir(self.vectores, self.ids)
                for consulta in self.consultas:
                    resultado = indice.knn(consulta, 10)
                    esperado = fuerza_bruta(self.vectores, self.ids, consulta, 10, metrica)
                    self.assertEqual([i for i, _ in resultado], [i for i, _ in esperado])
                    np.testing.assert_allclose([d for _, d in resultado], [d for _, d in esperado],
                                               rtol=1e-4, atol=1e-3)

    def test_distancias_de_menor_a_mayor(self):
        for metrica in METRICAS:
            with self.subTest(metrica=metrica):
                indice = IndiceVectorial("IVF", metrica, nlist=30).construir(self.vectores, self.ids)
                distancias = [d for _, d in indice.knn(self.consultas[0], 10)]
                self.assertEqual(distancias, sorted(distancias))

    def test_nprobe_chico_pierde_poco_recall(self):
        # con nprobe = 1 de 30 listas solo se compara ~1/30 de los vectores
        for metrica in METRICAS:
            indice = IndiceVectorial("IVF", metrica, nlist=30, nprobe=1).construir(self.vectores, self.ids)
            valores = [recall(indice.knn(c, 10), fuerza_bruta(self.vectores, self.ids, c, 10, metrica))
                       for c in self.consultas]
            print(f"\n  Recall@10 con nprobe=1/30 ({metrica}): {np.mean(valores):.2f}", end="")
            self.assertGreater(np.mean(valores), 0.5)

    def test_guardar_y_cargar(self):
        indice = IndiceVectorial("IVF", "cosine", nlist=30, nprobe=4).construir(self.vectores, self.ids)
        with tempfile.TemporaryDirectory() as carpeta:
            ruta = os.path.join(carpeta, "ivf.index")
            indice.guardar(ruta)
            self.assertEqual(indice.tamano_en_disco(), os.path.getsize(ruta))
            cargado = IndiceVectorial("IVF", "cosine", nprobe=4).cargar(ruta)
            self.assertEqual(cargado.knn(self.consultas[0], 10), indice.knn(self.consultas[0], 10))

    def test_insertar_y_eliminar(self):
        # con nprobe = nlist el IVF es exacto: tras los cambios debe coincidir con la fuerza bruta
        indice = IndiceVectorial("IVF", "euclidean", nlist=30, nprobe=30).construir(
            self.vectores[:2500], self.ids[:2500])
        indice.insertar(self.vectores[2500:], self.ids[2500:])
        self.assertEqual(indice.cantidad(), 3000)
        self.assertEqual(indice.eliminar(self.ids[:100]), 100)
        self.assertEqual(indice.eliminar(self.ids[:100]), 0)  # ya no estan
        self.assertEqual(indice.cantidad(), 2900)
        for consulta in self.consultas[:5]:
            esperado = fuerza_bruta(self.vectores[100:], self.ids[100:], consulta, 10, "euclidean")
            self.assertEqual([i for i, _ in indice.knn(consulta, 10)], [i for i, _ in esperado])

    def test_errores(self):
        with self.assertRaises(ValueError):
            IndiceVectorial("IVF", "manhattan")
        with self.assertRaises(ValueError):
            IndiceVectorial("IVF", "cosine", M=16)
        with self.assertRaises(ValueError):
            IndiceVectorial("IVF", "cosine", nlist=100).construir(self.vectores[:50], self.ids[:50])
        with self.assertRaises(RuntimeError):
            IndiceVectorial("IVF", "cosine").knn(self.consultas[0], 10)


class PruebasHNSW(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # las consultas salen de los mismos grupos pero no estan en el indice, como una imagen nueva
        todos = datos_agrupados(10_020)
        cls.vectores = todos[:10_000]
        cls.ids = np.arange(1, 10_001) * 10
        cls.consultas = todos[10_000:]
        cls.chico, cls.ids_chico = cls.vectores[:3000], cls.ids[:3000]

    def construir(self, metrica="cosine", **parametros):
        return IndiceVectorial("HNSW", metrica, **parametros).construir(self.chico, self.ids_chico)

    def test_recall_con_ef_100(self):
        for metrica in METRICAS:
            with self.subTest(metrica=metrica):
                indice = IndiceVectorial("HNSW", metrica, ef_busqueda=100).construir(self.vectores, self.ids)
                valores = [recall(indice.knn(c, 10), fuerza_bruta(self.vectores, self.ids, c, 10, metrica))
                           for c in self.consultas]
                print(f"\n  Recall@10 con ef_busqueda=100 en 10k ({metrica}): {np.mean(valores):.2f}", end="")
                self.assertGreaterEqual(np.mean(valores), 0.95)

    def test_distancias_de_menor_a_mayor(self):
        for metrica in METRICAS:
            with self.subTest(metrica=metrica):
                distancias = [d for _, d in self.construir(metrica).knn(self.consultas[0], 10)]
                self.assertEqual(len(distancias), 10)
                self.assertEqual(distancias, sorted(distancias))

    def test_un_id_borrado_no_vuelve(self):
        indice = self.construir("euclidean", ef_busqueda=64)
        borrados = {indice.knn(c, 1)[0][0] for c in self.consultas}  # el mas cercano de cada consulta
        self.assertEqual(indice.eliminar(list(borrados)), len(borrados))
        self.assertEqual(indice.eliminar(list(borrados)), 0)
        self.assertEqual(indice.cantidad(), 3000 - len(borrados))
        vivos = ~np.isin(self.ids_chico, list(borrados))
        for consulta in self.consultas:
            resultado = indice.knn(consulta, 10)
            self.assertEqual(len(resultado), 10)
            self.assertFalse(borrados & {i for i, _ in resultado})
            esperado = fuerza_bruta(self.chico[vivos], self.ids_chico[vivos], consulta, 10, "euclidean")
            self.assertGreaterEqual(recall(resultado, esperado), 0.9)

    def test_borrar_y_reinsertar_la_misma_pk(self):
        indice = self.construir("euclidean")
        pk, nuevo = int(self.ids_chico[0]), self.chico[1500] + 0.01  # el vector nuevo esta en otro lugar
        indice.eliminar([pk])
        indice.insertar(nuevo, [pk])
        self.assertEqual(indice.cantidad(), 3000)
        # el vector viejo ya no aparece: la pk no esta entre los vecinos de su posicion anterior
        self.assertNotIn(pk, [i for i, _ in indice.knn(self.chico[0], 10)])
        cercanos = indice.knn(nuevo, 10)
        self.assertEqual(cercanos[0][0], pk)
        self.assertAlmostEqual(cercanos[0][1], 0, places=3)
        self.assertEqual([i for i, _ in cercanos].count(pk), 1)
        with self.assertRaises(ValueError):
            indice.insertar(nuevo, [pk])  # ya esta vivo

    def test_insertar_sin_reconstruir(self):
        indice = IndiceVectorial("HNSW", "cosine", ef_busqueda=64).construir(self.chico[:2000], self.ids_chico[:2000])
        indice.insertar(self.chico[2000:], self.ids_chico[2000:])
        self.assertEqual((indice.cantidad(), indice.indice.ntotal, indice.reconstrucciones), (3000, 3000, 0))
        valores = [recall(indice.knn(c, 10), fuerza_bruta(self.chico, self.ids_chico, c, 10, "cosine"))
                   for c in self.consultas]
        self.assertGreaterEqual(np.mean(valores), 0.95)

    def test_umbral_compacta_el_indice(self):
        indice = self.construir()
        indice.eliminar(self.ids_chico[:900])  # 30 %: todavia no pasa el umbral
        self.assertEqual(indice.reconstrucciones, 0)
        self.assertAlmostEqual(indice.fraccion_borrada(), 0.30)
        self.assertEqual(indice.indice.ntotal, 3000)
        indice.eliminar(self.ids_chico[900:901])
        self.assertEqual(indice.reconstrucciones, 1)
        self.assertEqual(indice.fraccion_borrada(), 0)
        self.assertEqual((indice.cantidad(), indice.indice.ntotal), (2099, 2099))
        np.testing.assert_array_equal(indice.pks, self.ids_chico[901:])
        for consulta in self.consultas:
            resultado = indice.knn(consulta, 10)
            esperado = fuerza_bruta(self.chico[901:], self.ids_chico[901:], consulta, 10, "cosine")
            self.assertGreaterEqual(recall(resultado, esperado), 0.9)
        # despues de compactar se sigue borrando por pk
        self.assertEqual(indice.eliminar([self.ids_chico[901]]), 1)
        self.assertNotIn(int(self.ids_chico[901]), [i for i, _ in indice.knn(self.chico[901], 10)])

    def test_borrar_todo(self):
        indice = self.construir()
        self.assertEqual(indice.eliminar(self.ids_chico), 3000)
        self.assertEqual((indice.cantidad(), indice.knn(self.consultas[0], 10)), (0, []))
        indice.insertar(self.chico[:5], self.ids_chico[:5])
        self.assertEqual(len(indice.knn(self.consultas[0], 10)), 5)

    def test_guardar_y_cargar(self):
        indice = self.construir(M=24, ef_busqueda=64)
        indice.eliminar(self.ids_chico[:300])
        with tempfile.TemporaryDirectory() as carpeta:
            ruta = os.path.join(carpeta, "hnsw.index")
            indice.guardar(ruta)
            self.assertEqual(indice.tamano_en_disco(), os.path.getsize(ruta))
            cargado = IndiceVectorial("HNSW", "cosine", ef_busqueda=64).cargar(ruta)
            self.assertEqual(cargado.parametros["M"], 24)  # sale del archivo, no del constructor
            self.assertEqual(cargado.cantidad(), 2700)
            for consulta in self.consultas[:5]:
                self.assertEqual(cargado.knn(consulta, 10), indice.knn(consulta, 10))
            # los borrados siguen borrados y se puede seguir modificando
            self.assertEqual(cargado.eliminar(self.ids_chico[:300]), 0)
            self.assertEqual(cargado.eliminar(self.ids_chico[300:301]), 1)
            with self.assertRaises(ValueError):
                IndiceVectorial("HNSW", "euclidean").cargar(ruta)

    def test_errores(self):
        with self.assertRaises(ValueError):
            IndiceVectorial("HNSW", "cosine", nlist=10)
        for umbral in (0, 1.5):
            with self.assertRaises(ValueError):
                IndiceVectorial("HNSW", "cosine", umbral_reconstruccion=umbral)
        with self.assertRaises(ValueError):
            IndiceVectorial("HNSW", "cosine").construir(self.chico[:3], [1, 2, 2])
        with self.assertRaises(ValueError):
            self.construir().insertar(np.ones((1, 8), dtype=np.float32), [1])  # otra dimension
        with self.assertRaises(RuntimeError):
            IndiceVectorial("HNSW", "cosine").insertar(self.chico[:1], [1])
        with self.assertRaises(RuntimeError):
            IndiceVectorial("HNSW", "cosine").eliminar([1])


if __name__ == "__main__":
    unittest.main()
