import asyncio
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np
from fastapi import HTTPException

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

from api import copyright, web_api  # noqa: E402
from api.motor_cli import MotorSQL  # noqa: E402
from multimedia.segmentos_audio import procesar_tramos  # noqa: E402
from multimedia.test_extraer_audio import FRECUENCIA, escribir_wav  # noqa: E402


def melodia(notas, segundos, semilla):
    # una nota distinta cada medio segundo, elegida al azar entre las dadas
    rng = np.random.default_rng(semilla)
    paso = FRECUENCIA // 2
    t = np.arange(paso) / FRECUENCIA
    partes = [0.4 * np.sin(2 * np.pi * rng.choice(notas) * t) for _ in range(int(segundos * 2))]
    return np.concatenate(partes) + 0.01 * rng.standard_normal(paso * len(partes))


CANCIONES = [
    ("graves.wav", "a", melodia([110, 131, 147, 165, 196], 30, 1)),
    ("medios.wav", "b", melodia([440, 523, 587, 659, 784], 30, 2)),
    ("ruido.wav", "c", 0.3 * np.random.default_rng(3).standard_normal(30 * FRECUENCIA)),
]


UMBRAL_PRUEBA = 90.0


def coincidencia(cancion_id, similitud):
    return {"cancion_id": cancion_id, "archivo": f"c{cancion_id}.wav", "genero": "g",
            "similitud": similitud, "supera_umbral": similitud >= UMBRAL_PRUEBA}


class PruebaAgrupar(unittest.TestCase):
    def test_hacen_falta_dos_tramos(self):
        tramos = [
            {"coincidencias": [coincidencia(1, 95.0), coincidencia(2, 93.0)]},
            {"coincidencias": [coincidencia(1, 97.0), coincidencia(1, 91.0)]},
            {"coincidencias": [coincidencia(3, 60.0)]},
        ]
        canciones, veredicto = copyright.agrupar(tramos)
        # la 1 aparece en 2 de 3 tramos (repetida en el segundo cuenta una vez); la 2 en uno solo
        self.assertEqual(veredicto, "posible_copyright")
        self.assertEqual([c["cancion_id"] for c in canciones], [1])
        self.assertEqual(canciones[0]["tramos"], 2)
        self.assertEqual(canciones[0]["porcentaje"], 66.7)
        self.assertEqual(canciones[0]["similitud_maxima"], 97.0)

    def test_sin_coincidencias_bajo_el_umbral(self):
        tramos = [{"coincidencias": [coincidencia(1, 80.0)]}, {"coincidencias": [coincidencia(1, 85.0)]}]
        self.assertEqual(copyright.agrupar(tramos), ([], "sin_coincidencias"))
        self.assertEqual(copyright.agrupar([]), ([], "sin_coincidencias"))

    def test_un_solo_tramo_basta_si_el_audio_tiene_uno(self):
        canciones, veredicto = copyright.agrupar([{"coincidencias": [coincidencia(4, 99.0)]}])
        self.assertEqual(veredicto, "posible_copyright")
        self.assertEqual(canciones[0]["porcentaje"], 100.0)

    def test_orden_por_tramos_y_similitud(self):
        tramos = [{"coincidencias": [coincidencia(1, 92.0), coincidencia(2, 99.0), coincidencia(3, 92.0)]}] * 2
        canciones, _ = copyright.agrupar(tramos)
        self.assertEqual([c["cancion_id"] for c in canciones], [2, 1, 3])

    def test_lotes_no_pasan_el_limite(self):
        consultas = [("x" * 30) + ";" for _ in range(10)]
        lotes = list(copyright.lotes(consultas, limite=100))
        self.assertTrue(all(len(lote) <= 100 for lote in lotes))
        self.assertEqual("\n".join(lotes).split("\n"), consultas)
        self.assertEqual(len(lotes), 4)


