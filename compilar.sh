#!/bin/bash
# Motor de compilacion de macOS para LGA Mighty Tools (Debug en build/, Release en build-release/).
# Estructura tomada de LGA_VideoDownloader/compilar.sh. Qt: el oficial 6.5.3 (ver QT_PREFIX abajo).
#
# CONVENCION LGA: la app se lanza en BACKGROUND y el script termina enseguida. Dejarla en
# foreground retiene la terminal hasta que alguien la cierre a mano. --wait recupera el foreground.

set -e

APP_NAME="LGA Mighty Tools"
BUILD_TYPE="Debug"
BUILD_DIR="build"
NO_RUN=false
WAIT_FOR_APP=false
SIM_SLOW=false
FORCE_CLEAN=false

show_help() {
    echo "Uso: $0 [--release] [--no-run] [--wait] [--sim-slow] [--force-clean]"
    echo "  --release      Compila Release en build-release/ (lo que se publica)"
    echo "  --no-run       Compila sin lanzar la app (toda corrida automatizada)"
    echo "  --wait         Deja la app en foreground para ver su salida y su exit code"
    echo "  --sim-slow     Lanza la app degradada (QoS background, I/O throttled)"
    echo "  --force-clean  Borra el arbol de build antes de compilar"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release) BUILD_TYPE="Release"; BUILD_DIR="build-release"; shift ;;
        --no-run) NO_RUN=true; shift ;;
        --wait) WAIT_FOR_APP=true; shift ;;
        --sim-slow) SIM_SLOW=true; shift ;;
        --force-clean) FORCE_CLEAN=true; shift ;;
        --help) show_help; exit 0 ;;
        *) echo "Opcion desconocida: $1"; show_help; exit 1 ;;
    esac
done

cd "$(dirname "$0")"
APP_ROOT="$(pwd)"
APP_BUNDLE="$APP_ROOT/$BUILD_DIR/$APP_NAME.app"
APP_BIN="$APP_BUNDLE/Contents/MacOS/$APP_NAME"

# Instancia unica. Sin --no-run se cierran TODAS las copias de la app (por el ejecutable dentro del
# bundle, nunca por un patron generico); con --no-run, solo la copia que se va a pisar.
if [ "$NO_RUN" = "true" ]; then
    pkill -f "$APP_BIN" 2>/dev/null && echo "   - Copia de $BUILD_DIR cerrada" || true
else
    pkill -f "$APP_NAME.app/Contents/MacOS/$APP_NAME" 2>/dev/null && echo "   - $APP_NAME cerrada" || true
fi
sleep 1

if [ "$FORCE_CLEAN" = "true" ]; then
    echo "Limpiando $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Qt oficial 6.5.3 (~/Qt/6.5.3/macos), el mismo de Windows y del CI de mac: su macdeployqt arma un
