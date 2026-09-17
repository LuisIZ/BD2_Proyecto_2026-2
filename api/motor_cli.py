"""Envuelve el binario motor_sql (C++) para usarlo desde Python.

    from motor_cli import MotorSQL
    motor = MotorSQL()                      # compila .build/motor_sql si hace falta
    for r in motor.ejecutar("SELECT * FROM t WHERE Index = 5"):
        print(r["columnas"], r["filas"], r["plan"])

Tambien sirve de consola:  python api/motor_cli.py "SHOW TABLES"
"""

import json
import os
import platform
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent

FUENTES = [
    "motor/consultas/motor_sql.cpp",
    "motor/consultas/parser_sql.cpp",
    "motor/consultas/catalogo.cpp",
    "motor/consultas/ejecutor.cpp",
    "motor/consultas/external_algorithms.cpp",
    "motor/archivos/pagina_slotted.cpp",
    "motor/archivos/heap_file.cpp",
    "motor/archivos/sequential_file.cpp",
    "motor/indices/bplus_agrupado.cpp",
    "motor/indices/bplus_no_agrupado.cpp",
    "motor/indices/buffer_pool.cpp",
    "motor/indices/gestor_paginas.cpp",
]

CABECERAS = ["motor/consultas", "motor/archivos", "motor/indices", "motor/comun"]


class ErrorMotor(Exception):
    pass


class MotorSQL:
    def __init__(self, db="datos/db", binario=None, compilar=True):
        self.raiz = RAIZ
        self.db = str(db)
        exe = ".exe" if platform.system() == "Windows" else ""
        self.binario = Path(binario) if binario else self.raiz / ".build" / f"motor_sql{exe}"
        if compilar and self.necesita_compilar():
            ok, salida = self.compilar()
            if not ok:
                raise ErrorMotor("no se pudo compilar el motor:\n" + salida)

    # --- compilacion ---

    def necesita_compilar(self):
        if not self.binario.exists():
            return True
        binario_mtime = self.binario.stat().st_mtime
        rutas = [self.raiz / f for f in FUENTES]
        for carpeta in CABECERAS:
            rutas += list((self.raiz / carpeta).glob("*.h"))
        return any(r.exists() and r.stat().st_mtime > binario_mtime for r in rutas)

    def compilar(self):
        """Compila con g++. Devuelve (ok, salida)."""
        self.binario.parent.mkdir(parents=True, exist_ok=True)
        orden = ["g++", "-std=c++17", "-O2", "-Wall"] + FUENTES + ["-o", str(self.binario)]
        proceso = subprocess.run(orden, cwd=self.raiz, capture_output=True, text=True)
        salida = " ".join(orden) + "\n" + proceso.stdout + proceso.stderr
        return proceso.returncode == 0, salida

    # --- ejecucion ---

    def _correr(self, argumentos):
        orden = [str(self.binario), "--db", self.db] + argumentos
        proceso = subprocess.run(orden, cwd=self.raiz, capture_output=True)
        texto = proceso.stdout.decode("utf-8", errors="replace").strip()
        if not texto:
            raise ErrorMotor(proceso.stderr.decode("utf-8", errors="replace") or "el motor no respondio")
        try:
            return json.loads(texto)
        except json.JSONDecodeError as e:
            raise ErrorMotor(f"respuesta no valida del motor: {e}\n{texto[:500]}")

    def ejecutar(self, sql):
        """Ejecuta una o varias sentencias (separadas por ';'). Devuelve una lista de respuestas."""
        return self._correr(["--sql", sql])

    def catalogo(self):
        return self._correr(["--catalogo"])

    def tablas(self):
        """SHOW TABLES como lista de dicts."""
        r = self.ejecutar("SHOW TABLES")[0]
        if not r["ok"]:
            raise ErrorMotor(r["error"])
        return [dict(zip(r["columnas"], fila)) for fila in r["filas"]]


def imprimir(respuesta):
    if not respuesta["ok"]:
        print("ERROR:", respuesta["error"])
        return
    print(f"[{respuesta['tipo']}] {respuesta['mensaje']} ({respuesta['tiempo_ms']:.2f} ms)")
    if respuesta["columnas"]:
        anchos = [len(c) for c in respuesta["columnas"]]
        filas = [[str(v) for v in f] for f in respuesta["filas"]]
        for f in filas:
            anchos = [max(a, min(len(v), 40)) for a, v in zip(anchos, f)]
        linea = "  ".join(c.ljust(a) for c, a in zip(respuesta["columnas"], anchos))
        print(linea)
        print("-" * len(linea))
        for f in filas[:50]:
            print("  ".join(v[:40].ljust(a) for v, a in zip(f, anchos)))
        if len(filas) > 50:
            print(f"... {len(filas)} filas")
    for paso in respuesta["plan"]:
        detalles = ", ".join(f"{k}={v}" for k, v in paso.items() if k != "operacion")
        print(f"  plan: {paso['operacion']}  {detalles}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    motor = MotorSQL(db=os.environ.get("BD2_DB", "datos/db"))
    for r in motor.ejecutar(" ".join(sys.argv[1:])):
        imprimir(r)
