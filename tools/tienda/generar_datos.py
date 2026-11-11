"""Genera los datos de la tienda sobre el catalogo de productos ABO.

    python tools/tienda/generar_datos.py --n 10000 --semilla 7

ABO no trae precios, tiendas, stock ni compras, asi que se generan de forma reproducible (la
misma semilla da los mismos archivos) y se escriben en datos/tienda/:

    productos.csv   las columnas de productos_<n>.csv mas precio, con un rango por tipo
    sedes.csv       id, nombre, distrito, ubicacion: 25 sedes en distritos de Lima
    inventario.csv  id, sede_id, producto_id, stock: cada producto en 1 a 5 sedes
    usuarios.csv    id, nombre: 500 usuarios
    compras.csv     id, usuario_id, producto_id: cada usuario prefiere 2 o 3 tipos y el 80 %
                    de sus compras sale de ellos

Los id de productos.csv son los de productos_<n>.csv, y por eso coinciden con los de la tabla
catalogo (tools/dataset/preparar_catalogo.py). Ver docs/tienda.md.
"""

import argparse
import csv
import random
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
SALIDA = RAIZ / "datos" / "tienda"

USUARIOS = 500
COMPRAS_POR_USUARIO = (10, 20)  # 500 usuarios x 15 en promedio = unas 7 500 compras
SEDES_POR_PRODUCTO = (1, 5)  # 10 000 productos x 3 en promedio = unas 30 000 filas
STOCK_MAXIMO = 50
SIN_STOCK = 0.15  # parte de las filas de inventario con stock 0
PREFERIDAS = 0.8  # parte de las compras que salen de los tipos preferidos
MINIMO_POR_TIPO = 10  # un tipo con menos productos no se elige como preferido

# 25 sedes, una por distrito de Lima Metropolitana; coordenadas (lat, lon) del centro del distrito
SEDES = [
    ("Miraflores", -12.1211, -77.0297),
    ("Barranco", -12.1490, -77.0210),
    ("San Isidro", -12.0990, -77.0360),
    ("Santiago de Surco", -12.1450, -76.9920),
    ("La Molina", -12.0790, -76.9420),
    ("San Borja", -12.1080, -77.0010),
    ("Jesús María", -12.0770, -77.0490),
    ("Lince", -12.0850, -77.0360),
    ("Magdalena del Mar", -12.0910, -77.0700),
    ("Pueblo Libre", -12.0760, -77.0640),
    ("San Miguel", -12.0770, -77.0900),
    ("Cercado de Lima", -12.0464, -77.0428),
    ("Breña", -12.0590, -77.0530),
    ("La Victoria", -12.0710, -77.0170),
    ("Surquillo", -12.1120, -77.0210),
    ("Chorrillos", -12.1690, -77.0250),
    ("San Juan de Miraflores", -12.1600, -76.9700),
    ("Villa El Salvador", -12.2130, -76.9370),
    ("Ate", -12.0260, -76.9190),
    ("San Juan de Lurigancho", -11.9830, -77.0060),
    ("Los Olivos", -11.9700, -77.0730),
    ("Independencia", -11.9920, -77.0580),
    ("San Martín de Porres", -12.0040, -77.0880),
    ("Comas", -11.9330, -77.0590),
    ("Rímac", -12.0290, -77.0290),
]

