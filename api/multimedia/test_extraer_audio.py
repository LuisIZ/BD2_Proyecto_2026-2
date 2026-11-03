import csv
import os
import tempfile
import unittest
import wave
from unittest.mock import patch

import numpy as np

from multimedia.extraer_audio import (
    extract_features_audio,
    extraer_descriptores_audio,
    procesar_audios,
)

FRECUENCIA = 22050


def escribir_wav(ruta, senal):
    datos = (np.clip(senal, -1.0, 1.0) * 32767).astype("<i2")
    with wave.open(ruta, "wb") as archivo:
        archivo.setnchannels(1)
        archivo.setsampwidth(2)
        archivo.setframerate(FRECUENCIA)
        archivo.writeframes(datos.tobytes())


def tono(frecuencia, segundos, amplitud=0.5):
    t = np.arange(int(FRECUENCIA * segundos)) / FRECUENCIA
    return amplitud * np.sin(2 * np.pi * frecuencia * t)


def ruido(segundos, semilla):
    return 0.3 * np.random.default_rng(semilla).standard_normal(int(FRECUENCIA * segundos))


class PruebasAudio(unittest.TestCase):
    def test_un_mfcc_por_ventana_con_13_o_20_coeficientes(self):
        with tempfile.TemporaryDirectory() as carpeta:
            ruta = os.path.join(carpeta, "tono.wav")
            escribir_wav(ruta, tono(440, 2.0))
            d13 = extraer_descriptores_audio(ruta)
            d20 = extraer_descriptores_audio(ruta, n_mfcc=20)
            self.assertEqual(d13.dtype, np.float32)
            self.assertEqual(d13.shape[1], 13)
            self.assertEqual(d20.shape[1], 20)
            self.assertEqual(d13.shape[0], d20.shape[0])
            # 2 s a 22 050 Hz con salto de 512: una ventana cada 23 ms
            self.assertAlmostEqual(d13.shape[0], 2 * 22050 / 512, delta=3)
            np.testing.assert_array_equal(d13, extraer_descriptores_audio(ruta))
            self.assertEqual(extraer_descriptores_audio(ruta, max_descriptores=10).shape, (10, 13))

    def test_pipeline_vectores_y_cercania_entre_audios_parecidos(self):
        with tempfile.TemporaryDirectory() as carpeta:
            audios = {
                "grave.wav": tono(200, 3.0),
                "grave_corto.wav": tono(200, 2.0),
                "agudo.wav": tono(3000, 3.0),
                "ruido.wav": ruido(3.0, 1),
            }
            filas = []
            for i, (nombre, senal) in enumerate(audios.items(), 1):
                escribir_wav(os.path.join(carpeta, nombre), senal)
                filas.append({"id": i, "audio": nombre})
            ruta_csv = os.path.join(carpeta, "audios.csv")
            with open(ruta_csv, "w", encoding="utf-8", newline="") as archivo:
                escritor = csv.DictWriter(archivo, fieldnames=("id", "audio"))
                escritor.writeheader()
                escritor.writerows(filas)

            vocabulario = os.path.join(carpeta, "vocabulario_audio_8.npy")
            idf = os.path.join(carpeta, "idf_audio_8.npy")
            prefijo = os.path.join(carpeta, "salida", "vectores_audio_4")
            ids, vectores, _ = procesar_audios(
                ruta_csv, carpeta, k=8, procesos=1, vocabulario_path=vocabulario, idf_path=idf,
                cache_dir=os.path.join(carpeta, "cache"), salida=prefijo, muestra_descriptores=2000,
            )
            self.assertEqual(ids.tolist(), [1, 2, 3, 4])
            self.assertEqual(vectores.shape, (4, 8))
            for vector in vectores:
                self.assertAlmostEqual(float(np.linalg.norm(vector)), 1.0, places=5)
            self.assertEqual(len(os.listdir(os.path.join(carpeta, "cache"))), 4)
            with open(prefijo + ".csv", encoding="utf-8") as archivo:
                lineas = archivo.read().splitlines()
            self.assertEqual(lineas[0], "id,vector")
            self.assertTrue(lineas[1].startswith('1,"VECTOR(') and len(lineas) == 5)

            # el mismo tono con otra duracion queda mas cerca que un sonido distinto
            grave, corto, agudo, sucio = vectores
            self.assertLess(np.linalg.norm(corto - grave), np.linalg.norm(corto - agudo))
            self.assertLess(np.linalg.norm(corto - grave), np.linalg.norm(corto - sucio))

            with patch.dict(os.environ, {"BD2_VOCABULARIO_AUDIO": vocabulario, "BD2_IDF_AUDIO": idf}):
                np.testing.assert_allclose(extract_features_audio(os.path.join(carpeta, "grave.wav")), grave, atol=1e-6)

    def test_csv_sin_audio_o_con_archivos_faltantes_falla(self):
        with tempfile.TemporaryDirectory() as carpeta:
            sin_columna = os.path.join(carpeta, "malo.csv")
            with open(sin_columna, "w", encoding="utf-8", newline="") as archivo:
                archivo.write("id,imagen\n1,a.jpg\n")
            with self.assertRaises(ValueError):
                procesar_audios(sin_columna, carpeta, k=4, procesos=1)
            faltante = os.path.join(carpeta, "falta.csv")
            with open(faltante, "w", encoding="utf-8", newline="") as archivo:
                archivo.write("id,audio\n1,no_existe.wav\n")
            with self.assertRaises(FileNotFoundError):
                procesar_audios(faltante, carpeta, k=4, procesos=1)


if __name__ == "__main__":
    unittest.main()
