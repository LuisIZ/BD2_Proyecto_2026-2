import csv
import os
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from multimedia.extraer_audio import extract_features_audio
from multimedia.segmentos_audio import inicios_de_tramos, procesar_tramos
from multimedia.test_extraer_audio import FRECUENCIA, escribir_wav, ruido, tono


def leer_csv(ruta):
    with open(ruta, encoding="utf-8", newline="") as archivo:
        return list(csv.DictReader(archivo))


def vector_de_texto(texto):
    return np.array([float(x) for x in texto[len("VECTOR("):-1].split(",")], dtype=np.float32)


class PruebasSegmentos(unittest.TestCase):
    def test_inicios_de_los_tramos(self):
        self.assertEqual(inicios_de_tramos(30.0, 10, 5), [0, 5, 10, 15, 20])
        self.assertEqual(inicios_de_tramos(29.98, 10, 5), [0, 5, 10, 15, 20])
        self.assertEqual(inicios_de_tramos(22.0, 10, 5), [0, 5, 10])
        self.assertEqual(inicios_de_tramos(30.0, 10, 10), [0, 10, 20])
        self.assertEqual(inicios_de_tramos(7.0, 10, 5), [0])
        with self.assertRaises(ValueError):
            inicios_de_tramos(30.0, 10, 0)

    def test_tramos_de_dos_audios_y_mismo_vector_que_el_recorte(self):
        with tempfile.TemporaryDirectory() as carpeta:
            largo = np.concatenate([tono(200, 10), tono(1000, 10), ruido(10, 1)])
            escribir_wav(os.path.join(carpeta, "largo.wav"), largo)
            escribir_wav(os.path.join(carpeta, "corto.wav"), tono(500, 22))
            with open(os.path.join(carpeta, "canciones.csv"), "w", encoding="utf-8", newline="") as archivo:
                archivo.write("id,audio,genero\n1,largo.wav,rock\n2,corto.wav,jazz\n")

            vocabulario = os.path.join(carpeta, "vocabulario.npy")
            idf = os.path.join(carpeta, "idf.npy")
            np.save(vocabulario, np.random.default_rng(3).normal(0, 50, (8, 13)).astype(np.float32))
            np.save(idf, np.linspace(1.0, 2.0, 8).astype(np.float32))
            salida = os.path.join(carpeta, "salida")
            with patch.dict(os.environ, {"BD2_VOCABULARIO_AUDIO": vocabulario, "BD2_IDF_AUDIO": idf}):
                total, _ = procesar_tramos(os.path.join(carpeta, "canciones.csv"), carpeta, hop=5, duracion=10,
                                           salida=salida, procesos=1)

                # 30 s dan 5 tramos y 22 s dan 3
                self.assertEqual(total, 8)
                canciones = leer_csv(os.path.join(salida, "canciones.csv"))
                self.assertEqual([(c["id"], c["archivo"], c["genero"]) for c in canciones],
                                 [("1", "largo.wav", "rock"), ("2", "corto.wav", "jazz")])
                tramos = leer_csv(os.path.join(salida, "segmentos.csv"))
                self.assertEqual(list(tramos[0]), ["id", "cancion_id", "inicio", "fin", "v"])
                self.assertEqual([(t["id"], t["cancion_id"], float(t["inicio"]), float(t["fin"])) for t in tramos], [
                    ("1", "1", 0, 10), ("2", "1", 5, 15), ("3", "1", 10, 20), ("4", "1", 15, 25), ("5", "1", 20, 30),
                    ("6", "2", 0, 10), ("7", "2", 5, 15), ("8", "2", 10, 20),
                ])
                for t in tramos:
                    self.assertEqual(len(vector_de_texto(t["v"])), 8)
                    self.assertAlmostEqual(float(np.linalg.norm(vector_de_texto(t["v"]))), 1.0, places=5)

                # el tramo de 5 a 15 s da el mismo vector que ese recorte guardado aparte
                recorte = os.path.join(carpeta, "recorte.wav")
                escribir_wav(recorte, largo[5 * FRECUENCIA:15 * FRECUENCIA])
                np.testing.assert_allclose(vector_de_texto(tramos[1]["v"]), extract_features_audio(recorte), atol=1e-6)
                self.assertFalse(np.allclose(vector_de_texto(tramos[0]["v"]), vector_de_texto(tramos[4]["v"])))

    def test_csv_sin_genero_falla(self):
        with tempfile.TemporaryDirectory() as carpeta:
            ruta = os.path.join(carpeta, "malo.csv")
            with open(ruta, "w", encoding="utf-8", newline="") as archivo:
                archivo.write("id,audio\n1,a.wav\n")
            with self.assertRaises(ValueError):
                procesar_tramos(ruta, carpeta, procesos=1)


if __name__ == "__main__":
    unittest.main()
