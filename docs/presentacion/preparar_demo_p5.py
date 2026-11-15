"""Prepara los archivos de la demo de la Parte 5: el audio para la vista Copyright y la foto de SIMILAR_TO.

    python docs/presentacion/preparar_demo_p5.py --foto C:/ruta/a/una/foto.jpg

- Escribe datos/audio/remix_demo.wav: 20 s de classical.00011 y luego blues.00000 (de 5 a 25 s) a la
  mitad del volumen. La vista Copyright debe reportar las dos canciones.
- Con --foto, copia esa imagen a .build/subidas/foto.jpg, donde la busca SIMILAR_TO('foto.jpg').
"""

import argparse
import shutil
import sys
from pathlib import Path

import librosa
import numpy as np
import soundfile

RAIZ = Path(__file__).resolve().parents[2]
TASA = 22050


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--audios", default=RAIZ / "datos" / "audio" / "genres_original", type=Path)
    parser.add_argument("--salida", default=RAIZ / "datos" / "audio" / "remix_demo.wav", type=Path)
    parser.add_argument("--foto", type=Path, help="imagen que se copia a .build/subidas/foto.jpg")
    args = parser.parse_args()

    clasica = args.audios / "classical" / "classical.00011.wav"
    blues = args.audios / "blues" / "blues.00000.wav"
    if not clasica.is_file() or not blues.is_file():
        sys.exit(f"faltan los audios de GTZAN en {args.audios} (ver docs/extraccion_audio.md)")
    primera, _ = librosa.load(clasica, sr=TASA, mono=True, offset=0, duration=20)
    segunda, _ = librosa.load(blues, sr=TASA, mono=True, offset=5, duration=20)
    soundfile.write(args.salida, np.concatenate([primera, 0.5 * segunda]), TASA)
    print(f"audio de prueba en {args.salida}")

    if args.foto:
        if not args.foto.is_file():
            sys.exit(f"no existe {args.foto}")
        destino = RAIZ / ".build" / "subidas" / "foto.jpg"
        destino.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.foto, destino)
        print(f"foto en {destino}")


if __name__ == "__main__":
    main()
