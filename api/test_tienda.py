"""Pruebas de la API de la tienda (api/tienda.py) sobre una base temporal pequena hecha a mano.

    cd api && python -m unittest test_tienda -v

Usan el motor de verdad (motor_cli compila .build/motor_sql si hace falta) y el HNSW de FAISS.
"""

import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ))

from fastapi import HTTPException  # noqa: E402

from api import tienda, web_api  # noqa: E402
from api.motor_cli import MotorSQL  # noqa: E402

# id, nombre, tipo, marca, color, descripcion, precio, vector (None = sin fila en catalogo)
PRODUCTOS = [
    (1, "Red wooden chair", "CHAIR", "Rivet", "Red", "red wooden chair for the dining room", 120.5, (1, 0, 0, 0)),
    (2, "Blue wooden chair", "CHAIR", "Rivet", "Blue", "blue wooden chair", 99.9, (0.9, 0.1, 0, 0)),
    (3, "Red metal chair", "CHAIR", "Acme", "Red", "red metal chair", 80.0, (0.8, 0, 0.2, 0)),
    (4, "Green chair", "CHAIR", "Acme", "Green", "green plastic chair", 45.0, (0, 1, 0, 0)),
    (5, "Wooden table", "TABLE", "Rivet", "Red", "wooden table for four", 300.0, (0, 0, 1, 0)),
    (6, "Black table", "TABLE", "Acme", "Black", "black glass table", 250.0, (0, 0, 0.9, 0.1)),
    (7, "White lamp", "LAMP", "Acme", "White", "white floor lamp", 60.0, (0, 0, 0, 1)),
    (8, "Kid's lamp", "LAMP", "Acme", "White", "lamp for the kid's room", 30.0, None),
]
# id, usuario, producto: el 1 lo compraron u1 y u2; u1 tambien compro 2 y 3, u2 tambien compro 3
COMPRAS = [(1, 1, 1), (2, 1, 2), (3, 1, 3), (4, 2, 1), (5, 2, 3), (6, 3, 5)]
ORIGEN = (-12.1353, -77.0220)
# la sede 1 es la mas cercana al origen pero no tiene stock del producto 1
SEDES = [(1, "Sede A", "Barranco", -12.1360, -77.0225), (2, "Sede B", "Surco", -12.1500, -77.0210),
         (3, "Sede C", "Lince", -12.1000, -77.0300), (4, "Sede D", "Comas", -12.0000, -77.0000)]
INVENTARIO = [(1, 1, 1, 0), (2, 2, 1, 5), (3, 3, 1, 2), (4, 4, 1, 9), (5, 1, 5, 3)]


def texto(t):
    return "'" + t.replace("'", "''") + "'"


