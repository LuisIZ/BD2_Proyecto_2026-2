import csv
import tempfile
import unittest
from pathlib import Path

from motor.indices import ExtendibleHashing, cerrar_log


CSV_PATH = Path(__file__).parents[2] / "datos" / "organizations-100000.csv"


class ExtendibleHashCsvTest(unittest.TestCase):
    def test_construccion_consulta_y_actualizaciones_con_csv_real(self):
        def registros_csv():
            with CSV_PATH.open(encoding="utf-8", newline="") as archivo:
                for fila in csv.DictReader(archivo):
                    yield int(fila["Index"]), fila

        indice = ExtendibleHashing.construir(registros_csv(), bucket_capacity=128)

        self.assertEqual(indice.cantidad, 100_000)
        for clave in (1, 50_000, 100_000):
            registro = indice.buscar(clave)
            self.assertIsNotNone(registro)
            self.assertEqual(int(registro["Index"]), clave)
        self.assertIsNone(indice.buscar(100_001))

        claves_actualizadas = list(range(1, 1_001))
        valores = {clave: indice.buscar(clave) for clave in claves_actualizadas}
        for clave in claves_actualizadas:
            self.assertTrue(indice.eliminar(clave))
        for clave, registro in valores.items():
            self.assertTrue(indice.insertar(clave, registro))

        metricas = indice.metricas()
        self.assertGreater(metricas["construccion_ms"], 0)
        self.assertGreaterEqual(metricas["consultas"], 1_004)
        self.assertEqual(metricas["eliminaciones"], 1_000)
        self.assertEqual(metricas["inserciones"], 101_000)
        self.assertGreater(metricas["espacio_adicional_bytes"], 0)
        self.assertGreater(metricas["consulta_promedio_us"], 0)
        self.assertGreater(metricas["insercion_promedio_us"], 0)
        self.assertGreater(metricas["eliminacion_promedio_us"], 0)
        self.assertEqual(indice.cantidad, 100_000)

    def test_guarda_metricas_en_archivo_log(self):
        with tempfile.TemporaryDirectory() as directorio:
            ruta_log = Path(directorio) / "historial.log"
            try:
                indice = ExtendibleHashing(bucket_capacity=2, log_path=ruta_log)
                indice.insertar(1, "uno")
                indice.buscar(1)
                indice.metricas()

                contenido = ruta_log.read_text(encoding="utf-8")
                self.assertIn("Metricas del indice hash", contenido)
            finally:
                cerrar_log(ruta_log)


if __name__ == "__main__":
    unittest.main()