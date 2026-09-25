#!/bin/bash
# Motor de compilacion de macOS para LGA Mighty Tools (Debug en build/, Release en build-release/).
# Estructura tomada de LGA_VideoDownloader/compilar.sh. Qt sale de Homebrew (/opt/homebrew), igual
# que en las otras apps LGA de la Mac.
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

QT_PREFIX="/opt/homebrew"
SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)"

# Se reconfigura si falta el cache o si el build type cacheado no es el pedido: si no, pedir Release
# sobre un arbol en Debug compilaria Debug en silencio.
CACHED_TYPE=""
if [ -f CMakeCache.txt ]; then
    CACHED_TYPE="$(grep -E '^CMAKE_BUILD_TYPE:' CMakeCache.txt | cut -d= -f2)"
fi
if [ ! -f CMakeCache.txt ] || [ "$CACHED_TYPE" != "$BUILD_TYPE" ]; then
    echo "Configurando CMake ($BUILD_TYPE)..."
    cmake .. -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
        -DCMAKE_OSX_ARCHITECTURES="arm64" \
        -DCMAKE_OSX_SYSROOT="$SDK_PATH"
fi

echo "Compilando..."
cmake --build . -j "$(sysctl -n hw.ncpu)"
cd "$APP_ROOT"

if [ ! -x "$APP_BIN" ]; then
    echo "ERROR: no se genero $APP_BIN"
    exit 1
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
