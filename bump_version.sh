#!/bin/sh
# Bump de version de LGA Mighty Tools para publicar un release. Lo corre Lega cuando decide publicar:
# nunca se corre solo ni en cada cambio.
#
# Los dos numeros significan cosas distintas:
#   - CMakeLists.txt (y VERSION, que lo copia): la ultima version PUBLICADA.
#   - El primer "vX.YY:" de Docs/Changelog.md: la version EN DESARROLLO, donde se juntan las entradas
#     hasta el proximo release. Que el changelog vaya adelante es el estado normal.
# El bump sube CMakeLists.txt y VERSION a la version del changelog. Es UNO solo para Windows y macOS: las
# dos plataformas publican en el mismo release v<version>, en cualquier orden.
#
# Misma logica que tools/bump_version.ps1 (el de Windows), en sh y sin Python. La version va de a
# centesimos (1.52, 1.53...): con otro formato el script falla.
#
# Uso: ./bump_version.sh [--check] [--commit|--no-commit] [--publish|--no-publish]
#   (sin flags)   Sube la version y pregunta si commitear y pushear, y despues si publicar
#   --check       No escribe nada: sale 0 si CMakeLists.txt, VERSION y el changelog dicen lo mismo (listo
#                 para publicar) y 1 si no. Lo usa deploy.sh --publish.
#   --commit      Commitea y pushea el bump sin preguntar
#   --no-commit   Deja el bump sin commitear y no pregunta (tampoco ofrece publicar)
#   --publish     Despues del push publica sin preguntar: ./deploy.sh --publish (archivos de macOS)
#   --no-publish  No publica ni pregunta
# Sin terminal, toda pregunta se contesta que no.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CHANGELOG="$ROOT/Docs/Changelog.md"
CMAKE="$ROOT/CMakeLists.txt"
VERSION_FILE="$ROOT/VERSION"
EXPECTED_EMAIL="176236735+legandrop@users.noreply.github.com"

CHECK=false
COMMIT_MODE=ask
PUBLISH_MODE=ask
for arg in "$@"; do
    case "$arg" in
        --check) CHECK=true ;;
        --commit) COMMIT_MODE=yes ;;
        --no-commit) COMMIT_MODE=no ;;
        --publish) PUBLISH_MODE=yes ;;
        --no-publish) PUBLISH_MODE=no ;;
        -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "[bump_version] ERROR: flag desconocido: $arg"; exit 2 ;;
    esac
done

fail() { echo "[bump_version] ERROR: $*"; exit 1; }

# Pregunta Y/N con una sola tecla, como `choice /C YN` en Windows. Sin terminal contesta que no.
STTY_SAVED=""
restore_tty() {
    if [ -n "$STTY_SAVED" ]; then stty "$STTY_SAVED" 2>/dev/null || true; fi
    STTY_SAVED=""
}
trap 'restore_tty' EXIT
trap 'restore_tty; echo; exit 130' INT TERM
ask() {
    if [ ! -t 0 ]; then
        echo "$1 [Y,N]? N (sin terminal)"
        return 1
    fi
    while :; do
        printf '%s [Y,N]? ' "$1"
        STTY_SAVED=$(stty -g)
        stty -icanon -echo min 1
        key=$(dd bs=1 count=1 2>/dev/null)
        restore_tty
        case "$key" in
            y|Y|s|S) echo "Y"; return 0 ;;
            n|N) echo "N"; return 1 ;;
            *) echo ;;
        esac
    done
}

CL_VERSION=$(grep -m1 -E '^v[0-9]+\.[0-9]+[[:space:]]*:' "$CHANGELOG" | sed -E 's/^v([0-9]+\.[0-9]+).*/\1/')
CM_VERSION=$(grep -m1 -E 'project\([[:space:]]*LGA_MightyTools[[:space:]]+VERSION' "$CMAKE" \
    | sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/')
FILE_VERSION=""
[ -f "$VERSION_FILE" ] && FILE_VERSION=$(tr -d '[:space:]' < "$VERSION_FILE")

for v in "$CL_VERSION" "$CM_VERSION"; do
    echo "$v" | grep -qE '^[0-9]+\.[0-9]{2}$' \
        || fail "la version '$v' no tiene dos decimales (se numera 1.52, 1.53...)"
done
key() { echo "$1" | awk -F. '{ print $1 * 1000 + $2 }'; }

echo "[bump_version] Publicada (CMakeLists.txt): $CM_VERSION | En desarrollo (changelog): $CL_VERSION | VERSION: ${FILE_VERSION:-falta}"

if [ "$CHECK" = "true" ]; then
    if [ "$CM_VERSION" = "$CL_VERSION" ] && [ "$FILE_VERSION" = "$CL_VERSION" ]; then
        echo "[bump_version] OK: todo en $CL_VERSION."
        exit 0
    fi
    echo "[bump_version] ERROR: CMakeLists.txt, VERSION y el changelog no dicen lo mismo: falta correr ./bump_version.sh y commitear."
    exit 1
fi

# Antes de escribir nada, si se va a commitear: el bump sale de main al dia con origin (si la otra
# maquina ya bumpeo y publico, aca falta un pull) y del changelog commiteado (si no, se pushea un main
# donde CMakeLists.txt y el changelog no coinciden).
if [ "$COMMIT_MODE" != "no" ]; then
    branch=$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)
    [ "$branch" = "main" ] || fail "la rama es '$branch': el bump se hace en main."
    git -C "$ROOT" fetch -q origin main || fail "no se pudo leer origin/main."
    git -C "$ROOT" merge-base --is-ancestor origin/main HEAD \
        || fail "main esta atras de origin/main: hacer git pull antes del bump."
    git -C "$ROOT" diff --quiet HEAD -- Docs/Changelog.md \
        || fail "Docs/Changelog.md tiene cambios sin commitear: commitearlos antes del bump."
