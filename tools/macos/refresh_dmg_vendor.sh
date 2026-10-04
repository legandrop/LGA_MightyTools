#!/bin/bash

# Regenera tools/macos/vendor/ — la copia versionada de dmgbuild y sus dependencias que usa
# create_dmg.sh.
#
# ESTE es el unico paso que necesita red, y NO corre en el deploy: se ejecuta a mano cuando
# se quiere actualizar la version de dmgbuild. El deploy usa siempre la copia versionada.
#
# Los tres paquetes son Python puro (cero binarios compilados, ~268 KB en total) y corren
# con el `python3` que ya trae macOS, asi que la copia funciona en cualquier maquina sin
# instalar nada.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENDOR_DIR="$SCRIPT_DIR/vendor"
PACKAGES=(dmgbuild ds_store mac_alias)

TMP_ENV="$(mktemp -d "${TMPDIR:-/tmp}/dmg_vendor_XXXXXX")"
trap 'rm -rf "$TMP_ENV"' EXIT

# Se instala en un venv DESCARTABLE fuera del repo: lo unico que queda es la copia limpia.
PYBIN="$(command -v python3.13 || command -v python3.12 || command -v python3.11 || command -v python3)"
echo "Instalando dmgbuild en un venv temporal con $PYBIN..."
"$PYBIN" -m venv "$TMP_ENV/env"
"$TMP_ENV/env/bin/pip" install --quiet --upgrade pip dmgbuild

SITE="$("$TMP_ENV/env/bin/python" -c 'import site; print(site.getsitepackages()[0])')"

echo "Copiando a $VENDOR_DIR ..."
# Las licencias (LICENSE-<paquete>.txt, MIT) viven al lado de cada copia y NO salen del venv: se
# conservan. Si una version nueva cambia la licencia, hay que actualizarlas a mano desde el upstream.
LICENSES_BAK="$TMP_ENV/licencias"
mkdir -p "$LICENSES_BAK"
cp "$VENDOR_DIR"/LICENSE-*.txt "$LICENSES_BAK/" 2>/dev/null || true
rm -rf "$VENDOR_DIR"
mkdir -p "$VENDOR_DIR"
cp "$LICENSES_BAK"/LICENSE-*.txt "$VENDOR_DIR/" 2>/dev/null || true
for pkg in "${PACKAGES[@]}"; do
    if [[ ! -d "$SITE/$pkg" ]]; then
        echo "ERROR: no se encontro el paquete '$pkg' en el venv temporal."
        echo "Puede que dmgbuild haya cambiado sus dependencias: revisa PACKAGES en este script."
        exit 1
    fi
    # --exclude __pycache__: los .pyc son de la version de Python que instalo, no sirven en
    # otra maquina y ensucian el diff en cada regeneracion.
    rsync -a --exclude='__pycache__' "$SITE/$pkg" "$VENDOR_DIR/"
done

# Deja anotado que se copio, que si no no hay forma de saber que version esta adentro.
: > "$VENDOR_DIR/VERSIONS.txt"
for pkg in "${PACKAGES[@]}"; do
    ver="$(ls "$SITE" | grep -i "^${pkg}-.*dist-info$" | sed -E "s/^${pkg}-(.*)\.dist-info$/\1/I" | head -1)"
    echo "${pkg}==${ver}" >> "$VENDOR_DIR/VERSIONS.txt"
done

# Que la copia sirva de verdad con el Python del SISTEMA, que es el que va a usar el deploy
# en una maquina limpia. Sin esto, se podria vendorizar una version que solo corre con el
# Python de Homebrew de esta maquina.
echo "Verificando la copia con /usr/bin/python3 ($(/usr/bin/python3 -V 2>&1))..."
if ! PYTHONPATH="$VENDOR_DIR" /usr/bin/python3 -m dmgbuild --help >/dev/null 2>&1; then
    echo "ERROR: la copia vendorizada no corre con el python3 del sistema."
    exit 1
fi

echo ""
echo "vendor/ regenerado — $(du -sh "$VENDOR_DIR" | cut -f1):"
cat "$VENDOR_DIR/VERSIONS.txt"