# precio en soles (minimo, maximo) para los tipos mas comunes de productos_10k
PRECIOS = {
    "CELLULAR_PHONE_CASE": (15, 80),
    "SHOES": (90, 450),
    "SANDAL": (40, 220),
    "BOOT": (150, 600),
    "GROCERY": (5, 60),
    "HERB": (5, 40),
    "LEGUME": (5, 30),
    "TEA": (8, 45),
    "EDIBLE_OIL_VEGETABLE": (10, 60),
    "VITAMIN": (25, 120),
    "HEALTH_PERSONAL_CARE": (15, 150),
    "BEAUTY": (20, 180),
    "SKIN_CLEANING_AGENT": (15, 90),
    "HOME": (20, 300),
    "HOME_BED_AND_BATH": (30, 350),
    "HOME_FURNITURE_AND_DECOR": (60, 900),
    "KITCHEN": (20, 400),
    "DRINKING_CUP": (10, 60),
    "PILLOW": (30, 150),
    "FLAT_SHEET": (40, 200),
    "RUG": (120, 1500),
    "WALL_ART": (50, 600),
    "PLANTER": (25, 250),
    "LAMP": (60, 600),
    "LIGHT_FIXTURE": (80, 900),
    "LIGHT_BULB": (8, 60),
    "CHAIR": (150, 1500),
    "STOOL_SEATING": (120, 900),
    "SOFA": (900, 6000),
    "OTTOMAN": (150, 1200),
    "TABLE": (250, 3000),
    "DESK": (300, 2500),
    "BED": (800, 5000),
    "HEADBOARD": (300, 2000),
    "FURNITURE_COVER": (40, 250),
    "OUTDOOR_LIVING": (80, 2500),
    "FINEEARRING": (150, 2500),
    "FINENECKLACEBRACELETANKLET": (150, 3000),
    "FINERING": (200, 4000),
    "EARRING": (20, 150),
    "NECKLACE": (25, 200),
    "RING": (20, 180),
    "HANDBAG": (80, 900),
    "WALLET": (40, 300),
    "BACKPACK": (60, 400),
    "SUITCASE": (200, 1200),
    "HAT": (25, 150),
    "ACCESSORY": (15, 200),
    "PET_SUPPLIES": (15, 250),
    "OFFICE_PRODUCTS": (5, 200),
    "SPORTING_GOODS": (30, 800),
    "TOOLS": (20, 500),
    "HARDWARE": (10, 200),
    "HARDWARE_HANDLE": (10, 120),
    "STORAGE_HOOK": (8, 60),
    "JANITORIAL_SUPPLY": (10, 120),
    "FOOD_SERVICE_SUPPLY": (10, 200),
    "SAFETY_SUPPLY": (10, 150),
    "AUTO_ACCESSORY": (20, 400),
    "PORTABLE_ELECTRONIC_DEVICE_COVER": (20, 120),
}
PRECIO_OTROS = (20, 500)

NOMBRES = [
    "Ana", "Luis", "María", "Carlos", "Lucía", "Jorge", "Rosa", "Diego", "Camila", "José",
    "Valeria", "Miguel", "Sofía", "Juan", "Daniela", "Pedro", "Fiorella", "Renzo", "Andrea",
    "Gabriel", "Paola", "Sebastián", "Carmen", "Alonso", "Brigitte", "Noé", "Dayane", "Mateo",
    "Ximena", "Rodrigo",
]
APELLIDOS = [
    "Quispe", "Flores", "Rojas", "Huamán", "Mamani", "García", "Torres", "Ramírez", "Vargas",
    "Castillo", "Mendoza", "Chávez", "Paredes", "Gutiérrez", "Sánchez", "Díaz", "Romero",
    "Salazar", "Cárdenas", "Espinoza", "Ríos", "Zapata", "Medina", "Vega",
]


def ruta_productos(n):
    """El CSV de productos mas chico que tiene al menos n filas."""
    for nombre, filas in (("1k", 1000), ("10k", 10000), ("100k", 100000)):
        ruta = RAIZ / "datos" / f"productos_{nombre}.csv"
        if filas >= n and ruta.is_file():
            return ruta
    raise SystemExit(f"no hay un CSV de productos con {n} filas (ver docs/dataset.md)")


def leer_productos(n):
    with open(ruta_productos(n), encoding="utf-8", newline="") as archivo:
        lector = csv.DictReader(archivo)
        filas = [fila for _, fila in zip(range(n), lector)]
    if len(filas) < n:
        raise SystemExit(f"el CSV solo tiene {len(filas)} productos")
    return filas


