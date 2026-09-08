import unittest
from collections import Counter

from motor.indices import ExtendibleHashing


class ExtendibleHashingTest(unittest.TestCase):
    def test_inserta_busca_y_rechaza_duplicados(self):
        indice = ExtendibleHashing[int, str](bucket_capacity=2)

        self.assertTrue(indice.insertar(10, "diez"))
        self.assertTrue(indice.insertar(20, "veinte"))
        self.assertFalse(indice.insertar(10, "otro valor"))
        self.assertEqual(indice.buscar(10), "diez")
        self.assertIsNone(indice.buscar(99))
        self.assertEqual(len(indice), 2)

    def test_divide_buckets_y_conserva_todos_los_registros(self):
        indice = ExtendibleHashing[int, str](bucket_capacity=2)

        for clave in range(8):
            self.assertTrue(indice.insertar(clave, str(clave)))
            self._assert_invariantes(indice)

        self.assertGreater(indice.profundidad_global, 0)
        self.assertGreater(indice.cantidad_buckets, 1)
        self.assertEqual(dict(indice.items()), {clave: str(clave) for clave in range(8)})

    def test_no_soporta_predicados_de_rango(self):
        indice = ExtendibleHashing[int, str]()

        self.assertFalse(indice.supportsRange())
        self.assertFalse(indice.supports_range())

    def test_maneja_colisiones_con_hash_controlado(self):
        indice = ExtendibleHashing[int, str](
            bucket_capacity=2,
            hash_function=lambda clave: clave % 4,
        )

        indice.insertar(1, "uno")
        indice.insertar(5, "cinco")
        with self.assertRaises(OverflowError):
            indice.insertar(9, "nueve")
        self.assertEqual(len(indice), 2)
        self.assertEqual(indice.buscar(1), "uno")
        self.assertEqual(indice.buscar(5), "cinco")

    def test_elimina_y_fusiona_buckets(self):
        indice = ExtendibleHashing[int, str](bucket_capacity=2)
        for clave in range(4):
            indice.insertar(clave, str(clave))
        profundidad_con_datos = indice.profundidad_global

        for clave in range(4):
            self.assertTrue(indice.eliminar(clave))
            self._assert_invariantes(indice)

        self.assertEqual(len(indice), 0)
        self.assertEqual(indice.profundidad_global, 0)
        self.assertEqual(indice.cantidad_buckets, 1)
        self.assertFalse(indice.eliminar(100))
        self.assertGreaterEqual(profundidad_con_datos, 1)

    def test_crece_con_100000_claves(self):
        indice = ExtendibleHashing[int, int](bucket_capacity=64)

        for clave in range(100_000):
            self.assertTrue(indice.insertar(clave, clave))

        self.assertEqual(len(indice), 100_000)
        self.assertGreater(indice.profundidad_global, 0)
        self.assertEqual(len(indice._directorio), 1 << indice.profundidad_global)
        self.assertEqual(indice.buscar(0), 0)
        self.assertEqual(indice.buscar(50_000), 50_000)
        self.assertEqual(indice.buscar(99_999), 99_999)
        self.assertIsNone(indice.buscar(100_000))

    def test_valida_parametros_y_expone_estadisticas(self):
        with self.assertRaises(ValueError):
            ExtendibleHashing(bucket_capacity=0)
        with self.assertRaises(ValueError):
            ExtendibleHashing(max_depth=-1)

        indice = ExtendibleHashing[int, str](bucket_capacity=2)
        indice.insertar(1, "uno")
        estadisticas = indice.estadisticas()
        self.assertEqual(estadisticas["registros"], 1)
        self.assertEqual(estadisticas["capacidad_bucket"], 2)

    @staticmethod
    def _assert_invariantes(indice):
        """Comprueba la estructura interna después de cada split."""
        profundidad = indice.profundidad_global
        directorio = indice._directorio
        assert len(directorio) == 1 << profundidad

        referencias = Counter(id(bucket) for bucket in directorio)
        for bucket in indice._buckets_unicos():
            assert 0 <= bucket.profundidad_local <= profundidad
            assert referencias[id(bucket)] == 1 << (
                profundidad - bucket.profundidad_local
            )

        for clave, valor in indice.items():
            bucket = indice._bucket_para(clave)
            assert bucket.registros[clave] == valor


if __name__ == "__main__":
    unittest.main()