class PruebaTienda(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp(dir=RAIZ / ".build")
        cls.anterior = (web_api.motor, web_api.indices_faiss)
        web_api.motor = MotorSQL(db=os.path.relpath(cls.dir, RAIZ))
        web_api.indices_faiss = web_api.IndicesFaiss()
        tienda._tipos_por_base.clear()
        sql = [
            "CREATE TABLE productos (id INT PRIMARY KEY, item_id VARCHAR(20), nombre VARCHAR(200), tipo VARCHAR(50), "
            "categoria VARCHAR(200), marca VARCHAR(80), color VARCHAR(40), descripcion VARCHAR(1000), imagen VARCHAR(80), "
            "precio FLOAT) USING BPLUS",
            "CREATE TABLE catalogo (id INT PRIMARY KEY, nombre VARCHAR(80), v VECTOR(4)) USING BPLUS",
            "CREATE TABLE sedes (id INT PRIMARY KEY, nombre VARCHAR(40), distrito VARCHAR(30), ubicacion POINT)",
            "CREATE TABLE inventario (id INT PRIMARY KEY, sede_id INT, producto_id INT, stock INT)",
            "CREATE TABLE compras (id INT PRIMARY KEY, usuario_id INT, producto_id INT)",
        ]
        for i, nombre, tipo, marca, color, desc, precio, v in PRODUCTOS:
            sql.append(f"INSERT INTO productos VALUES ({i}, 'B{i:03}', {texto(nombre)}, '{tipo}', '/Categories', "
                       f"'{marca}', '{color}', {texto(desc)}, 'aa/{i}.jpg', {precio})")
            if v:
                sql.append(f"INSERT INTO catalogo VALUES ({i}, {texto(nombre)}, VECTOR({', '.join(map(str, v))}))")
        sql += [f"INSERT INTO sedes VALUES ({i}, '{n}', '{d}', POINT({la}, {lo}))" for i, n, d, la, lo in SEDES]
        sql += [f"INSERT INTO inventario VALUES ({i}, {s}, {p}, {k})" for i, s, p, k in INVENTARIO]
        sql += [f"INSERT INTO compras VALUES ({i}, {u}, {p})" for i, u, p in COMPRAS]
        sql += [
            "CREATE INDEX compras_usuario ON compras (usuario_id) USING HASH",
            "CREATE INDEX sedes_ubicacion ON sedes (ubicacion) USING RTREE",
            "CREATE INDEX productos_desc ON productos (descripcion) USING SPIMI",
            "CREATE INDEX catalogo_hnsw ON catalogo (v) USING HNSW",
        ]
        salida = web_api.consultas(web_api.Consulta(sql=";\n".join(sql)))
        for r in salida["resultados"]:
            assert r["ok"], r.get("error")

    @classmethod
    def tearDownClass(cls):
        web_api.motor, web_api.indices_faiss = cls.anterior
        tienda._tipos_por_base.clear()
        shutil.rmtree(cls.dir, ignore_errors=True)

    def test_rrf_contra_el_calculo_a_mano(self):
        listas = {"texto": [1, 2, 3], "imagen": [2, 3, 4], "metadata": [3, 1, 3]}
        fusion = tienda.rrf(listas)
        # 3: 1/63 + 1/62 + 1/61; 1 y 2: 1/61 + 1/62 (empatan y gana el id menor); 4: 1/63
        self.assertEqual([i for i, _, _ in fusion], [3, 1, 2, 4])
        scores = {i: s for i, s, _ in fusion}
        self.assertAlmostEqual(scores[3], 1 / 63 + 1 / 62 + 1 / 61)
        self.assertAlmostEqual(scores[1], 1 / 61 + 1 / 62)
        self.assertAlmostEqual(scores[2], scores[1])
        self.assertAlmostEqual(scores[4], 1 / 63)
        # el 3 repetido en metadata cuenta solo su primer puesto
        self.assertEqual(fusion[0][2], {"texto": 3, "imagen": 2, "metadata": 1})
        print("rrf: 3 listas iguales al calculo a mano, empates por id y repetidos una sola vez")

    def test_recomendaciones_excluyen_el_producto_consultado(self):
        r = tienda.recomendaciones(1, k=10)
        ids = [x["id"] for x in r["recomendaciones"]]
        self.assertNotIn(1, ids)
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(r["rrf_k"], 60)
        self.assertTrue(all(f["ok"] for f in r["fuentes"]), r["fuentes"])
        self.assertEqual(r["avisos"], [])
        # compras: u1 y u2 compraron el 1; el 3 lo compraron los dos y el 2 solo u1
        self.assertEqual(tienda.lista_compras(1, [1, 2], {1: [1, 2, 3], 2: [1, 3]}), [3, 2])
        # metadata: mismo tipo CHAIR y marca Rivet o color Red; primero el que coincide en marca
        filas = [{"id": 2, "marca": "Rivet", "color": "Blue"}, {"id": 3, "marca": "Acme", "color": "Red"},
                 {"id": 4, "marca": "Acme", "color": "Green"}]
        self.assertEqual(tienda.lista_metadata(filas, "Rivet", "Red"), [2, 3])
        candidatos = {f["fuente"]: f["candidatos"] for f in r["fuentes"]}
        self.assertEqual((candidatos["metadata"], candidatos["compras"]), (2, 2))
        self.assertEqual([x["puestos"].get("compras") for x in r["recomendaciones"][:2]], [2, 1])
        for x in r["recomendaciones"]:
            esperado = sum(1 / (60 + p) for p in x["puestos"].values())
            self.assertAlmostEqual(x["score"], esperado, places=6)
            self.assertEqual(set(x["fuentes"]), set(x["puestos"]))
        print(f"recomendaciones del 1: {ids}, sin el 1 y sin repetidos")

    def test_fuente_vacia_o_fallida(self):
        # el 8 no tiene compras ni vector: se fusionan texto y metadata y la respuesta lo avisa
        r = tienda.recomendaciones(8, k=5)
        fuentes = {f["fuente"]: f for f in r["fuentes"]}
        self.assertTrue(fuentes["compras"]["ok"])
        self.assertEqual(fuentes["compras"]["candidatos"], 0)
        self.assertFalse(fuentes["imagen"]["ok"])
        self.assertIn("no tiene vector", fuentes["imagen"]["error"])
        self.assertIn("la fuente compras no tiene candidatos", r["avisos"])
        self.assertTrue(any(a.startswith("la fuente imagen fallo") for a in r["avisos"]))
        self.assertIn(7, [x["id"] for x in r["recomendaciones"]])
        self.assertNotIn(8, [x["id"] for x in r["recomendaciones"]])
        print("fuente vacia (compras) y fallida (imagen): se fusionan las demas y se avisa")

    def test_sedes_por_distancia_y_con_stock(self):
        r = tienda.sedes(1, lat=ORIGEN[0], lon=ORIGEN[1], k=2)
        self.assertEqual([s["id"] for s in r["sedes"]], [2, 3])  # la 1 esta mas cerca pero sin stock
        self.assertEqual([s["stock"] for s in r["sedes"]], [5, 2])
        distancias = [s["distancia_m"] for s in r["sedes"]]
        self.assertEqual(distancias, sorted(distancias))
        self.assertAlmostEqual(distancias[0], tienda.haversine_m(ORIGEN, (-12.15, -77.021)), delta=1)
        todas = tienda.sedes(1, lat=ORIGEN[0], lon=ORIGEN[1], k=10)["sedes"]
        self.assertEqual([s["id"] for s in todas], [2, 3, 4])
        print(f"sedes del producto 1: {[s['id'] for s in todas]} por distancia, sin la sede sin stock")

    def test_catalogo_y_ficha(self):
        anterior = tienda.POR_PAGINA
        tienda.POR_PAGINA = 3
        try:
            r = tienda.productos(q="", tipo="CHAIR", pagina=2)
            self.assertEqual((r["total"], [p["id"] for p in r["productos"]]), (4, [4]))
            r = tienda.productos(q="", tipo="", pagina=2)
            self.assertEqual((r["total"], [p["id"] for p in r["productos"]]), (8, [4, 5, 6]))
            r = tienda.productos(q="wooden", tipo="", pagina=1)
            self.assertEqual(sorted(p["id"] for p in r["productos"]), [1, 2, 5])
            self.assertIn("MATCH(descripcion, 'wooden')", r["consulta"])
            r = tienda.productos(q="kid's", tipo="LAMP", pagina=1)
            self.assertEqual([p["id"] for p in r["productos"]], [8])
            self.assertEqual(r["tipos"][0], {"tipo": "CHAIR", "productos": 4})
        finally:
            tienda.POR_PAGINA = anterior
        ficha = tienda.producto(8)
        self.assertEqual((ficha["nombre"], ficha["precio"], ficha["item_id"]), ("Kid's lamp", 30.0, "B008"))
        with self.assertRaises(HTTPException) as error:
            tienda.producto(99)
        self.assertEqual(error.exception.status_code, 404)
        print("catalogo: paginas por texto, por tipo y sin filtros; ficha y 404")


if __name__ == "__main__":
    unittest.main()