fi

[ "$(key "$CL_VERSION")" -ge "$(key "$CM_VERSION")" ] \
    || fail "el changelog (v$CL_VERSION) esta por debajo de la version publicada ($CM_VERSION). Revisarlo a mano."

if [ "$CL_VERSION" = "$CM_VERSION" ]; then
    echo "[bump_version] CMakeLists.txt ya esta en $CM_VERSION: no hay version nueva que subir."
    echo "               Para un release nuevo, el changelog tiene que abrir v<siguiente>: con sus entradas."
else
    NEXT=$(echo "$CM_VERSION" | awk -F. '{ n = $1 * 100 + $2 + 1; printf "%d.%02d", int(n / 100), n % 100 }')
    if [ "$CL_VERSION" != "$NEXT" ]; then
        echo "[bump_version] AVISO: el changelog salta de $CM_VERSION a $CL_VERSION (lo esperable era $NEXT)."
        ask "Subir igual a $CL_VERSION" || fail "bump cancelado."
    fi
    # Solo la linea project(...) del CMake. `sed -i.bak` y borrar el .bak: es la forma que anda igual en
    # el sed de macOS y en el de GNU.
    sed -i.bak -E "s/(project\([[:space:]]*LGA_MightyTools[[:space:]]+VERSION[[:space:]]+)[0-9]+\.[0-9]+/\1$CL_VERSION/" "$CMAKE"
    rm -f "$CMAKE.bak"
    echo "[bump_version] Bump: $CM_VERSION -> $CL_VERSION"
fi
if [ "$FILE_VERSION" != "$CL_VERSION" ]; then
    printf '%s\n' "$CL_VERSION" > "$VERSION_FILE"
fi
VERSION="$CL_VERSION"

# ---- Commit y push: SOLO los archivos del bump, nunca un `git add -A` que arrastre trabajo en curso.
[ "$COMMIT_MODE" = "no" ] && { echo "[bump_version] Sin commit: los cambios quedan en el arbol."; exit 0; }
cd "$ROOT"
CHANGED=$(git status --porcelain -- CMakeLists.txt VERSION | sed -E 's/^.{3}//')
if [ -n "$CHANGED" ]; then
    echo "[bump_version] Archivos del bump:"
    printf '  %s\n' $CHANGED
    if [ "$COMMIT_MODE" = "ask" ] && ! ask "Commitear y pushear el bump a v$VERSION"; then
        echo "[bump_version] Commit omitido: los cambios quedan en el arbol."
        exit 0
    fi
    # GitHub atribuye por email: con otra identidad el commit queda fuera de la cuenta.
    email=$(git config user.email || true)
    [ "$email" = "$EXPECTED_EMAIL" ] || fail "git user.email es '$email' y se esperaba '$EXPECTED_EMAIL'. Commit cancelado."
    git add -- $CHANGED
    git commit -q -m "Version: bump a v$VERSION" -- $CHANGED || fail "fallo el commit; los cambios quedan stageados."
    git log -1 --format='[bump_version] Commit %h: %s'
    git push -q || fail "fallo el push; el commit queda local. Revisar credenciales y reintentar a mano."
    echo "[bump_version] Pusheado."
else
    echo "[bump_version] No hay cambios de version para commitear."
fi

# ---- Publicar: el release v<version> es uno solo para las dos plataformas. Desde la Mac se suben los
# archivos de macOS; el instalador de Windows lo publica instalador.bat (bump_version.bat lo ofrece).
[ "$PUBLISH_MODE" = "no" ] && exit 0
if [ "$(uname -s)" != "Darwin" ]; then
    echo "[bump_version] Publicar desde aca no aplica: en Windows es bump_version.bat (instalador.bat)."
    exit 0
fi
if [ "$PUBLISH_MODE" = "ask" ] && ! ask "Publicar los archivos de macOS en el release v$VERSION (./deploy.sh --publish)"; then
    echo "[bump_version] Publicacion omitida. Para hacerla despues: ./deploy.sh --publish"
    exit 0
fi
exec "$ROOT/deploy.sh" --publish
