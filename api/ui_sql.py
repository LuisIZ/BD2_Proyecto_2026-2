"""Interfaz simple del minigestor: 4 paneles (tablas, consulta, resultados, plan).

    python api/ui_sql.py                 usa datos/db
    python api/ui_sql.py --db otra/ruta

Ejecutar: F5 o Ctrl+Enter. Varias sentencias separadas por ';'.
"""

import argparse
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))
from motor_cli import ErrorMotor, MotorSQL  # noqa: E402

EJEMPLOS = """-- Cargar una tabla desde CSV (HEAP, SEQUENTIAL o BPLUS)
CREATE TABLE organizaciones FROM FILE 'datos/organizations-1000.csv' USING SEQUENTIAL;

-- Consultas
SELECT Index, Name, Country, Founded FROM organizaciones WHERE Index = 500;
SELECT Index, Name FROM organizaciones WHERE Index BETWEEN 10 AND 20;
SELECT Country, COUNT(*), AVG(Number_of_employees) FROM organizaciones GROUP BY Country ORDER BY Country LIMIT 10;

-- JOIN: hace falta una segunda tabla
-- CREATE TABLE decadas FROM FILE 'datos/decadas.csv' USING BPLUS;
-- CREATE TABLE paises FROM FILE 'datos/paises.csv' USING HEAP;

-- misma consulta en los dos sentidos: mira 'algoritmo' en el plan
-- SELECT organizaciones.Name, decadas.Decada FROM organizaciones JOIN decadas ON organizaciones.Founded = decadas.Founded LIMIT 20;
-- SELECT decadas.Decada, organizaciones.Name FROM decadas JOIN organizaciones ON decadas.Founded = organizaciones.Founded LIMIT 20;

-- el WHERE se aplica antes del join: mira filas_izquierda
-- SELECT organizaciones.Name, paises.Region FROM organizaciones JOIN paises ON organizaciones.Country = paises.Country WHERE organizaciones.Country = 'Peru';
-- SELECT paises.Region, COUNT(*) FROM organizaciones JOIN paises ON organizaciones.Country = paises.Country GROUP BY paises.Region ORDER BY Region;

-- Indice secundario (solo sobre tablas HEAP)
-- CREATE INDEX idx_founded ON organizaciones (Founded);

-- Modificaciones
-- INSERT INTO organizaciones VALUES (1001, 'abc', 'Nueva SAC', 'http://x', 'Peru', 'desc', 2024, 'Software', 10);
-- DELETE FROM organizaciones WHERE Index = 1001;
"""


