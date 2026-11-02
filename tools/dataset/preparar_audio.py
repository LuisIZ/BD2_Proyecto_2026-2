import argparse
import csv
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description="Genera audios_1k.csv (id,audio,genero) desde GTZAN")
    parser.add_argument("audios", help="carpeta genres_original de GTZAN (una subcarpeta por genero)")
    parser.add_argument("--salida", default=RAIZ / "datos" / "audios_1k.csv")
    args = parser.parse_args()

    raiz = Path(args.audios)
    archivos = sorted(raiz.glob("*/*.wav"))
    if not archivos:
        raise SystemExit(f"no hay .wav en {raiz}/<genero>/")
    with open(args.salida, "w", encoding="utf-8", newline="") as salida:
        escritor = csv.writer(salida, lineterminator="\n")
        escritor.writerow(["id", "audio", "genero"])
        for i, archivo in enumerate(archivos, 1):
            escritor.writerow([i, archivo.relative_to(raiz).as_posix(), archivo.parent.name])
    print(f"{len(archivos)} audios en {args.salida}")


if __name__ == "__main__":
    main()