def generar(productos, semilla):
    """Devuelve las 5 tablas como {nombre: (columnas, filas)}. No escribe nada."""
    rng = random.Random(semilla)

    columnas = list(productos[0].keys()) + ["precio"]
    filas_productos = []
    for producto in productos:
        minimo, maximo = PRECIOS.get(producto["tipo"], PRECIO_OTROS)
        precio = round(rng.uniform(minimo, maximo - 1)) + 0.9  # precios como 49.90
        filas_productos.append([*producto.values(), f"{precio:.2f}"])

    sedes = [[i, f"Tienda {distrito}", distrito, f"{lat:.4f} {lon:.4f}"]
             for i, (distrito, lat, lon) in enumerate(SEDES, start=1)]

    inventario = []
    for producto in productos:
        elegidas = rng.sample(range(1, len(SEDES) + 1), rng.randint(*SEDES_POR_PRODUCTO))
        for orden, sede in enumerate(sorted(elegidas)):
            # la primera sede siempre tiene stock, asi todo producto se puede comprar en algun lado
            sin_stock = orden > 0 and rng.random() < SIN_STOCK
            stock = 0 if sin_stock else rng.randint(1, STOCK_MAXIMO)
            inventario.append([len(inventario) + 1, sede, int(producto["id"]), stock])

    usuarios = [[i, f"{rng.choice(NOMBRES)} {rng.choice(APELLIDOS)}"] for i in range(1, USUARIOS + 1)]

    por_tipo = {}
    for producto in productos:
        por_tipo.setdefault(producto["tipo"], []).append(int(producto["id"]))
    tipos = sorted(t for t, ids in por_tipo.items() if len(ids) >= MINIMO_POR_TIPO)
    todos = [int(p["id"]) for p in productos]

    compras = []
    for usuario in range(1, USUARIOS + 1):
        preferidos = rng.sample(tipos, rng.randint(2, 3))
        favoritos = sorted({i for t in preferidos for i in por_tipo[t]})
        total = rng.randint(*COMPRAS_POR_USUARIO)
        de_preferidos = min(round(total * PREFERIDAS), len(favoritos))
        elegidos = rng.sample(favoritos, de_preferidos)
        vistos = set(elegidos)
        while len(elegidos) < total:  # el resto, de cualquier tipo y sin repetir
            producto = rng.choice(todos)
            if producto not in vistos:
                vistos.add(producto)
                elegidos.append(producto)
        rng.shuffle(elegidos)
        compras.extend([len(compras) + 1, usuario, producto] for producto in elegidos)

    return {
        "productos": (columnas, filas_productos),
        "sedes": (["id", "nombre", "distrito", "ubicacion"], sedes),
        "inventario": (["id", "sede_id", "producto_id", "stock"], inventario),
        "usuarios": (["id", "nombre"], usuarios),
        "compras": (["id", "usuario_id", "producto_id"], compras),
    }


def escribir(tablas, carpeta):
    carpeta.mkdir(parents=True, exist_ok=True)
    for nombre, (columnas, filas) in tablas.items():
        with open(carpeta / f"{nombre}.csv", "w", encoding="utf-8", newline="") as archivo:
            escritor = csv.writer(archivo, lineterminator="\n")
            escritor.writerow(columnas)
            escritor.writerows(filas)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--n", type=int, default=10000, help="cantidad de productos")
    parser.add_argument("--semilla", type=int, default=7)
    parser.add_argument("--salida", type=Path, default=SALIDA)
    args = parser.parse_args()

    tablas = generar(leer_productos(args.n), args.semilla)
    escribir(tablas, args.salida)
    for nombre, (_, filas) in tablas.items():
        print(f"{nombre}.csv: {len(filas)} filas")
    print(f"en {args.salida}")


if __name__ == "__main__":
    main()
