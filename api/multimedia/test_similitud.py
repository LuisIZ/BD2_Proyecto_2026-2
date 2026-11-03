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
from multimedia.similitud import IndicesFaiss, leer_cambios, reescribir

DIMENSION = 16
N = 300


def literal(v):
    return "VECTOR(" + ", ".join(f"{float(x):.8g}" for x in v) + ")"


def vecinos_coseno(vectores, ids, consulta, k):
    d = 1 - (vectores @ consulta) / (np.linalg.norm(vectores, axis=1) * np.linalg.norm(consulta))
    return [int(ids[i]) for i in np.argsort(d, kind="stable")[:k]]


class ConMotor(unittest.TestCase):
    """Una tabla fotos con N vectores al azar en una base temporal."""

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


class PruebaIndicesFaiss(ConMotor):
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


class PruebaSimilarTo(ConMotor):
    """La API reescribe SIMILAR_TO ... USING IVF|HNSW con los candidatos de FAISS y el motor arma la respuesta."""

    def setUp(self):
        super().setUp()
        self.sql("CREATE INDEX f_hnsw ON fotos (v) USING HNSW; CREATE INDEX f_ivf ON fotos (v) USING IVF WITH (nlist = 4, nprobe = 4)")
        self.sincronizar()
        self.consulta = np.random.default_rng(9).standard_normal(DIMENSION).astype(np.float32)

    def reescribir(self, sql):
        return reescribir(sql, self.motor.catalogo(), self.indices)

    def test_using_hnsw_e_ivf(self):
        esperado = vecinos_coseno(self.vectores, self.ids, self.consulta, 5)
        for tipo in ("HNSW", "IVF"):
            sql, busquedas = self.reescribir(f"SELECT id, SIMILARITY_SCORE() FROM fotos WHERE SIMILAR_TO({literal(self.consulta)}, k=5) USING {tipo}")
            self.assertIn(f"USING {tipo} CANDIDATES (", sql)
            self.assertEqual((busquedas[0]["tipo"], busquedas[0]["candidatos"], busquedas[0]["vectores"]), (tipo, 5, N))
            respuesta = self.sql(sql)[0]
            self.assertEqual([fila[0] for fila in respuesta["filas"]], esperado)
            self.assertEqual(respuesta["plan"][-1]["operacion"], "knn_vectorial_indice")
            scores = [fila[1] for fila in respuesta["filas"]]
            self.assertEqual(scores, sorted(scores, reverse=True))

    def test_lo_que_no_se_toca(self):
        v = literal(self.consulta)
        lote = (f"INSERT INTO fotos VALUES (5000, {v}); SELECT id FROM fotos WHERE SIMILAR_TO({v}, k=3) USING HNSW; "
                f"SELECT id FROM fotos WHERE SIMILAR_TO(v, {v}, k=3)")
        sql, busquedas = self.reescribir(lote)
        partes = sql.split(";\n")
        self.assertEqual(len(partes), 3)
        self.assertNotIn("CANDIDATES", partes[0] + partes[2], "el INSERT y el k-NN secuencial quedan igual")
        self.assertIn("CANDIDATES", partes[1])
        self.assertEqual(len(busquedas), 1)

        # EXPLAIN sin ANALYZE no ejecuta: el motor arma el plan sin candidatos
        sql, _ = self.reescribir(f"EXPLAIN SELECT * FROM fotos WHERE SIMILAR_TO({v}) USING HNSW")
        self.assertNotIn("CANDIDATES", sql)
        plan = "\n".join(f[0] for f in self.sql(sql)[0]["filas"])
        self.assertIn("Vector Scan using f_hnsw on fotos (HNSW, coseno, ef=64)", plan)
        sql, _ = self.reescribir(f"EXPLAIN ANALYZE SELECT * FROM fotos WHERE SIMILAR_TO({v}) USING HNSW")
        self.assertIn("CANDIDATES", sql)
        self.sql(sql)

        # lo que no se puede resolver llega igual al motor, que explica el error
        for consulta, error in [
            (f"SELECT id FROM fotos WHERE SIMILAR_TO({v}) USING HNSW WITH METRIC = euclidiana", "metrica"),
            (f"SELECT id FROM no_existe WHERE SIMILAR_TO({v}) USING HNSW", "no_existe"),
            ("SELECT id FROM fotos WHERE SIMILAR_TO('a;b.jpg') USING HNSW", "API"),
        ]:
            sql, busquedas = self.reescribir(consulta)
            self.assertEqual((sql, busquedas), (consulta, []))
            respuesta = self.motor.ejecutar(sql)[0]
            self.assertFalse(respuesta["ok"])
            self.assertIn(error, respuesta["error"])

    def test_aplica_lo_pendiente_antes_de_buscar(self):
        nuevo = -self.consulta
        self.sql(f"INSERT INTO fotos VALUES (7000, {literal(nuevo)})")  # sin sincronizar
        sql, busquedas = self.reescribir(f"SELECT id FROM fotos WHERE SIMILAR_TO({literal(nuevo)}, k=1) USING HNSW")
        self.assertEqual(busquedas[0]["cambios_aplicados"], 1)
        self.assertEqual(self.sql(sql)[0]["filas"], [[7000]])

    def test_endpoint_consultas(self):
        import sys
        sys.path.insert(0, str(RAIZ))
        from api import web_api

        web_api.motor, web_api.indices_faiss = self.motor, self.indices
        respuesta = web_api.consultas(web_api.Consulta(
            sql=f"-- vecinos con HNSW\nSELECT id FROM fotos WHERE SIMILAR_TO({literal(self.consulta)}, k=4) USING HNSW; "
                "DELETE FROM fotos WHERE id = 1"))
        self.assertEqual([f[0] for f in respuesta["resultados"][0]["filas"]], vecinos_coseno(self.vectores, self.ids, self.consulta, 4))
        busqueda, sincronizados = respuesta["faiss"][0], {r["indice"]: r for r in respuesta["faiss"][1:]}
        self.assertEqual((busqueda["indice"], busqueda["candidatos"]), ("f_hnsw", 4))
        self.assertEqual(sincronizados["f_hnsw"]["eliminados"], 1, "el DELETE del mismo lote se aplica despues")
        self.assertEqual(sincronizados["f_ivf"]["eliminados"], 1)


if __name__ == "__main__":
    unittest.main()