class Aplicacion(tk.Tk):
    def __init__(self, db):
        super().__init__()
        self.title("Minigestor BD2 - cliente SQL")
        self.geometry("1280x800")
        self.motor = None
        self.db = db
        self._armar()
        self.after(100, self._arrancar)

    # --- construccion de la ventana ---

    def _armar(self):
        barra = ttk.Frame(self, padding=(8, 6))
        barra.pack(fill=tk.X)
        ttk.Button(barra, text="Ejecutar (F5)", command=self.ejecutar).pack(side=tk.LEFT)
        ttk.Button(barra, text="Limpiar", command=lambda: self.editor.delete("1.0", tk.END)).pack(side=tk.LEFT, padx=4)
        ttk.Button(barra, text="Cargar CSV...", command=self.cargar_csv).pack(side=tk.LEFT, padx=4)
        ttk.Button(barra, text="Ejemplos", command=self.poner_ejemplos).pack(side=tk.LEFT, padx=4)
        ttk.Button(barra, text="Refrescar tablas", command=self.refrescar_tablas).pack(side=tk.LEFT, padx=4)
        ttk.Button(barra, text="Recompilar motor", command=self.recompilar).pack(side=tk.RIGHT)
        self.estado = tk.StringVar(value="iniciando...")
        ttk.Label(barra, textvariable=self.estado).pack(side=tk.RIGHT, padx=12)

        principal = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        principal.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0, 8))

        # panel 1: archivos / tablas
        marco_tablas = ttk.LabelFrame(principal, text="1. Tablas cargadas", padding=4)
        self.arbol = ttk.Treeview(marco_tablas, columns=("info",), show="tree headings")
        self.arbol.heading("#0", text="tabla / columna")
        self.arbol.heading("info", text="detalle")
        self.arbol.column("#0", width=200)
        self.arbol.column("info", width=170)
        self.arbol.pack(fill=tk.BOTH, expand=True)
        self.arbol.bind("<Double-1>", self._doble_click_tabla)
        principal.add(marco_tablas, weight=1)

        derecha = ttk.PanedWindow(principal, orient=tk.VERTICAL)
        principal.add(derecha, weight=4)

        # panel 2: consulta
        marco_editor = ttk.LabelFrame(derecha, text="2. Consulta SQL", padding=4)
        self.editor = tk.Text(marco_editor, height=10, font=("Consolas", 11), undo=True, wrap=tk.NONE)
        self.editor.pack(fill=tk.BOTH, expand=True)
        self.editor.bind("<F5>", lambda e: self.ejecutar())
        self.editor.bind("<Control-Return>", lambda e: self.ejecutar() or "break")
        derecha.add(marco_editor, weight=2)

        # panel 3: resultados
        marco_resultados = ttk.LabelFrame(derecha, text="3. Resultados", padding=4)
        self.resumen = tk.StringVar(value="")
        ttk.Label(marco_resultados, textvariable=self.resumen).pack(anchor=tk.W)
        contenedor = ttk.Frame(marco_resultados)
        contenedor.pack(fill=tk.BOTH, expand=True)
        self.grilla = ttk.Treeview(contenedor, show="headings")
        sy = ttk.Scrollbar(contenedor, orient=tk.VERTICAL, command=self.grilla.yview)
        sx = ttk.Scrollbar(contenedor, orient=tk.HORIZONTAL, command=self.grilla.xview)
        self.grilla.configure(yscrollcommand=sy.set, xscrollcommand=sx.set)
        self.grilla.grid(row=0, column=0, sticky="nsew")
        sy.grid(row=0, column=1, sticky="ns")
        sx.grid(row=1, column=0, sticky="ew")
        contenedor.rowconfigure(0, weight=1)
        contenedor.columnconfigure(0, weight=1)
        derecha.add(marco_resultados, weight=4)

        # panel 4: plan de ejecucion
        marco_plan = ttk.LabelFrame(derecha, text="4. Plan de ejecucion", padding=4)
        self.plan = tk.Text(marco_plan, height=9, font=("Consolas", 10), state=tk.DISABLED, wrap=tk.WORD)
        self.plan.tag_configure("titulo", font=("Consolas", 10, "bold"))
        self.plan.tag_configure("error", foreground="#b00020")
        self.plan.tag_configure("paso", foreground="#1a4d8f")
        self.plan.pack(fill=tk.BOTH, expand=True)
        derecha.add(marco_plan, weight=2)

    # --- arranque ---

    def _arrancar(self):
        self.estado.set("compilando motor si hace falta...")
        self.update_idletasks()
        try:
            self.motor = MotorSQL(db=self.db)
        except ErrorMotor as e:
            messagebox.showerror("Motor", str(e))
            self.estado.set("motor no disponible")
            return
        self.estado.set(f"listo - base en {self.db}")
        self.poner_ejemplos()
        self.refrescar_tablas()

    def recompilar(self):
        if not self.motor:
            self.motor = MotorSQL(db=self.db, compilar=False)
        self.estado.set("compilando...")
        self.update_idletasks()
        ok, salida = self.motor.compilar()
        self._escribir_plan([(salida, "paso" if ok else "error")], limpiar=True)
        self.estado.set("motor recompilado" if ok else "error al compilar")

    # --- acciones ---

    def poner_ejemplos(self):
        self.editor.delete("1.0", tk.END)
        self.editor.insert("1.0", EJEMPLOS)

    def cargar_csv(self):
        ruta = filedialog.askopenfilename(title="CSV", filetypes=[("CSV", "*.csv"), ("Todos", "*.*")])
        if not ruta:
            return
        try:
            ruta = str(Path(ruta).relative_to(self.motor.raiz)).replace("\\", "/")
        except ValueError:
            ruta = ruta.replace("\\", "/")
        nombre = Path(ruta).stem.replace("-", "_")
        self.editor.insert(tk.END, f"\nCREATE TABLE {nombre} FROM FILE '{ruta}' USING HEAP;\n")

    def ejecutar(self):
        if not self.motor:
            return
        sql = self.editor.get("sel.first", "sel.last") if self.editor.tag_ranges("sel") else self.editor.get("1.0", tk.END)
        sql = "\n".join(l for l in sql.splitlines() if not l.strip().startswith("--")).strip()
        if not sql:
            return
        self.estado.set("ejecutando...")
        self._escribir_plan([], limpiar=True)

        # el hilo solo habla con el motor; la ventana se actualiza desde el hilo principal
        cola = queue.Queue()

        def trabajo():
            try:
                cola.put(self.motor.ejecutar(sql))
            except ErrorMotor as e:
                cola.put([{"ok": False, "error": str(e)}])

        threading.Thread(target=trabajo, daemon=True).start()
        self._esperar(cola)

    def _esperar(self, cola):
        try:
            respuestas = cola.get_nowait()
        except queue.Empty:
            self.after(50, lambda: self._esperar(cola))
            return
        self._mostrar(respuestas)

    def _mostrar(self, respuestas):
        lineas = []
        ultima_con_filas = None
        errores = 0
        for i, r in enumerate(respuestas, 1):
            if not r["ok"]:
                errores += 1
                lineas.append((f"[{i}] ERROR: {r['error']}\n", "error"))
                continue
            lineas.append((f"[{i}] {r['tipo']}: {r['mensaje']}  ({r['tiempo_ms']:.2f} ms)\n", "titulo"))
            for paso in r["plan"]:
                detalles = "  ".join(f"{k}={v}" for k, v in paso.items() if k != "operacion")
                lineas.append((f"      {paso['operacion']:22} {detalles}\n", "paso"))
            if r["columnas"]:
                ultima_con_filas = r
        self._escribir_plan(lineas, limpiar=True)
        if ultima_con_filas:
            self._llenar_grilla(ultima_con_filas)
        else:
            self._llenar_grilla(None)
        self.estado.set(f"{len(respuestas)} sentencias, {errores} errores")
        tipos = {r.get("tipo") for r in respuestas}
        if tipos & {"create_table", "drop_table", "create_index", "insert", "delete"}:
            self.refrescar_tablas()

    def _llenar_grilla(self, r):
        self.grilla.delete(*self.grilla.get_children())
        if not r:
            self.grilla["columns"] = ()
            self.resumen.set("")
            return
        columnas = r["columnas"]
        self.grilla["columns"] = columnas
        limite = 2000
        filas = [[str(v) for v in fila] for fila in r["filas"][:limite]]
        # el ancho sale del contenido y no solo del encabezado: si no, valores
        # largos como la lista de indices de SHOW TABLES quedan cortados
        muestra = filas[:200]
        for i, c in enumerate(columnas):
            largo = max([len(c)] + [len(f[i]) for f in muestra if i < len(f)])
            self.grilla.heading(c, text=c)
            # sin stretch: cada columna conserva su ancho y el sobrante queda a la derecha
            self.grilla.column(c, width=max(80, min(420, 7 * largo + 24)), stretch=False)
        for fila in filas:
            self.grilla.insert("", tk.END, values=fila)
        extra = f" (se muestran {limite})" if len(r["filas"]) > limite else ""
        self.resumen.set(f"{len(r['filas'])} filas{extra} - {r['tiempo_ms']:.2f} ms")

    def _escribir_plan(self, lineas, limpiar=False):
        self.plan.configure(state=tk.NORMAL)
        if limpiar:
            self.plan.delete("1.0", tk.END)
        for texto, etiqueta in lineas:
            self.plan.insert(tk.END, texto, etiqueta)
        self.plan.configure(state=tk.DISABLED)

    def refrescar_tablas(self):
        if not self.motor:
            return
        self.arbol.delete(*self.arbol.get_children())
        try:
            catalogo = self.motor.catalogo()
            estadisticas = {t["tabla"].lower(): t for t in self.motor.tablas()}
        except ErrorMotor as e:
            self.arbol.insert("", tk.END, text="error", values=(str(e),))
            return
        for t in catalogo["tablas"]:
            e = estadisticas.get(t["nombre"].lower(), {})
            info = f"{t['organizacion']} - {e.get('registros', '?')} regs, {e.get('paginas', '?')} pag"
            nodo = self.arbol.insert("", tk.END, text=t["nombre"], values=(info,), open=True)
            for c in t["columnas"]:
                tipo = c["tipo"] if c["tipo"] == "INT" else f"VARCHAR({c['tam']})"
                marca = " [PK]" if c["pk"] else ""
                self.arbol.insert(nodo, tk.END, text=f"  {c['nombre']}{marca}", values=(tipo,))
            for x in t["indices"]:
                estructura = "hash extensible" if x.get("tipo") == "HASH" else "B+ no agrupado"
                self.arbol.insert(nodo, tk.END, text=f"  idx {x['nombre']}", values=(f"{estructura} ({x['columna']})",))
            if e.get("detalle"):
                self.arbol.insert(nodo, tk.END, text="  archivo", values=(e["detalle"],))

    def _doble_click_tabla(self, _evento):
        item = self.arbol.focus()
        if not item or self.arbol.parent(item):
            return
        nombre = self.arbol.item(item, "text")
        self.editor.insert(tk.END, f"\nSELECT * FROM {nombre} LIMIT 50;\n")


def main():
    parser = argparse.ArgumentParser(description="cliente SQL del minigestor")
    parser.add_argument("--db", default="datos/db", help="carpeta de la base (por defecto datos/db)")
    args = parser.parse_args()
    Aplicacion(args.db).mainloop()


if __name__ == "__main__":
    main()