# bundle autocontenido. El de Homebrew deja afuera dependencias reales (QtDBus, brotli) que siguen
# apuntando a /opt/homebrew. Homebrew queda de respaldo si no esta el oficial; QT_PREFIX lo cambia.
if [ -z "${QT_PREFIX:-}" ]; then
    if [ -f "$HOME/Qt/6.5.3/macos/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        QT_PREFIX="$HOME/Qt/6.5.3/macos"
    else
        QT_PREFIX="/opt/homebrew"
    fi
fi
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"

# Arquitecturas: Release (lo que se publica) es UNIVERSAL, arm64 + x86_64: el Qt oficial es universal y
# las Mac Intel de los usuarios tambien actualizan. Debug (desarrollo) solo arm64, que compila la mitad
# de rapido. Cambiar el conjunto sobre un arbol ya compilado mezcla objetos de otra arquitectura: se
# reconfigura de cero (ver mas abajo).
if [ "$BUILD_TYPE" = "Release" ]; then
    OSX_ARCHS="arm64;x86_64"
else
    OSX_ARCHS="arm64"
fi

# Se reconfigura si falta el cache o si el build type cacheado no es el pedido: si no, pedir Release
# sobre un arbol en Debug compilaria Debug en silencio.
CACHED_TYPE=""
CACHED_PREFIX=""
CACHED_ARCHS=""
if [ -f CMakeCache.txt ]; then
    CACHED_TYPE="$(grep -E '^CMAKE_BUILD_TYPE:' CMakeCache.txt | cut -d= -f2)"
    CACHED_PREFIX="$(grep -E '^CMAKE_PREFIX_PATH:' CMakeCache.txt | cut -d= -f2)"
    CACHED_ARCHS="$(grep -E '^CMAKE_OSX_ARCHITECTURES:' CMakeCache.txt | cut -d= -f2)"
fi
# Cambiar de Qt sobre un arbol ya configurado mezcla los dos: se empieza de cero.
if [ -f CMakeCache.txt ] && [ "$CACHED_PREFIX" != "$QT_PREFIX" ]; then
    echo "El arbol estaba configurado con otro Qt ($CACHED_PREFIX): se limpia."
    cd "$APP_ROOT"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
fi
# Lo mismo con las arquitecturas: un arbol de otra arquitectura (el Release de antes era solo arm64) se
# borra entero, no se reaprovecha.
if [ -f CMakeCache.txt ] && [ "$CACHED_ARCHS" != "$OSX_ARCHS" ]; then
    echo "El arbol estaba configurado para otras arquitecturas ($CACHED_ARCHS): se limpia."
    cd "$APP_ROOT"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
fi
if [ ! -f CMakeCache.txt ] || [ "$CACHED_TYPE" != "$BUILD_TYPE" ]; then
    echo "Configurando CMake ($BUILD_TYPE)..."
    cmake .. -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DCMAKE_OSX_ARCHITECTURES="$OSX_ARCHS" \
        -DCMAKE_OSX_SYSROOT="$SDK_PATH"
fi

echo "Compilando..."
cmake --build . -j "$(sysctl -n hw.ncpu)"
cd "$APP_ROOT"

if [ ! -x "$APP_BIN" ]; then
    echo "ERROR: no se genero $APP_BIN"
    exit 1
fi

# Firma con una identidad ESTABLE, si existe en el llavero (bloque de LGA_Base_QT_C_Py/compilar.sh).
# Con la firma del linker la identidad del bundle para los permisos de macOS (TCC: Accesibilidad,
# Automatizacion) es el cdhash, que cambia en CADA compilacion: el permiso concedido deja de coincidir
# y Nuke Shortcuts y Folder Switch lo vuelven a pedir despues de cada build. Con un certificado de Code
# Signing (alcanza el self-signed "LGA Code Signing", ver LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md) el
# requisito pasa a ser identifier + certificado y el permiso sobrevive. Sin identidad no se firma.
CODESIGN_IDENTITY="${LGA_CODESIGN_IDENTITY:-LGA Code Signing}"
if security find-identity -v -p codesigning 2>/dev/null | grep -q "\"$CODESIGN_IDENTITY\""; then
    if codesign --force --deep --sign "$CODESIGN_IDENTITY" "$APP_BUNDLE" 2>/dev/null; then
        echo "Firmado con la identidad '$CODESIGN_IDENTITY' (permisos de macOS estables entre builds)."
    else
        echo "AVISO: no se pudo firmar con '$CODESIGN_IDENTITY'; queda la firma del linker."
    fi
fi

# Refrescar el cache de iconos del bundle (Dock/Finder pueden seguir mostrando el viejo).
LSREG="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
touch "$APP_BUNDLE"
[ -x "$LSREG" ] && "$LSREG" -f "$APP_BUNDLE" >/dev/null 2>&1 || true

echo "Compilacion completada."
if [ "$NO_RUN" = "true" ]; then
    echo "Ejecucion omitida (--no-run)."
    exit 0
fi

echo "Iniciando $APP_NAME..."
if [ "$SIM_SLOW" = "true" ]; then
    echo "   --sim-slow: QoS background e I/O throttled."
    taskpolicy -c background -d throttle "$APP_BIN" >/dev/null 2>&1 &
    disown
elif [ "$WAIT_FOR_APP" = "true" ]; then
    "$APP_BIN"
else
    "$APP_BIN" >/dev/null 2>&1 &
    disown
    echo "   PID $! (background)."
    echo "   Usa --wait si necesitas ver su salida o su exit code en la terminal."
fi
