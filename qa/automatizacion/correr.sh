#!/usr/bin/env bash
# Corre Playwright dentro de un entorno aislado (bwrap): sin directorio personal, sin
# sesiones ni credenciales. Solo se montan esta carpeta, la de ejecuciones y el archivo
# bajo prueba (en solo lectura). Los dominios de red los limita fixtures/regalitos.ts.
#
# Uso:
#   ./correr.sh instalar                 instala las dependencias (sin scripts de instalación)
#   ./correr.sh [opciones de playwright] corre las pruebas y escribe el informe
#   REGALITOS_HTML=/ruta/archivo.html ./correr.sh    prueba otro archivo
#
# Ejemplos: ./correr.sh --grep @P0     ./correr.sh --project=movil     ./correr.sh tests/stock.spec.ts
set -euo pipefail
aqui="$(cd "$(dirname "$0")" && pwd)"
ejecuciones="$(realpath -m "$aqui/../ejecuciones")"; mkdir -p "$ejecuciones"
node_bin="$(dirname "$(readlink -f "$(asdf which node 2>/dev/null || command -v node)")")"
node_raiz="$(dirname "$node_bin")"

archivo="${REGALITOS_HTML:-}"
if [ -z "$archivo" ]; then
  for candidato in "$aqui/../../Regalitos Argentinos.html" \
      "$aqui/../../../../30-39-codigo/31-repositorios/proyecto2-regalitos/Regalitos Argentinos.html"; do
    if [ -f "$candidato" ]; then archivo="$(realpath "$candidato")"; break; fi
  done
fi
[ -f "$archivo" ] || { echo "No se encuentra el archivo bajo prueba; indicar REGALITOS_HTML" >&2; exit 2; }
repo="$(dirname "$archivo")"
version="archivo \`$(basename "$archivo")\`"
if git -C "$repo" rev-parse --short HEAD >/dev/null 2>&1; then
  version="rama \`$(git -C "$repo" branch --show-current)\`, commit \`$(git -C "$repo" rev-parse --short HEAD)\`"
fi

aislado() {
  bwrap --unshare-all --share-net --die-with-parent \
    --ro-bind /usr /usr --ro-bind /opt /opt --ro-bind /etc/fonts /etc/fonts \
    --ro-bind /etc/resolv.conf /etc/resolv.conf --ro-bind /etc/ssl /etc/ssl \
    --ro-bind /etc/ca-certificates /etc/ca-certificates --ro-bind /etc/hosts /etc/hosts \
    --symlink usr/lib /lib --symlink usr/lib /lib64 --symlink usr/bin /bin \
    --proc /proc --dev /dev --tmpfs /tmp --tmpfs /home/aislado --setenv HOME /home/aislado \
    --ro-bind "$node_raiz" "$node_raiz" --setenv PATH "$node_bin:/usr/bin" \
    --bind "$aqui" "$aqui" --bind "$ejecuciones" "$ejecuciones" \
    --ro-bind "$archivo" /bajo-prueba/pagina.html --setenv REGALITOS_HTML /bajo-prueba/pagina.html \
    --setenv CI 1 --chdir "$aqui" "$@"
}

if [ "${1:-}" = instalar ]; then
  aislado npm ci --ignore-scripts --no-audit --no-fund
  exit
fi

estado=0
aislado node node_modules/@playwright/test/cli.js test "$@" || estado=$?
aislado node scripts/informe.mjs "$version"
exit $estado
