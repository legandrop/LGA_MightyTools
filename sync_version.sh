#!/bin/sh
# Sincroniza la version del repo: toma la mas alta entre CMakeLists.txt y el primer "vX.YY:" de
# Docs/Changelog.md y la escribe en los tres lugares (CMakeLists.txt, el changelog y VERSION).
# Misma logica que tools/sync_version.ps1 (el de Windows), en sh y sin Python.
#
# Orden: PRIMERO la entrada nueva del changelog con su encabezado "vX.YY:", DESPUES este script.
# La version va de a centesimos (2.01, 2.02...): con otro formato el script falla.
#
# Uso: ./sync_version.sh          propaga
#      ./sync_version.sh --check  no escribe; sale 1 si algo difiere
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CHANGELOG="$ROOT/Docs/Changelog.md"
CMAKE="$ROOT/CMakeLists.txt"
VERSION_FILE="$ROOT/VERSION"
CHECK=false
[ "${1:-}" = "--check" ] && CHECK=true

CL_VERSION=$(grep -m1 -E '^v[0-9]+\.[0-9]+[[:space:]]*:' "$CHANGELOG" | sed -E 's/^v([0-9]+\.[0-9]+).*/\1/')
CM_VERSION=$(grep -m1 -E 'project\([[:space:]]*LGA_MightyTools[[:space:]]+VERSION' "$CMAKE" \
    | sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+).*/\1/')

for v in "$CL_VERSION" "$CM_VERSION"; do
    if ! echo "$v" | grep -qE '^[0-9]+\.[0-9]{2}$'; then
        echo "[sync_version] ERROR: la version '$v' no tiene dos decimales (se numera 2.01, 2.02...)"
        exit 1
    fi
done

key() { echo "$1" | awk -F. '{ print $1 * 1000 + $2 }'; }
if [ "$(key "$CL_VERSION")" -ge "$(key "$CM_VERSION")" ]; then RESOLVED="$CL_VERSION"; else RESOLVED="$CM_VERSION"; fi
CURRENT_FILE=""
[ -f "$VERSION_FILE" ] && CURRENT_FILE=$(tr -d '[:space:]' < "$VERSION_FILE")

if [ "$CHECK" = "true" ]; then
    FAIL=0
    [ "$CM_VERSION" = "$RESOLVED" ] || { echo "    CMakeLists.txt ($CM_VERSION)"; FAIL=1; }
    [ "$CL_VERSION" = "$RESOLVED" ] || { echo "    Docs/Changelog.md ($CL_VERSION)"; FAIL=1; }
    [ "$CURRENT_FILE" = "$RESOLVED" ] || { echo "    VERSION (${CURRENT_FILE:-falta})"; FAIL=1; }
    if [ "$FAIL" = "1" ]; then
        echo "[sync_version] ERROR: desincronizado contra $RESOLVED."
        exit 1
    fi
    echo "[sync_version] OK: todo en $RESOLVED."
    exit 0
fi

# Solo el PRIMER encabezado del changelog y la linea project(...) del CMake. `sed -i.bak` y borrar el
# .bak: es la forma que anda igual en el sed de macOS y en el de GNU.
awk -v r="$RESOLVED" 'done == 0 && /^v[0-9]+\.[0-9]+[[:space:]]*:/ { print "v" r ":"; done = 1; next } { print }' \
    "$CHANGELOG" > "$CHANGELOG.tmp" && mv "$CHANGELOG.tmp" "$CHANGELOG"
sed -i.bak -E "s/(project\([[:space:]]*LGA_MightyTools[[:space:]]+VERSION[[:space:]]+)[0-9]+\.[0-9]+/\1$RESOLVED/" "$CMAKE"
rm -f "$CMAKE.bak"
printf '%s\n' "$RESOLVED" > "$VERSION_FILE"

echo "[sync_version] CMake: $CM_VERSION | Changelog: $CL_VERSION | Resuelta: $RESOLVED"
echo "[sync_version] Sincronizados: CMakeLists.txt, Docs/Changelog.md, VERSION"