class PruebaAnalizar(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = Path(tempfile.mkdtemp(dir=RAIZ / ".build"))
        audios = cls.dir / "audios"
        audios.mkdir()
        with open(cls.dir / "canciones.csv", "w", encoding="utf-8") as archivo:
            archivo.write("id,audio,genero\n")
            for i, (nombre, genero, senal) in enumerate(CANCIONES, 1):
                escribir_wav(os.fspath(audios / nombre), senal)
                archivo.write(f"{i},{nombre},{genero}\n")
        datos = cls.dir / "datos"
        cls.entorno = patch.dict(os.environ, {
            "BD2_VOCABULARIO_COPYRIGHT": os.fspath(datos / "vocabulario_copyright_8.npy"),
            "BD2_IDF_COPYRIGHT": os.fspath(datos / "idf_copyright_8.npy"),
        })
        cls.entorno.start()
        procesar_tramos(cls.dir / "canciones.csv", audios, salida=datos, procesos=1, k=8)

        cls.anterior = (web_api.motor, web_api.indices_faiss, web_api.SUBIDAS)
        web_api.motor = MotorSQL(db=os.path.relpath(cls.dir / "db", RAIZ))
        web_api.indices_faiss = web_api.IndicesFaiss()
        web_api.SUBIDAS = cls.dir / "subidas"
        sql = [
            "CREATE TABLE canciones (id INT PRIMARY KEY, archivo VARCHAR(40), genero VARCHAR(12))",
            f"COPY canciones FROM FILE '{os.path.relpath(datos / 'canciones.csv', RAIZ).replace(os.sep, '/')}'",
            "CREATE TABLE segmentos (id INT PRIMARY KEY, cancion_id INT, inicio FLOAT, fin FLOAT, v VECTOR(8))",
            f"COPY segmentos FROM FILE '{os.path.relpath(datos / 'segmentos.csv', RAIZ).replace(os.sep, '/')}'",
            "CREATE INDEX segmentos_hnsw ON segmentos (v) USING HNSW",
        ]
        for r in web_api.consultas(web_api.Consulta(sql=";\n".join(sql) + ";"))["resultados"]:
            assert r["ok"], r.get("error")

    @classmethod
    def tearDownClass(cls):
        web_api.motor, web_api.indices_faiss, web_api.SUBIDAS = cls.anterior
        cls.entorno.stop()
        shutil.rmtree(cls.dir, ignore_errors=True)

    def analizar(self, senal, nombre="consulta.wav"):
        ruta = self.dir / nombre
        escribir_wav(os.fspath(ruta), senal)
        return copyright.analizar_audio(ruta, UMBRAL_PRUEBA)

    def test_recorte_mas_bajo_es_posible_copyright(self):
        # 5-20 s de la segunda cancion a la mitad del volumen
        recorte = 0.5 * CANCIONES[1][2][5 * FRECUENCIA:20 * FRECUENCIA]
        r = self.analizar(recorte)
        self.assertEqual(r["veredicto"], "posible_copyright")
        self.assertEqual([(t["inicio"], t["fin"]) for t in r["tramos"]], [(0.0, 10.0), (5.0, 15.0)])
        self.assertEqual(r["canciones"][0]["archivo"], "medios.wav")
        self.assertEqual(r["canciones"][0]["tramos"], 2)
        primeras = [t["coincidencias"][0] for t in r["tramos"]]
        self.assertEqual([c["inicio_original"] for c in primeras], [5.0, 10.0])
        self.assertTrue(all(c["supera_umbral"] for c in primeras))
        print("recorte:", [(c["archivo"], c["similitud"]) for c in primeras])

    def test_audio_distinto_sin_coincidencias(self):
        r = self.analizar(melodia([2093, 2349, 2637, 3136], 20, 9), "agudos.wav")
        self.assertEqual(r["veredicto"], "sin_coincidencias")
        self.assertEqual(r["canciones"], [])
        print("agudos: mejor similitud", max(c["similitud"] for t in r["tramos"] for c in t["coincidencias"]))

    def test_rechaza_lo_que_no_es_audio(self):
        class Peticion:
            async def body(self):
                return b"hola"

        with self.assertRaises(HTTPException) as error:
            asyncio.run(copyright.analizar(Peticion(), "notas.txt"))
        self.assertEqual(error.exception.status_code, 400)


if __name__ == "__main__":
    unittest.main()
