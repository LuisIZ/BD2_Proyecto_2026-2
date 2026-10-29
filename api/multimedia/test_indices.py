"""Pruebas del indice IVF contra la fuerza bruta.

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

    def test_errores(self):
        with self.assertRaises(ValueError):
            IndiceVectorial("IVF", "manhattan")
        with self.assertRaises(ValueError):
            IndiceVectorial("IVF", "cosine", M=16)
        with self.assertRaises(ValueError):
            IndiceVectorial("IVF", "cosine", nlist=100).construir(self.vectores[:50], self.ids[:50])
        with self.assertRaises(RuntimeError):
            IndiceVectorial("IVF", "cosine").knn(self.consultas[0], 10)


if __name__ == "__main__":
    unittest.main()
