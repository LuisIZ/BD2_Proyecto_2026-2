"""Un solo comando para dejar el proyecto listo y abrir la web.

    motor run      instala lo que falte, compila el motor y abre la web
    motor test     lo anterior y corre todas las pruebas
    motor setup    solo prepara el entorno, sin abrir nada
    motor bench    benchmarks de las estructuras
    motor clean    borra lo generado (.venv, node_modules, .build)

El ejecutable vive en `tools/bin/motor` y se enlaza a ~/.local/bin la primera vez
que se corre `./bd2 run`; desde entonces basta con escribir `motor`. No puede
estar en la raíz del repo porque ahí ya está la carpeta `motor/` del código C++,
y en Linux un archivo no puede llamarse igual que una carpeta. En Windows sí se
permite, así que `motor.cmd` está en la raíz y funciona desde el primer momento.

Con make instalado, `make run` hace lo mismo sin instalar nada.

No hace falta activar el entorno virtual: este script se encarga de todo y solo
necesita Python, Node y g++.

Cada paso se salta si ya está hecho, así que la segunda vez arranca en segundos.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
WEB = RAIZ / "web"
VENV = RAIZ / ".venv"
WINDOWS = os.name == "nt"
# el ejecutable `motor` vive en tools/bin y se enlaza al PATH; en la raíz no
# puede estar porque ahí ya existe la carpeta motor/ del código C++
ORIGEN_MOTOR = RAIZ / "tools" / "bin" / "motor"
DESTINO_MOTOR = Path.home() / ".local" / "bin" / "motor"
PY_VENV = VENV / ("Scripts/python.exe" if WINDOWS else "bin/python")
REQUISITOS = RAIZ / "api" / "requirements.txt"
# marca qué requirements.txt se instaló, para no reinstalar en cada arranque
SELLO = VENV / ".requisitos-instalados"

VERDE, AMARILLO, ROJO, GRIS, FIN = "\033[32m", "\033[33m", "\033[31m", "\033[90m", "\033[0m"
if WINDOWS and not os.environ.get("WT_SESSION"):
    VERDE = AMARILLO = ROJO = GRIS = FIN = ""


def paso(texto):
    print(f"\n{VERDE}==>{FIN} {texto}", flush=True)


def aviso(texto):
    print(f"{AMARILLO}  ! {texto}{FIN}", flush=True)


def detalle(texto):
    print(f"{GRIS}    {texto}{FIN}", flush=True)


def morir(texto, arreglo=None):
    print(f"\n{ROJO}Error:{FIN} {texto}", file=sys.stderr)
    if arreglo:
        print(f"  {arreglo}", file=sys.stderr)
    sys.exit(1)


def correr(orden, cwd=RAIZ, callar=False):
    """Lanza un comando y devuelve si salió bien."""
    resultado = subprocess.run(
        orden,
        cwd=cwd,
        shell=WINDOWS,  # en Windows npm es npm.cmd y hace falta el shell
        capture_output=callar,
        text=True,
    )
    if callar and resultado.returncode != 0:
        print(resultado.stdout or "", file=sys.stderr)
        print(resultado.stderr or "", file=sys.stderr)
    return resultado.returncode == 0


def buscar_node():
    """Añade Node al PATH si está instalado pero no visible.

    nvm, fnm y volta no instalan Node en el sistema: lo añaden al PATH desde el
    arranque del shell. Si `motor` se lanza desde un sitio donde ese arranque no
    corrió (un IDE, un servicio, cron, un shell no interactivo), Node existe pero
    no se ve. Aquí se busca en los sitios de siempre y se añade al PATH de este
    proceso, que es el que heredan npm y el servidor."""
    candidatos = []
    casa = Path.home()

    # nvm: ~/.nvm/versions/node/vX.Y.Z/bin · se prefiere la versión por defecto
    nvm = Path(os.environ.get("NVM_DIR", casa / ".nvm")) / "versions" / "node"
    if nvm.is_dir():
        versiones = sorted(
            (d for d in nvm.iterdir() if (d / "bin").is_dir()),
            key=lambda d: [int(n) for n in d.name.lstrip("v").split(".") if n.isdigit()] or [0],
        )
        alias = nvm.parent.parent / "alias" / "default"
        if alias.is_file():
            pedida = alias.read_text(encoding="utf-8").strip().lstrip("v")
            for d in versiones:
                if d.name.lstrip("v").startswith(pedida):
                    candidatos.append(d / "bin")
        candidatos += [d / "bin" for d in reversed(versiones)]

    # fnm
    fnm = casa / ".fnm" / "node-versions"
    if fnm.is_dir():
        candidatos += sorted((d / "installation" / "bin" for d in fnm.iterdir()), reverse=True)

    # volta, homebrew en Apple Silicon y las rutas habituales
    candidatos += [casa / ".volta" / "bin", Path("/opt/homebrew/bin"), Path("/usr/local/bin")]
    if WINDOWS:
        for base in (os.environ.get("ProgramFiles", ""), os.environ.get("APPDATA", "")):
            if base:
                candidatos += [Path(base) / "nodejs", Path(base) / "npm"]

    binario = "node.exe" if WINDOWS else "node"
    for carpeta in candidatos:
        if (carpeta / binario).is_file():
            os.environ["PATH"] = str(carpeta) + os.pathsep + os.environ.get("PATH", "")
            return carpeta
    return None


def lanzador():
    """Cómo escribe el usuario el comando, según lo que tenga instalado."""
    if WINDOWS:
        return "motor"
    if shutil.which("motor"):
        return "motor"
    return "./bd2"


def instalar_comando(silencioso=True):
    """Enlaza `motor` en ~/.local/bin para poder escribirlo desde cualquier sitio.

    No se pone en la raíz del repo porque ahí ya existe la carpeta motor/, y en
    Linux un archivo no puede llamarse igual que una carpeta."""
    if WINDOWS:
        return  # en Windows el lanzador es motor.cmd y ya está en la raíz
    try:
        if DESTINO_MOTOR.is_symlink() and DESTINO_MOTOR.resolve() == ORIGEN_MOTOR.resolve():
            return  # ya apunta a este repo
        DESTINO_MOTOR.parent.mkdir(parents=True, exist_ok=True)
        if DESTINO_MOTOR.exists() or DESTINO_MOTOR.is_symlink():
            if not DESTINO_MOTOR.is_symlink():
                aviso(f"{DESTINO_MOTOR} ya existe y no es un enlace; no se toca.")
                return
            DESTINO_MOTOR.unlink()
        DESTINO_MOTOR.symlink_to(ORIGEN_MOTOR)
    except OSError as error:
        if not silencioso:
            aviso(f"No se pudo instalar el comando `motor`: {error}")
        return

    paso("Comando `motor` instalado")
    detalle(f"{DESTINO_MOTOR} -> tools/bin/motor")
    # en muchas distros ~/.local/bin solo entra al PATH si ya existía al iniciar sesión
    if str(DESTINO_MOTOR.parent) not in os.environ.get("PATH", "").split(os.pathsep):
        aviso("~/.local/bin todavía no está en tu PATH de esta terminal.")
        aviso('Añádelo con:  echo \'export PATH="$HOME/.local/bin:$PATH"\' >> ~/.bashrc')
        aviso("Abre una terminal nueva y ya podrás escribir `motor run` desde cualquier carpeta.")
    else:
        detalle("ya puedes escribir `motor run` desde cualquier carpeta")


def puerto_ocupado(numero):
    """Devuelve el puerto si alguien ya está escuchando ahí, si no None."""
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.3)
        return numero if sock.connect_ex(("127.0.0.1", numero)) == 0 else None


def version_node():
    try:
        salida = subprocess.run(["node", "--version"], capture_output=True, text=True, shell=WINDOWS)
        partes = salida.stdout.strip().lstrip("v").split(".")
        return int(partes[0]), int(partes[1])
    except (OSError, ValueError, IndexError):
        return None


def comprobar_requisitos():
    paso("Comprobando lo que hace falta")
    if sys.version_info < (3, 10):
        morir(f"Python {sys.version_info.major}.{sys.version_info.minor} es muy viejo; hace falta 3.10 o superior.")
    detalle(f"Python {sys.version_info.major}.{sys.version_info.minor} ✓")

    if not shutil.which("node"):
        encontrado = buscar_node()
        if encontrado:
            detalle(f"Node encontrado en {encontrado}")
            detalle("(estaba instalado pero fuera del PATH de esta terminal)")
        else:
            morir("No se encontró Node.js.",
                  "Instálalo desde https://nodejs.org (versión 22.12 o superior).\n"
                  "  Si lo tienes con nvm, abre una terminal nueva o ejecuta:  source ~/.nvm/nvm.sh")
    version = version_node()
    if version and version < (22, 12):
        aviso(f"Node {version[0]}.{version[1]} está por debajo de la 22.12 que pide el proyecto.")
        aviso("Suele funcionar igual, pero si algo falla raro, empieza por actualizar Node.")
    elif version:
        detalle(f"Node {version[0]}.{version[1]} ✓")

    if not shutil.which("npm") and not (WINDOWS and shutil.which("npm.cmd")):
        morir("No se encontró npm.", "Viene con Node.js: https://nodejs.org")

    if not shutil.which("g++"):
        arreglo = ("Instala MinGW-w64 y añádelo al PATH." if WINDOWS
                   else "Linux: sudo apt install g++   ·   macOS: xcode-select --install")
        morir("No se encontró g++, y hace falta para compilar el motor.", arreglo)
    detalle("g++ ✓")


def preparar_python():
    if not PY_VENV.exists():
        paso("Creando el entorno de Python (.venv)")
        if not correr([sys.executable, "-m", "venv", str(VENV)]):
            morir("No se pudo crear el entorno virtual.",
                  "En Debian o Ubuntu puede faltar: sudo apt install python3-venv")
    firma = REQUISITOS.read_text(encoding="utf-8") if REQUISITOS.is_file() else ""
    if SELLO.is_file() and SELLO.read_text(encoding="utf-8") == firma:
        detalle("Dependencias de Python al día")
        return
    paso("Instalando las dependencias de Python")
    if not correr([str(PY_VENV), "-m", "pip", "install", "--quiet", "--disable-pip-version-check",
                   "-r", str(REQUISITOS)]):
        morir("Falló la instalación de las dependencias de Python.")
    SELLO.write_text(firma, encoding="utf-8")


def preparar_node():
    sello = WEB / "node_modules" / ".package-lock.json"
    bloqueo = WEB / "package-lock.json"
    # npm deja ese archivo al instalar; si es más nuevo que el lock, no hay nada que hacer
    if sello.is_file() and bloqueo.is_file() and sello.stat().st_mtime >= bloqueo.stat().st_mtime:
        detalle("Dependencias de npm al día")
        return
    paso("Instalando las dependencias de npm (puede tardar un poco la primera vez)")
    if not correr(["npm", "install"], cwd=WEB):
        morir("Falló npm install.")


def compilar_motor():
    paso("Compilando el motor C++")
    inicio = time.monotonic()
    guion = (
        "import sys; sys.path.insert(0, r'%s');"
        "from api.motor_cli import MotorSQL;"
        "m = MotorSQL(db='datos/db');"
        "print('   binario:', m.binario)" % RAIZ
    )
    if not correr([str(PY_VENV), "-c", guion]):
        morir("No se pudo compilar el motor.", "Revisa que g++ esté en el PATH y que no haya errores de compilación.")
    detalle(f"listo en {time.monotonic() - inicio:.1f} s")


def preparar(mensaje_final=True):
    comprobar_requisitos()
    preparar_python()
    preparar_node()
    compilar_motor()
    instalar_comando()
    if mensaje_final:
        paso("Todo listo")
        detalle(f"Para abrir la web: {lanzador()} run")


def abrir_web():
    ocupado = puerto_ocupado(5173) or puerto_ocupado(8000)
    if ocupado:
        morir(f"El puerto {ocupado} ya está en uso.",
              f"Seguramente hay otro '{lanzador()} run' abierto. Ciérralo con Ctrl + C y vuelve a intentarlo.")
    preparar(mensaje_final=False)
    paso("Abriendo la web en http://127.0.0.1:5173")
    detalle("la API queda en el puerto 8000 · Ctrl + C para cerrar las dos")
    # npm run dev levanta la API y el front; se cede la terminal a ese proceso
    orden = ["npm", "run", "dev"]
    try:
        return 0 if correr(orden, cwd=WEB) else 1
    except KeyboardInterrupt:
        print("\nCerrado.")
        return 0


def correr_pruebas():
    preparar(mensaje_final=False)
    fallos = []

    paso("Pruebas del motor C++")
    if shutil.which("make") or shutil.which("mingw32-make"):
        make = "make" if shutil.which("make") else "mingw32-make"
        if not correr([make, "test"]):
            fallos.append("pruebas del motor")
    else:
        aviso("No se encontró make, se saltan las pruebas del motor C++.")
        aviso("Linux: sudo apt install make · Windows: viene con MinGW-w64 como mingw32-make")

    paso("Pruebas de la interfaz (Playwright)")
    ocupado = puerto_ocupado(5173) or puerto_ocupado(8000)
    if ocupado:
        aviso(f"El puerto {ocupado} está ocupado: las pruebas levantan su propio servidor y no pueden arrancar.")
        aviso(f"Cierra el '{lanzador()} run' que tengas abierto y vuelve a intentarlo.")
        fallos.append("pruebas de la interfaz (puerto ocupado)")
    else:
        # sin --with-deps: esa variante instala paquetes del sistema y pide root
        if not correr(["npx", "playwright", "install", "chromium"], cwd=WEB, callar=True):
            aviso("No se pudo descargar el navegador de Playwright; se intenta con lo que haya instalado.")
        if not correr(["npx", "playwright", "test"], cwd=WEB):
            fallos.append("pruebas de la interfaz")

    if fallos:
        print(f"\n{ROJO}Fallaron: {', '.join(fallos)}{FIN}")
        return 1
    print(f"\n{VERDE}Todas las pruebas pasaron.{FIN}")
    return 0


def correr_bench():
    preparar(mensaje_final=False)
    paso("Comparando B+ agrupado contra heap + B+ no agrupado")
    if not correr([str(PY_VENV), str(RAIZ / "api" / "bench_agrupado.py")]):
        return 1
    if shutil.which("make"):
        paso("Benchmarks de heap, secuencial y B+ agrupado")
        correr(["make", "bench"])
    else:
        aviso("No se encontró make, se saltan los benchmarks de archivos.")
    detalle("resultados en datos/resultados/")
    return 0


def limpiar():
    paso("Borrando lo generado")
    for ruta in (VENV, WEB / "node_modules", RAIZ / ".build", WEB / "dist", WEB / "test-results"):
        if ruta.exists():
            shutil.rmtree(ruta, ignore_errors=True)
            detalle(f"borrado {ruta.relative_to(RAIZ)}")
    detalle("los datos de datos/db no se tocan")
    return 0


def main():
    parser = argparse.ArgumentParser(
        prog=lanzador(),
        description="Prepara el proyecto y abre la web con un solo comando.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "accion",
        nargs="?",
        default="run",
        choices=["run", "setup", "test", "bench", "clean"],
        help="run (por defecto) abre la web; setup solo prepara; test corre las pruebas",
    )
    opciones = parser.parse_args()
    acciones = {
        "run": abrir_web,
        "setup": lambda: (preparar(), 0)[1],
        "test": correr_pruebas,
        "bench": correr_bench,
        "clean": limpiar,
    }
    try:
        return acciones[opciones.accion]()
    except KeyboardInterrupt:
        print("\nInterrumpido.")
        return 130


if __name__ == "__main__":
    sys.exit(main())
