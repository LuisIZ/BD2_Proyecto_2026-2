#!/usr/bin/env bash
# Exporta cada diagrama TikZ del informe a PNG para las diapositivas.
#   bash docs/informe/diagramas/exportar.sh
# Requiere pdflatex (MiKTeX o TeX Live) y pdftoppm (poppler).
# Salida: docs/presentacion/diagramas/<nombre>.png a 200 dpi.
set -euo pipefail
AQUI="$(cd "$(dirname "$0")" && pwd)"
SALIDA="$AQUI/../../presentacion/diagramas"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$SALIDA"
for f in "$AQUI"/*.tex; do
  nombre="$(basename "$f" .tex)"
  ruta="$(cygpath -m "$f" 2>/dev/null || echo "$f")"   # en Windows, pdflatex espera C:/...
  cat > "$TMP/$nombre.tex" <<EOF
\\documentclass[border=6pt]{standalone}
\\usepackage[utf8]{inputenc}
\\usepackage[T1]{fontenc}
\\usepackage[spanish]{babel}
\\usepackage{lmodern}
\\usepackage{amsmath}
\\usepackage{xcolor}
\\usepackage{tikz}
\\usetikzlibrary{positioning,arrows.meta,calc,shapes.geometric,babel}
\\begin{document}
\\input{$ruta}
\\end{document}
EOF
  (cd "$TMP" && pdflatex -interaction=nonstopmode -halt-on-error "$nombre.tex" > /dev/null) \
    || { echo "error al compilar $nombre (ver $TMP/$nombre.log)"; exit 1; }
  pdftoppm -png -r 200 -singlefile "$TMP/$nombre.pdf" "$SALIDA/$nombre"
  echo "  $SALIDA/$nombre.png"
done
