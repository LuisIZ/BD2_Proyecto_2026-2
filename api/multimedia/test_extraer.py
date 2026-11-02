"""Pruebas SIFT determinista y de invariancia aproximada a escala/rotación."""

import csv
import io
import os
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import cv2
import faiss
import numpy as np

from multimedia.extraer import (
    extract_features,
    extraer_descriptores,
    extraer_imagenes_tar,
    procesar_csv,
)


def imagen_patron(seed):
    rng = np.random.default_rng(seed)
    imagen = np.zeros((256, 256), dtype=np.uint8)
    for _ in range(60):
        x, y = rng.integers(10, 246, size=2)
        radio = int(rng.integers(3, 15))
        tono = int(rng.integers(80, 256))
        cv2.circle(imagen, (int(x), int(y)), radio, tono, thickness=int(rng.integers(1, 4)))
        x2, y2 = rng.integers(0, 256, size=2)
        cv2.line(imagen, (int(x), int(y)), (int(x2), int(y2)), tono, 1)
    return imagen


class PruebasExtraccion(unittest.TestCase):
    def test_sift_128_y_vector_determinista_con_imagen_transformada(self):
        with tempfile.TemporaryDirectory() as carpeta:
            original = imagen_patron(5)
            escalada = cv2.resize(original, (320, 320), interpolation=cv2.INTER_CUBIC)
            matriz = cv2.getRotationMatrix2D((160, 160), 23, 1.0)
            rotada = cv2.warpAffine(escalada, matriz, (320, 320))
            distractora = np.zeros((256, 256), dtype=np.uint8)
            rutas = [os.path.join(carpeta, nombre) for nombre in ("original.png", "rotada.png", "otra.png")]
            for ruta, imagen in zip(rutas, (original, rotada, distractora)):
                self.assertTrue(cv2.imwrite(ruta, imagen))

            descriptores = [extraer_descriptores(ruta) for ruta in rutas]
            self.assertTrue(all(d.ndim == 2 and d.shape[1] == 128 for d in descriptores))
            muestra = np.ascontiguousarray(np.vstack(descriptores), dtype=np.float32)
            kmeans = faiss.Kmeans(128, 16, niter=10, seed=2026, verbose=False)
            kmeans.train(muestra)
            ruta_vocabulario = os.path.join(carpeta, "vocabulario.npy")
            ruta_idf = os.path.join(carpeta, "idf.npy")
            np.save(ruta_vocabulario, kmeans.centroids)
            np.save(ruta_idf, np.ones(16, dtype=np.float32))

            with patch.dict(os.environ, {
                "BD2_VOCABULARIO": ruta_vocabulario,
                "BD2_IDF_VISUAL": ruta_idf,
            }):
                vector_original = extract_features(rutas[0])
                repetido = extract_features(rutas[0])
                vector_rotado = extract_features(rutas[1])
                vector_otra = extract_features(rutas[2])

            self.assertEqual(vector_original.shape, (16,))
            np.testing.assert_array_equal(vector_original, repetido)
            self.assertAlmostEqual(float(np.linalg.norm(vector_original)), 1.0, places=5)
            d_original = np.linalg.norm(vector_rotado - vector_original)
            d_otra = np.linalg.norm(vector_rotado - vector_otra)
            self.assertLess(d_original, d_otra)

    def test_extrae_del_tar_solo_rutas_del_csv(self):
        with tempfile.TemporaryDirectory() as carpeta:
            ruta_csv = os.path.join(carpeta, "productos.csv")
            ruta_tar = os.path.join(carpeta, "abo.tar")
            destino = os.path.join(carpeta, "imagenes")
            with open(ruta_csv, "w", encoding="utf-8", newline="") as archivo:
                escritor = csv.writer(archivo)
                escritor.writerow(["id", "imagen"])
                escritor.writerow([1, "ab/foto.jpg"])
            with tarfile.open(ruta_tar, "w") as tar:
                for nombre, contenido in (("images/small/xx/foto.jpg", b"foto"),
                                          ("images/small/yy/otra.jpg", b"otra")):
                    info = tarfile.TarInfo(nombre)
                    info.size = len(contenido)
                    tar.addfile(info, io.BytesIO(contenido))
            extraidas, faltantes = extraer_imagenes_tar(ruta_csv, ruta_tar, destino)
            self.assertEqual(extraidas, 1)
            self.assertEqual(faltantes, [])
            with open(os.path.join(destino, "ab", "foto.jpg"), "rb") as imagen:
                self.assertEqual(imagen.read(), b"foto")

    def test_pipeline_entrena_vocabulario_exporta_csv_y_cache(self):
        with tempfile.TemporaryDirectory() as carpeta:
            raiz_imagenes = os.path.join(carpeta, "imagenes")
            os.makedirs(raiz_imagenes)
            filas = []
            for i, imagen in enumerate((imagen_patron(5), imagen_patron(6), imagen_patron(7)), 1):
                nombre = f"img_{i}.png"
                self.assertTrue(cv2.imwrite(os.path.join(raiz_imagenes, nombre), imagen))
                filas.append({"id": i, "imagen": nombre})
            ruta_csv = os.path.join(carpeta, "productos.csv")
            with open(ruta_csv, "w", encoding="utf-8", newline="") as archivo:
                escritor = csv.DictWriter(archivo, fieldnames=("id", "imagen"))
                escritor.writeheader()
                escritor.writerows(filas)
            vocabulario = os.path.join(carpeta, "vocabulario_16.npy")
            idf = os.path.join(carpeta, "idf_16.npy")
            prefijo = os.path.join(carpeta, "salida", "vectores_3")
            ids, vectores, _ = procesar_csv(
                ruta_csv,
                raiz_imagenes,
                k=16,
                procesos=1,
                vocabulario_path=vocabulario,
                cache_dir=os.path.join(carpeta, "cache"),
                salida=prefijo,
                muestra_descriptores=200,
                idf_path=idf,
            )
            self.assertEqual(ids.tolist(), [1, 2, 3])
            self.assertEqual(vectores.shape, (3, 16))
            self.assertTrue(os.path.isfile(prefijo + ".npy"))
            self.assertTrue(os.path.isfile(prefijo + ".csv"))
            self.assertTrue(os.path.isfile(idf))
            self.assertEqual(len(os.listdir(os.path.join(carpeta, "cache"))), 3)
            with patch.dict(os.environ, {"BD2_VOCABULARIO": vocabulario, "BD2_IDF_VISUAL": idf}):
                np.testing.assert_allclose(extract_features(os.path.join(raiz_imagenes, "img_1.png")), vectores[0])


if __name__ == "__main__":
    unittest.main()
