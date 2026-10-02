#!/bin/bash
# Deploy de macOS de LGA Mighty Tools: compila Release, arma un bundle AUTOCONTENIDO en deploy/, lo firma
# y lo instala en /Applications. Esquema de LGA_Base_QT_C_Py/deploy.sh (ver su docs/Doc_Deploy_macOS.md).
#
# En mac la app se prueba INSTALADA, nunca desde build/: el inicio con la sesion (SMAppService), la
# asociacion de los .nk, el navegador por defecto y los permisos de macOS (Accesibilidad) dependen de la
# ruta y de la firma de la copia instalada.
#
# Uso: ./deploy.sh [--no-install] [--no-open]
#   --no-install  Deja el bundle en deploy/ sin copiarlo a /Applications
#   --no-open     Instala pero no abre la app al terminar

set -euo pipefail

APP_NAME="LGA Mighty Tools"
INSTALL_DIR="/Applications"
INSTALL=true
OPEN_AFTER=true

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-install) INSTALL=false; shift ;;
        --no-open) OPEN_AFTER=false; shift ;;
        -h|--help) sed -n 2,12p "$0"; exit 0 ;;
        *) echo "Opcion desconocida: $1"; exit 1 ;;
    esac
done

cd "$(dirname "$0")"
APP_VERSION="$(tr -d '\r\n' < VERSION)"
echo "Deploy de $APP_NAME v$APP_VERSION"

# Release en build-release/, un arbol separado del de desarrollo.
bash ./compilar.sh --release --no-run
BUILD_TYPE="$(sed -n 's/^CMAKE_BUILD_TYPE:[^=]*=//p' build-release/CMakeCache.txt | head -1)"
if [ "$BUILD_TYPE" != "Release" ]; then
    echo "ERROR: build-release quedo configurado en '${BUILD_TYPE:-?}', no en Release."
    exit 1
fi

# ditto y no `cp -R`: conserva symlinks, permisos y metadata del bundle.
mkdir -p deploy
rm -rf "deploy/$APP_NAME.app"
ditto "build-release/$APP_NAME.app" "deploy/$APP_NAME.app"

# Frameworks y plugins de Qt adentro del bundle: sin esto el binario apunta al Qt de Homebrew y solo
# arranca en una Mac que lo tenga instalado.
# El macdeployqt del MISMO Qt con el que se compilo (compilar.sh deja CMAKE_PREFIX_PATH en el cache).
QT_USED="$(sed -n 's/^CMAKE_PREFIX_PATH:[^=]*=//p' build-release/CMakeCache.txt | head -1)"
MACDEPLOYQT="$QT_USED/bin/macdeployqt"
if [ ! -x "$MACDEPLOYQT" ]; then
    echo "ERROR: no se encontro $MACDEPLOYQT."
    exit 1
fi
"$MACDEPLOYQT" "deploy/$APP_NAME.app" -verbose=1
# macdeployqt copia todos los plugins de los modulos enlazados (SVG, PDF, teclado virtual...) y deja
# rotos los que no resuelve. La poda deja solo los que la app usa, borra lo que nadie referencia y
# falla si alguna dependencia no resuelve adentro del bundle.
/usr/bin/python3 tools/macos/podar_bundle.py "deploy/$APP_NAME.app"

# La firma va DESPUES de copiar todo adentro: cubre el contenido. Identidad estable si existe (la misma
# de compilar.sh), asi el permiso de Accesibilidad sobrevive a cada version; si no, ad-hoc.
CODESIGN_IDENTITY="${LGA_CODESIGN_IDENTITY:-LGA Code Signing}"
if security find-identity -v -p codesigning 2>/dev/null | grep -q "\"$CODESIGN_IDENTITY\""; then
    echo "Firmando con '$CODESIGN_IDENTITY'..."
    codesign --force --deep --sign "$CODESIGN_IDENTITY" "deploy/$APP_NAME.app"
else
    echo "Firmando ad-hoc (no hay identidad '$CODESIGN_IDENTITY' en el llavero)..."
    codesign --force --deep --sign - "deploy/$APP_NAME.app"
fi
codesign --verify --deep --strict "deploy/$APP_NAME.app"
echo "Bundle listo en deploy/$APP_NAME.app"

if [ "$INSTALL" = "false" ]; then
    exit 0
fi

# Cerrar las copias abiertas antes de pisar la instalada: primero pedirle a la residente que salga sola
# (--quit, como «Quit» del menu, se lleva el icono de la barra), y por las dudas por ruta exacta.
TARGET="$INSTALL_DIR/$APP_NAME.app"
for COPY in "$TARGET" "$PWD/build/$APP_NAME.app"; do
    BIN="$COPY/Contents/MacOS/$APP_NAME"
    if pgrep -f "$BIN" >/dev/null 2>&1; then
        "$BIN" --quit >/dev/null 2>&1 || true
        sleep 1
        pkill -f "$BIN" 2>/dev/null || true
    fi
done

rm -rf "$TARGET"
ditto "deploy/$APP_NAME.app" "$TARGET"
# Que LaunchServices vea la copia nueva (tipos de documento, URLs, icono).
LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
touch "$TARGET"
[ -x "$LSREG" ] && "$LSREG" -f "$TARGET" >/dev/null 2>&1 || true
echo "Instalada en $TARGET"

if [ "$OPEN_AFTER" = "true" ]; then
    # Con `open` y no lanzando el binario: asi macOS atribuye los permisos a la app y no a la terminal.
    open "$TARGET"
    echo "Abierta."
fi
