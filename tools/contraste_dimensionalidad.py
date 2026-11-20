"""Mide la concentracion de distancias de los vectores de imagenes (seccion de la
maldicion de la dimensionalidad del informe).

Uso: python tools/contraste_dimensionalidad.py [datos/productos/vectores_100000.npy]

Contraste relativo de una consulta: (d_max - d_min) / d_min sobre una muestra de la
coleccion. Si tiende a 0, el vecino mas cercano y el mas lejano estan casi a la misma
distancia y un indice exacto no puede podar.
"""
import sys

import numpy as np

SEMILLA = 7
N_COLECCION = 10_000
N_CONSULTAS = 200


def contraste(datos, rng):
    coleccion = datos[rng.choice(len(datos), N_COLECCION, replace=False)]
    consultas = datos[rng.choice(len(datos), N_CONSULTAS, replace=False)]
    valores = []
    for q in consultas:
        d = np.linalg.norm(coleccion - q, axis=1)
        d = d[d > 1e-9]  # descarta la propia consulta y las fotos repetidas
        valores.append((d.max() - d.min()) / d.min())
    return float(np.median(valores))


def main():
    ruta = sys.argv[1] if len(sys.argv) > 1 else "datos/productos/vectores_100000.npy"
    x = np.load(ruta).astype(np.float64)
    rng = np.random.default_rng(SEMILLA)
    print(f"vectores: {x.shape[0]} de dimension {x.shape[1]}")
    print(f"norma media: {np.linalg.norm(x, axis=1).mean():.3f}")
    print(f"componentes distintas de cero (media): {(x > 0).sum(axis=1).mean():.1f}")
    print(f"contraste relativo (mediana), ABO: {contraste(x, rng):.2f}")
    for d in (2, 16, 256):
        u = rng.random((20_000, d))
        u /= np.linalg.norm(u, axis=1, keepdims=True)
        print(f"contraste relativo (mediana), uniforme d={d}: {contraste(u, rng):.2f}")
    muestra = x[rng.choice(len(x), 2_000, replace=False)]
    cos = (muestra @ muestra.T)[np.triu_indices(2_000, 1)]
    print(f"coseno entre pares: media {cos.mean():.2f}, desviacion {cos.std():.2f}")
    centrada = x[rng.choice(len(x), 20_000, replace=False)]
    centrada = centrada - centrada.mean(axis=0)
    var = np.linalg.svd(centrada, compute_uv=False) ** 2
    acumulada = np.cumsum(var) / var.sum()
    print("componentes para 90 % de la varianza:", int(np.searchsorted(acumulada, 0.9)) + 1)


if __name__ == "__main__":
    main()
