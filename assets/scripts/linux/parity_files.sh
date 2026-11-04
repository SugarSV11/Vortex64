#!/bin/bash
# Genera PROJECT64_PARITY_FILES.md: inventario de archivos de Source/ que
# difieren de upstream/develop. La clase y la razón salen de
# Scripts/linux/parity_classes.tsv (primera regla que coincide); un archivo
# sin regla queda como "?" y hay que revisarlo a mano.
cd "$(dirname "$0")/../.." || exit 1
UP=${1:-upstream/develop}
RULES=Scripts/linux/parity_classes.tsv
mapfile -t PATTERNS < <(grep -v '^#' "$RULES" | cut -f1)
mapfile -t CLASSES < <(grep -v '^#' "$RULES" | cut -f2)
mapfile -t REASONS < <(grep -v '^#' "$RULES" | cut -f3)
echo "# Inventario de diferencias contra $(git rev-parse --short "$UP")"
echo
echo "Clases: A idéntico · B cambio necesario en Linux · C sin portar / stub de upstream · D divergencia sin justificación · E no aplicable"
echo "Generado por Scripts/linux/parity_files.sh a partir de Scripts/linux/parity_classes.tsv."
echo
echo "| Estado | +/- | Archivo | Clase | Razón |"
echo "|---|---|---|---|---|"
git diff --numstat "$UP" -- Source | while IFS=$'\t' read -r a d f; do
  if git cat-file -e "$UP:$f" 2>/dev/null; then s=M; else s=A; fi
  c='?'; r='sin clasificar'
  for i in "${!PATTERNS[@]}"; do
    if [[ $f == ${PATTERNS[$i]} ]]; then c=${CLASSES[$i]}; r=${REASONS[$i]}; break; fi
  done
  echo "| $s | +$a/-$d | \`$f\` | $c | $r |"
done | sort -t'|' -k4
echo
echo "Archivos de upstream ausentes en Vortex64 (clase C/E):"
git diff --name-status "$UP" -- Source | awk '$1=="D"{print "- `"$2"`"}'
