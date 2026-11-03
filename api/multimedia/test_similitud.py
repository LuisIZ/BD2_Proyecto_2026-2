"""Pruebas de los indices FAISS que la API construye y mantiene con el archivo de cambios del motor.

    cd api && python -m unittest multimedia.test_similitud -v

Usan el motor de verdad (motor_cli compila .build/motor_sql si hace falta) sobre una base temporal.
"""

import os
import shutil
import tempfile
import unittest

import numpy as np

from motor_cli import RAIZ, MotorSQL
from multimedia.similitud import IndicesFaiss, leer_cambios

DIMENSION = 16
N = 300


def literal(v):
    return "VECTOR(" + ", ".join(f"{float(x):.8g}" for x in v) + ")"


def vecinos_coseno(vectores, ids, consulta, k):
    d = 1 - (vectores @ consulta) / (np.linalg.norm(vectores, axis=1) * np.linalg.norm(consulta))
    return [int(ids[i]) for i in np.argsort(d, kind="stable")[:k]]


class PruebaIndicesFaiss(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(dir=RAIZ / ".build")
        self.motor = MotorSQL(db=os.path.relpath(self.dir, RAIZ))
        self.indices = IndicesFaiss()
        rng = np.random.default_rng(5)
        self.vectores = rng.standard_normal((N, DIMENSION)).astype(np.float32)
        self.ids = np.arange(1, N + 1)
        csv = os.path.join(self.dir, "fotos.csv")
        with open(csv, "w", encoding="utf-8") as f:
            f.write("id,v\n")
            for i, v in zip(self.ids, self.vectores):
                f.write(f'{i},"{literal(v)}"\n')
        self.sql(f"CREATE TABLE fotos (id INT PRIMARY KEY, v VECTOR({DIMENSION})); COPY fotos FROM FILE '{csv}'")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def sql(self, texto):
        respuestas = self.motor.ejecutar(texto)
        for r in respuestas:
            self.assertTrue(r["ok"], r.get("error"))
        return respuestas

    def sincronizar(self):
        return {r["indice"]: r for r in self.indices.sincronizar(self.motor.catalogo())}

    def indice(self, nombre):
        for tabla in self.motor.catalogo()["tablas"]:
            for indice in tabla["indices"]:
                if indice["nombre"] == nombre:
                    return indice
        raise KeyError(nombre)

    def test_create_index_construye_con_la_tabla(self):
        self.sql("CREATE INDEX f_hnsw ON fotos (v) USING HNSW; CREATE INDEX f_ivf ON fotos (v) USING IVF WITH (nlist = 4, nprobe = 4)")
        resumen = self.sincronizar()
        for nombre in ("f_hnsw", "f_ivf"):
            self.assertTrue(resumen[nombre]["construido"])
            self.assertEqual(resumen[nombre]["vectores"], N)
            ruta = self.indices.ruta(self.indice(nombre))
            self.assertTrue(ruta.exists())
            self.assertFalse(self.indices.ruta_cambios(ruta).exists(), "el archivo de cambios se borra despues de guardar")
        hnsw, _ = self.indices.abrir(self.indice("f_hnsw"), DIMENSION)
        self.assertEqual(hnsw.parametros["ef_busqueda"], 64)
        consulta = self.vectores[41]
        self.assertEqual([pk for pk, _ in hnsw.knn(consulta, 5)], vecinos_coseno(self.vectores, self.ids, consulta, 5))
        self.assertEqual(self.sincronizar(), {}, "sin cambios no se vuelve a escribir nada")

    def test_insert_delete_y_transacciones(self):
        self.sql("CREATE INDEX f_hnsw ON fotos (v) USING HNSW")
        self.sincronizar()
        nuevo = np.ones(DIMENSION, dtype=np.float32)
        self.sql(f"INSERT INTO fotos VALUES (1000, {literal(nuevo)}); DELETE FROM fotos WHERE id <= 10")
        resumen = self.sincronizar()["f_hnsw"]
        self.assertEqual((resumen["insertados"], resumen["eliminados"], resumen["vectores"]), (1, 10, N - 9))
        hnsw, _ = self.indices.abrir(self.indice("f_hnsw"), DIMENSION)
        self.assertEqual(hnsw.knn(nuevo, 1)[0][0], 1000)
        self.assertNotIn(1, [pk for pk, _ in hnsw.knn(self.vectores[0], 5)], "lo borrado no vuelve a salir")

        # ROLLBACK (explicito o al final del lote) no deja nada para FAISS
        self.sql(f"BEGIN; DELETE FROM fotos WHERE id = 1000; INSERT INTO fotos VALUES (2000, {literal(nuevo)}); ROLLBACK")
        self.sql("BEGIN; DELETE FROM fotos WHERE id = 1000")
        self.assertEqual(self.sincronizar(), {})
        # COMMIT si
        self.sql("BEGIN; DELETE FROM fotos WHERE id = 1000; COMMIT")
        self.assertEqual(self.sincronizar()["f_hnsw"]["eliminados"], 1)
        self.assertNotEqual(hnsw.knn(nuevo, 1)[0][0], 1000)

    def test_volver_a_aplicar_no_duplica(self):
        """Si la API cae entre guardar el indice y borrar el archivo de cambios, se aplican otra vez sin repetir vectores."""
        self.sql("CREATE INDEX f_ivf ON fotos (v) USING IVF WITH (nlist = 4); CREATE INDEX f_hnsw ON fotos (v) USING HNSW")
        self.sincronizar()
        self.sql(f"INSERT INTO fotos VALUES (1000, {literal(np.ones(DIMENSION))}); DELETE FROM fotos WHERE id = 5")
        copias = {}
        for nombre in ("f_ivf", "f_hnsw"):
            cambios = self.indices.ruta_cambios(self.indices.ruta(self.indice(nombre)))
            self.assertEqual([chr(op) for op in leer_cambios(cambios, DIMENSION)["op"]], ["I", "D"])
            copias[cambios] = cambios.read_bytes()
        self.sincronizar()
        for cambios, contenido in copias.items():
            cambios.write_bytes(contenido)
        resumen = self.sincronizar()
        self.assertEqual(resumen["f_ivf"]["vectores"], N)
        self.assertEqual(resumen["f_hnsw"]["vectores"], N)

    def test_indice_nuevo_tras_drop_table(self):
        self.sql("CREATE INDEX f_hnsw ON fotos (v) USING HNSW")
        self.sincronizar()
        ruta = self.indices.ruta(self.indice("f_hnsw"))
        self.sql("DROP TABLE fotos")
        self.assertFalse(ruta.exists())
        self.assertEqual(self.sincronizar(), {})
        self.assertEqual(self.indices._abiertos, {}, "el indice de la tabla borrada sale de la memoria")
        self.sql(f"CREATE TABLE fotos (id INT PRIMARY KEY, v VECTOR({DIMENSION})); CREATE INDEX f_hnsw ON fotos (v) USING HNSW")
        resumen = self.sincronizar()["f_hnsw"]
        self.assertTrue(resumen["construido"])
        self.assertEqual(resumen["vectores"], 0, "una tabla vacia da un HNSW vacio")
        self.sql(f"INSERT INTO fotos VALUES (7, {literal(self.vectores[0])})")
        self.assertEqual(self.sincronizar()["f_hnsw"]["vectores"], 1)

    def test_un_error_no_frena_a_los_demas(self):
        self.sql("CREATE INDEX f_hnsw ON fotos (v) USING HNSW; CREATE INDEX f_ivf ON fotos (v) USING IVF WITH (nlist = 4)")
        cambios_ivf = self.indices.ruta_cambios(self.indices.ruta(self.indice("f_ivf")))
        with open(cambios_ivf, "ab") as f:
            f.write(b"\x00\x01")  # registro cortado
        resumen = self.sincronizar()
        self.assertIn("incompleto", resumen["f_ivf"]["error"])
        self.assertTrue(cambios_ivf.exists(), "los cambios quedan para el proximo intento")
        self.assertEqual(resumen["f_hnsw"]["vectores"], N)


if __name__ == "__main__":
    unittest.main()
