"""Pruebas de escala 1k/10k para el benchmark FAISS."""

import unittest

from multimedia.bench_vectorial import benchmark_dataset, vectores_sinteticos


class PruebasBenchmarkVectorial(unittest.TestCase):
    def test_indices_en_1k_y_10k(self):
        for n, metrica in ((1_000, "euclidean"), (10_000, "cosine")):
            with self.subTest(n=n, metrica=metrica):
                vectores = vectores_sinteticos(n, dimension=32, seed=n)
                filas = benchmark_dataset(vectores, metrica=metrica, consultas=3, seed=n)
                exacto = [fila for fila in filas if fila["indice"] == "secuencial_exact"]
                self.assertEqual(len(exacto), 1)
                self.assertEqual(exacto[0]["recall_at_10"], 1.0)
                self.assertTrue(any(fila["indice"] == "IVF" for fila in filas))
                self.assertTrue(any(fila["indice"] == "HNSW" for fila in filas))
                self.assertTrue(all(fila["disco_index_bytes"] > 0 for fila in filas))


if __name__ == "__main__":
    unittest.main()
