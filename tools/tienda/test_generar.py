"""Pruebas de los datos de la tienda: python -m unittest tools.tienda.test_generar"""

import tempfile
import unittest
from collections import Counter
from pathlib import Path

from tools.tienda import generar_datos as g

N = 10000
SEMILLA = 7


class GenerarDatosTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.productos = g.leer_productos(N)
        cls.tablas = g.generar(cls.productos, SEMILLA)

    def filas(self, tabla):
        return self.tablas[tabla][1]

    def test_misma_semilla_mismos_archivos(self):
        with tempfile.TemporaryDirectory() as uno, tempfile.TemporaryDirectory() as dos:
            g.escribir(self.tablas, Path(uno))
            g.escribir(g.generar(self.productos, SEMILLA), Path(dos))
            for tabla in self.tablas:
                self.assertEqual((Path(uno) / f"{tabla}.csv").read_bytes(),
                                 (Path(dos) / f"{tabla}.csv").read_bytes(), tabla)

    def test_otra_semilla_cambia_los_datos(self):
        otra = g.generar(self.productos, SEMILLA + 1)
        self.assertNotEqual(otra["compras"][1], self.filas("compras"))

    def test_conteos(self):
        self.assertEqual(len(self.filas("productos")), N)
        self.assertEqual(len(self.filas("sedes")), 25)
        self.assertEqual(len(self.filas("usuarios")), 500)
        self.assertTrue(27000 <= len(self.filas("inventario")) <= 33000)
        self.assertTrue(5000 <= len(self.filas("compras")) <= 10000)

    def test_ids_correlativos(self):
        for tabla, (_, filas) in self.tablas.items():
            ids = [int(fila[0]) for fila in filas]
            self.assertEqual(len(ids), len(set(ids)), tabla)
        # los de productos son los de productos_10k.csv, y por eso los de catalogo
        self.assertEqual([int(f[0]) for f in self.filas("productos")], list(range(1, N + 1)))

    def test_precios_dentro_del_rango_de_su_tipo(self):
        columnas = self.tablas["productos"][0]
        tipo, precio = columnas.index("tipo"), columnas.index("precio")
        self.assertEqual(columnas[-1], "precio")
        for fila in self.filas("productos"):
            minimo, maximo = g.PRECIOS.get(fila[tipo], g.PRECIO_OTROS)
            self.assertTrue(minimo <= float(fila[precio]) <= maximo, fila[:4])

    def test_sedes_en_lima(self):
        for _, _, _, ubicacion in self.filas("sedes"):
            lat, lon = map(float, ubicacion.split())
            self.assertTrue(-12.3 < lat < -11.8 and -77.2 < lon < -76.8, ubicacion)

    def test_referencias_existen(self):
        productos = {int(f[0]) for f in self.filas("productos")}
        sedes = {f[0] for f in self.filas("sedes")}
        usuarios = {f[0] for f in self.filas("usuarios")}
        for _, sede, producto, _ in self.filas("inventario"):
            self.assertIn(sede, sedes)
            self.assertIn(producto, productos)
        for _, usuario, producto in self.filas("compras"):
            self.assertIn(usuario, usuarios)
            self.assertIn(producto, productos)

    def test_stock(self):
        inventario = self.filas("inventario")
        self.assertTrue(all(0 <= f[3] <= g.STOCK_MAXIMO for f in inventario))
        pares = [(f[1], f[2]) for f in inventario]
        self.assertEqual(len(pares), len(set(pares)), "una fila por sede y producto")
        con_stock = {f[2] for f in inventario if f[3] > 0}
        self.assertEqual(len(con_stock), N, "todo producto tiene stock en alguna sede")
        self.assertGreater(sum(1 for f in inventario if f[3] == 0), 0, "hay productos agotados")

    def test_compras_siguen_las_preferencias(self):
        tipo_de = {int(p["id"]): p["tipo"] for p in self.productos}
        por_usuario = {}
        for _, usuario, producto in self.filas("compras"):
            por_usuario.setdefault(usuario, []).append(producto)
        self.assertEqual(len(por_usuario), 500)
        for usuario, compras in por_usuario.items():
            self.assertEqual(len(compras), len(set(compras)), "sin compras repetidas")
            # los 2 o 3 tipos que mas compra cubren al menos el 80 %
            top = Counter(tipo_de[p] for p in compras).most_common(3)
            self.assertGreaterEqual(sum(c for _, c in top), 0.8 * len(compras) - 1, usuario)

    def test_cada_registro_cabe_en_una_pagina(self):
        for tabla, (_, filas) in self.tablas.items():
            mayor = max(sum(len(str(c).encode("utf-8")) for c in fila) for fila in filas)
            self.assertLess(mayor, 4096, tabla)


if __name__ == "__main__":
    unittest.main()
