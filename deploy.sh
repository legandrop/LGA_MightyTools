#!/bin/bash
# Deploy de macOS de LGA Mighty Tools: compila Release UNIVERSAL (arm64 + x86_64), arma un bundle
# AUTOCONTENIDO en deploy/, lo firma y, segun los flags, lo instala en /Applications o lo empaqueta
# (ZIP de actualizacion, DMG de primera instalacion) y lo publica. Esquema de LGA_Base_QT_C_Py/deploy.sh
# (ver su docs/Doc_Deploy_macOS.md) y de LGA_VideoDownloader/deploy.sh para el empaquetado.
#
# En mac la app se prueba INSTALADA, nunca desde build/: el inicio con la sesion (SMAppService), la
# asociacion de los .nk, el navegador por defecto y los permisos de macOS (Accesibilidad) dependen de la
# ruta y de la firma de la copia instalada.
#
# Uso: ./deploy.sh [--no-install] [--no-open] [--zip] [--dmg] [--install] [--publish [--dry-run] [--replace]]
#   (sin flags)   Compila, arma, firma, instala en /Applications y abre la app
#   --no-install  Deja el bundle en deploy/ sin copiarlo a /Applications
#   --no-open     Instala pero no abre la app al terminar
#   --zip         Arma deploy/LGA_MightyTools_Mac_v<version>.zip (actualizacion) y lo verifica
#   --dmg         Arma deploy/LGA_MightyTools_Mac_v<version>.dmg (primera instalacion)
#   --install     Con --zip/--dmg: ademas instala (por defecto empaquetar NO instala ni abre nada)
#   --publish     Arma el .zip y el .dmg y los sube al release v<version> de legandrop/LGA_MightyTools
#   --dry-run     Con --publish: corre todas las comprobaciones y dice que subiria, sin escribir nada
#   --replace     Con --publish: reemplazar los archivos de macOS si el release ya los tiene

set -euo pipefail

APP_NAME="LGA Mighty Tools"
# El nombre de ARCHIVO de los artefactos va sin espacios porque viaja por URL en los releases.
ARTIFACT_NAME="LGA_MightyTools"
INSTALL_DIR="/Applications"
RELEASE_REPO="legandrop/LGA_MightyTools"
# LGA_GH apunta a otro gh, igual que en el helper de las notas.
GH="${LGA_GH:-gh}"

# SHA-1 del certificado «LGA Code Signing» con el que se firma lo que se PUBLICA. Esta FIJADO a proposito:
# el requisito designado de la app es `identifier + certificado`, y los permisos de macOS de los usuarios
# (Accesibilidad, acceso al disco) estan atados a el. Publicar con otro certificado (o ad-hoc) hace perder
# esos permisos a TODOS los usuarios en la siguiente actualizacion. Cambiarlo es una decision de Lega.
SIGN_CERT_SHA1="9989B6EFE3798349A8747E15774439DB501F6AB2"

INSTALL_DEFAULT=true
OPEN_AFTER=true
EXPLICIT_INSTALL=false
CREATE_ZIP=false
CREATE_DMG=false
PUBLISH=false
DRY_RUN=false
REPLACE=false

usage() {
    sed -n 2,20p "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-install) INSTALL_DEFAULT=false; shift ;;
        --no-open) OPEN_AFTER=false; shift ;;
        --install) EXPLICIT_INSTALL=true; shift ;;
        --zip) CREATE_ZIP=true; shift ;;
        --dmg) CREATE_DMG=true; shift ;;
        --publish) PUBLISH=true; CREATE_ZIP=true; CREATE_DMG=true; shift ;;
        --dry-run) DRY_RUN=true; shift ;;
        --replace) REPLACE=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Opcion desconocida: $1"; exit 1 ;;
    esac
done

PACKAGING=false
if [ "$CREATE_ZIP" = "true" ] || [ "$CREATE_DMG" = "true" ]; then
    PACKAGING=true
fi

# Empaquetar o publicar NO instala ni abre nada, salvo --install explicito (copia instalada de la
# maquina de desarrollo = la que esta usando su dueño). Sin flags de empaquetado, todo igual que siempre.
if [ "$PACKAGING" = "true" ]; then
    INSTALL="$EXPLICIT_INSTALL"
else
    INSTALL="$INSTALL_DEFAULT"
fi
if [ "$EXPLICIT_INSTALL" = "true" ] && [ "$INSTALL_DEFAULT" = "false" ]; then
    echo "ERROR: --install y --no-install se contradicen."
    exit 1
fi
if [ "$EXPLICIT_INSTALL" = "true" ] && [ "$PACKAGING" = "false" ]; then
    echo "ERROR: --install solo tiene sentido con --zip o --dmg (sin ellos ya instala)."
    exit 1
fi
if [ "$PUBLISH" = "true" ] && [ "$EXPLICIT_INSTALL" = "true" ]; then
    echo "ERROR: --publish no se combina con --install: publicar nunca toca /Applications."
    exit 1
fi
if [ "$DRY_RUN" = "true" ] && [ "$PUBLISH" != "true" ]; then
    echo "ERROR: --dry-run solo se usa con --publish (sin el, este script compila e instala de verdad)."
    exit 1
fi
if [ "$REPLACE" = "true" ] && [ "$PUBLISH" != "true" ]; then
    echo "ERROR: --replace solo se usa con --publish."
    exit 1
fi

if [ "$(uname -s)" != "Darwin" ]; then
    echo "ERROR: deploy.sh es de macOS."
    exit 1
fi

cd "$(dirname "$0")"
APP_VERSION="$(tr -d '\r\n' < VERSION)"
if ! [[ "$APP_VERSION" =~ ^[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: VERSION tiene un formato invalido: '$APP_VERSION'"
    exit 1
fi
TAG="v${APP_VERSION}"
ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
DMG_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
EXE_NAME="${ARTIFACT_NAME}_Setup_v${APP_VERSION}.exe"
echo "Deploy de $APP_NAME v$APP_VERSION"

# ==== VERIFICACIONES: inicio

# Todos los Mach-O de un .app (o uno solo) traen arm64 Y x86_64. Un solo binario de una arquitectura
# (un plugin o un framework que macdeployqt copio de otro lado) hace que la app muera en las Mac de esa
# otra arquitectura, y nada lo delata en esta.
validate_universal() {  # <ruta del .app, de una carpeta o de un Mach-O>
    local f info archs has_arm has_x86 checked=0 bad=0
    while IFS= read -r -d '' f; do
        info="$(file -b "$f" 2>/dev/null || true)"
        case "$info" in *Mach-O*) ;; *) continue ;; esac
        checked=$((checked + 1))
        archs="$(lipo -archs "$f" 2>/dev/null || true)"
        # Cada arquitectura por separado: dos patrones pegados en un solo case no encuentran la segunda.
        case " $archs " in *" arm64 "*) has_arm=true ;; *) has_arm=false ;; esac
        case " $archs " in *" x86_64 "*) has_x86=true ;; *) has_x86=false ;; esac
        if [ "$has_arm" != "true" ] || [ "$has_x86" != "true" ]; then
            echo "  No universal: $f :: ${archs:-?}"
            bad=$((bad + 1))
        fi
    done < <(find "$1" -type f -print0)
    if [ "$checked" -eq 0 ]; then
        echo "ERROR: no se encontro ningun Mach-O en $1."
        return 1
    fi
    if [ "$bad" -ne 0 ]; then
        echo "ERROR: $bad de $checked binarios Mach-O no traen las dos arquitecturas (arm64 y x86_64)."
        return 1
    fi
    echo "OK: $checked binarios Mach-O universales (arm64 + x86_64)."
}

# El bundle esta firmado con el certificado FIJADO (SIGN_CERT_SHA1) y no ad-hoc ni con otro. Se mira la
# firma del bundle (requisito designado y autoridad), no solo que la identidad exista en el llavero.
verify_signature_pinned() {  # <.app>
    local app="$1" req leaf info auth
    req="$(codesign -d -r- "$app" 2>&1)" || { echo "ERROR: codesign no pudo leer el requisito de $app"; return 1; }
    info="$(codesign -dvvv "$app" 2>&1)" || { echo "ERROR: codesign no pudo leer la firma de $app"; return 1; }
    if printf '%s\n' "$info" | grep -q '^Signature=adhoc'; then
        echo "ERROR: el bundle esta firmado ad-hoc: no se publica (los usuarios perderian sus permisos de macOS)."
        return 1
    fi
    leaf="$(printf '%s\n' "$req" | sed -n 's/.*certificate leaf = H"\([0-9A-Fa-f]*\)".*/\1/p' | head -1 | tr 'a-f' 'A-F')"
    auth="$(printf '%s\n' "$info" | sed -n 's/^Authority=//p' | head -1)"
    if [ "$leaf" != "$SIGN_CERT_SHA1" ]; then
        echo "ERROR: el certificado del bundle no es el fijado."
        echo "       bundle: ${leaf:-ninguno} (autoridad: ${auth:-ninguna})"
        echo "       fijado: $SIGN_CERT_SHA1 (LGA Code Signing)"
        return 1
    fi
    echo "Firma OK: autoridad '$auth', certificado $leaf (el fijado)."
}

_verify_zip_inner() {  # <carpeta temporal> <zip> <.app de referencia> <exigir certificado fijado>
    local tmp="$1" zip="$2" ref_app="$3" pinned="$4" app src_links zip_links
    app="$tmp/$APP_NAME.app"
    if ! ditto -x -k "$zip" "$tmp"; then
        echo "ERROR: ditto no pudo descomprimir $zip."
        return 1
    fi
    if [ ! -d "$app" ]; then
        echo "ERROR: el ZIP no contiene $APP_NAME.app en su raiz."
        return 1
    fi
    # Un ZIP armado con `zip -r` resuelve los symlinks: cero symlinks y un tamano inflado lo delatan.
    src_links="$(find "$ref_app" -type l | wc -l | tr -d ' ')"
    zip_links="$(find "$app" -type l | wc -l | tr -d ' ')"
    echo "Symlinks: bundle $src_links | ZIP descomprimido $zip_links"
    if [ "$zip_links" -eq 0 ] || [ "$zip_links" != "$src_links" ]; then
        echo "ERROR: el ZIP no conservo los symlinks del bundle (zip -r en vez de ditto?)."
        return 1
    fi
    echo "Tamano: bundle $(du -sh "$ref_app" | cut -f1) | ZIP descomprimido $(du -sh "$app" | cut -f1)"
    if ! codesign --verify --deep --strict "$app"; then
        echo "ERROR: el bundle descomprimido del ZIP no pasa codesign --verify --deep --strict."
        return 1
    fi
    echo "codesign --verify --deep --strict: OK sobre el ZIP descomprimido."
    validate_universal "$app" || return 1
    if [ "$pinned" = "true" ]; then
        verify_signature_pinned "$app" || return 1
    fi
}

# El ZIP es lo que baja el actualizador: se descomprime en un temporal y se comprueba como lo va a
# ver el usuario. Siempre borra el temporal.
verify_zip() {  # <zip> <.app de referencia> <exigir certificado fijado: true|false>
    local tmp rc=0
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/lga_zipcheck_XXXXXX")" || return 1
    _verify_zip_inner "$tmp" "$1" "$2" "$3" || rc=1
    rm -rf "$tmp"
    return "$rc"
}

# ==== VERIFICACIONES: fin

# ==== PUBLICAR: inicio

# Todo lo que ESCRIBE en GitHub pasa por aca. Con --dry-run no ejecuta nada: dice que correria. Las
# lecturas (release view, release download, ls-remote) van directo a "$GH" / git.
gh_write() {
    if [ "$DRY_RUN" = "true" ]; then
        echo "  [dry-run] NO se ejecuta: gh $*"
        return 0
    fi
    "$GH" "$@"
}

# SHA256SUMS del release: las lineas que NO son de esta plataforma, mas las propias al final. Una
# sola linea por nombre de archivo; formato sha256sum ("hash  nombre"), LF. Una linea ilegible
# corta: no se adivina que era. Sin intervalos {64} en las regex: el awk de macOS no siempre los
# entiende.
merge_sha256sums() {  # <SHA256SUMS del release, o /dev/null> <el propio> <salida>
    if ! grep -q . "$2"; then
        echo "ERROR: $2 esta vacio."
        return 1
    fi
    awk '
        { sub(/\r$/, "") }
        length($0) > 66 && substr($0, 1, 64) ~ /^[0-9a-fA-F]+$/ && substr($0, 65, 1) == " " && substr($0, 66, 1) ~ /[ *]/ {
            name = substr($0, 67)
            if (FNR == NR) { if (!(name in own)) { own[name] = 1; mine[++m] = $0 } }
            else if (!(name in own) && !(name in seen)) { seen[name] = 1; keep[++k] = $0 }
            next
        }
        NF { print "ERROR: el SHA256SUMS del release tiene una linea ilegible: " $0 > "/dev/stderr"; bad = 1; exit 1 }
        END { if (bad) exit 1; for (i = 1; i <= k; i++) print keep[i]; for (i = 1; i <= m; i++) print mine[i] }
    ' "$2" "$1" > "$3"
}

# El release v<version> tiene que EXISTIR ya, con el instalador de Windows y su SHA256SUMS: lo crea
# instalador.bat (con el tag). Este script nunca crea ni el release ni el tag: solo suma lo de macOS.
# Corre antes de compilar y otra vez al subir, porque el release puede cambiar en el medio. Corta si el
# release es un borrador, si ya tiene los archivos de macOS (salvo --replace) o si le falta el .exe o
# el SHA256SUMS (fusionar contra nada borraria la linea del .exe).
release_check() {
    local out names draft
    if ! out="$("$GH" release view "$TAG" --repo "$RELEASE_REPO" --json isDraft 2>&1)"; then
        if printf '%s\n' "$out" | grep -qi "not found"; then
            echo "ERROR: el release $TAG no existe en $RELEASE_REPO."
            echo "       Lo crea Windows con instalador.bat (junto con el tag $TAG y el $EXE_NAME)."
            echo "       Este script NO crea releases ni tags: solo suma el .zip y el .dmg de macOS a uno que ya exista."
        else
            echo "ERROR: no se pudo leer el release $TAG de $RELEASE_REPO: $out"
        fi
        return 1
    fi
    draft="$("$GH" release view "$TAG" --repo "$RELEASE_REPO" --json isDraft -q '.isDraft' | tr -d '\r')" \
        || { echo "ERROR: no se pudo leer el release $TAG de $RELEASE_REPO."; return 1; }
    names="$("$GH" release view "$TAG" --repo "$RELEASE_REPO" --json assets -q '.assets[].name' | tr -d '\r')" \
        || { echo "ERROR: no se pudo leer los archivos del release $TAG de $RELEASE_REPO."; return 1; }
    if printf '%s\n' "$draft" | grep -qx 'true'; then
        echo "ERROR: el release $TAG existe como borrador. Publicarlo o borrarlo a mano antes de seguir."
        return 1
    fi
    if ! printf '%s\n' "$names" | grep -qx "$EXE_NAME"; then
        echo "ERROR: el release $TAG existe pero todavia no tiene $EXE_NAME."
        echo "       Windows tiene que terminar de publicar su instalador (instalador.bat) antes de sumar macOS."
        return 1
    fi
    if ! printf '%s\n' "$names" | grep -qx 'SHA256SUMS'; then
        echo "ERROR: el release $TAG tiene el instalador de Windows pero no SHA256SUMS: no se fusiona contra"
        echo "       nada, porque se perderia su linea. Subir primero el SHA256SUMS de Windows."
        return 1
    fi
    if printf '%s\n' "$names" | grep -q "^${ARTIFACT_NAME}_Mac_v" && [ "$REPLACE" != "true" ]; then
        echo "ERROR: el release $TAG ya tiene los archivos de macOS. Para reemplazarlos, correr con --replace."
        return 1
    fi
    echo "Release $TAG de $RELEASE_REPO: existe, con $EXE_NAME y SHA256SUMS."
    return 0
}

PRE_ERRORS=0
pre_fail() {  # <mensaje>: suma un error del control previo y sigue, asi se ven todos juntos
    echo "ERROR: $1"
    PRE_ERRORS=$((PRE_ERRORS + 1))
}

# Todo lo que puede cortar la publicacion se chequea ACA, antes de borrar nada y de compilar. Junta
# TODOS los errores en vez de cortar en el primero. Deja HEAD_SHA, WHATS_NEW_FILE y WHATS_NEW_SH.
publish_preflight() {
    local status branch refs tag_sha syncout
    echo "Comprobaciones previas de la publicacion de $TAG..."

    if ! command -v "$GH" >/dev/null 2>&1 || ! "$GH" auth status >/dev/null 2>&1; then
        pre_fail "--publish necesita gh (GitHub CLI) instalado y con login (gh auth login)."
        GH_OK=false
    else
        GH_OK=true
    fi

    # El release sale de un commit: arbol limpio (con archivos nuevos sin trackear tambien) y rama main.
    status="$(git status --porcelain)"
    if [ -n "$status" ]; then
        pre_fail "hay cambios sin commitear; el release tiene que salir de un commit:"
        printf '%s\n' "$status" | head -20 | sed 's/^/       /'
    fi
    branch="$(git symbolic-ref --short -q HEAD || true)"
    if [ "$branch" != "main" ]; then
        pre_fail "la rama actual es '${branch:-HEAD suelto}', no main: se publica solo desde main."
    fi
    HEAD_SHA="$(git rev-parse HEAD)"

    # HEAD tiene que ser EXACTAMENTE el commit del tag v<version> de origin (el que crea instalador.bat).
    if ! refs="$(git ls-remote origin "refs/tags/$TAG" "refs/tags/$TAG^{}" 2>&1)"; then
        pre_fail "no se pudo leer los tags de origin: $refs"
    else
        tag_sha="$(printf '%s\n' "$refs" | awk -v t="refs/tags/$TAG" '$2 == t "^{}" { p = $1 } $2 == t { d = $1 } END { print (p != "" ? p : d) }')"
        if [ -z "$tag_sha" ]; then
            pre_fail "el tag $TAG no existe en origin (lo crea y pushea instalador.bat de Windows). Este script no crea tags."
        elif [ "$tag_sha" != "$HEAD_SHA" ]; then
            pre_fail "HEAD no es el commit del tag $TAG."
            echo "       HEAD:    $HEAD_SHA"
            echo "       tag $TAG: $tag_sha"
        else
            echo "Tag $TAG en origin: coincide con HEAD (${HEAD_SHA:0:9})."
        fi
    fi

    # VERSION, CMakeLists.txt y el changelog tienen que decir lo mismo.
    if ! syncout="$(sh ./sync_version.sh --check 2>&1)"; then
        pre_fail "la version no esta sincronizada (correr ./sync_version.sh y commitear):"
        printf '%s\n' "$syncout" | sed 's/^/       /'
    fi

    # El certificado fijado tiene que estar en el llavero (la firma final se vuelve a comprobar en el bundle).
    if ! security find-identity -v -p codesigning 2>/dev/null | grep -qi "$SIGN_CERT_SHA1"; then
        pre_fail "el certificado 'LGA Code Signing' fijado ($SIGN_CERT_SHA1) no esta, valido, en el llavero: no se publica con otro."
    fi

    # Notas para el usuario (What's new): la logica vive en LGA_RepoTools (WhatsNew_Mac).
    # LGA_REPOTOOLS apunta a otra copia.
    WHATS_NEW_FILE="$(pwd)/Docs/WhatsNew.md"
    WHATS_NEW_SH="${LGA_REPOTOOLS:-$(pwd)/../LGA_RepoTools}/WhatsNew_Mac/whats_new_release.sh"
    if [ ! -f "$WHATS_NEW_FILE" ]; then
        pre_fail "falta $WHATS_NEW_FILE (las notas para el usuario de v${APP_VERSION}). Sin notas no se publica."
    fi
    if [ ! -f "$WHATS_NEW_SH" ]; then
        pre_fail "no encontre $WHATS_NEW_SH (clonar LGA_RepoTools al lado de este repo o definir LGA_REPOTOOLS)."
    fi

    # El release de Windows ya publicado, con su .exe y su SHA256SUMS.
    if [ "$GH_OK" = "true" ]; then
        release_check || PRE_ERRORS=$((PRE_ERRORS + 1))
    fi

    if [ "$PRE_ERRORS" -gt 0 ]; then
        echo ""
        echo "No se publica: $PRE_ERRORS problema(s) arriba. No se compilo ni se subio nada."
        exit 1
    fi

    # Recien con todo lo demas en orden se corre el control de las notas (puede preguntar).
    echo "Verificando las notas para el usuario (What's new) de v${APP_VERSION}..."
    if ! sh "$WHATS_NEW_SH" check "$WHATS_NEW_FILE" "$APP_VERSION"; then
        echo "ERROR: faltan o fallan las notas de v${APP_VERSION}, o se contesto que no. No se compilo ni se subio nada."
        exit 1
    fi
}

publish_failed() {  # <carpeta temporal>
    echo "ERROR: fallo la publicacion del release $TAG. Ver el mensaje de arriba."
    [ -n "$1" ] && rm -rf "$1"
    return 0
}

# El release v<version> vive en este mismo repo y lo crea Windows. Aca se SUMAN el .zip, el .dmg y
# las lineas de macOS del SHA256SUMS, y al final se publican las notas (cuerpo del release,
# whats_new.json y WHATS_NEW.md en la raiz del repo: eso ultimo es un commit en main hecho por la API,
# asi que despues de publicar hay que hacer `git pull`). Todo lo que puede cortar (leer, fusionar) pasa
# antes de escribir nada. La app no depende del SHA256SUMS: verifica contra el digest del manifiesto.
publish_release() {
    local work old a sums=deploy/release/SHA256SUMS clobber="" lines names
    local assets=()
    for a in "deploy/$ZIP_NAME" "deploy/$DMG_NAME"; do
        [ -f "$a" ] || { echo "ERROR: falta $a"; return 1; }
        assets+=("$a")
    done
    [ "$REPLACE" = "true" ] && clobber="--clobber"

    # Nada cambio desde las comprobaciones previas (la compilacion tarda minutos).
    if [ "$(git rev-parse HEAD)" != "$HEAD_SHA" ] || [ -n "$(git status --porcelain)" ]; then
        echo "ERROR: el repo cambio durante el deploy (HEAD o archivos). No se publica."
        return 1
    fi
    release_check || return 1

    work="$(mktemp -d)" || return 1
    "$GH" release download "$TAG" --repo "$RELEASE_REPO" --pattern SHA256SUMS --dir "$work" \
        || { publish_failed "$work"; return 1; }
    old="$work/SHA256SUMS"
    # El fusionado queda en una ruta fija: si la subida falla (--clobber borra el viejo antes
    # de subir), se resube a mano desde ahi.
    mkdir -p deploy/release
    merge_sha256sums "$old" deploy/SHA256SUMS "$sums" || { publish_failed "$work"; return 1; }
    # La linea del instalador de Windows tiene que sobrevivir a la fusion.
    if ! grep -q "  ${EXE_NAME}\$" "$sums"; then
        echo "ERROR: el SHA256SUMS del release no trae la linea de $EXE_NAME: no se fusiona (se perderia el update de Windows)."
        publish_failed "$work"
        return 1
    fi
    lines="$(wc -l < "$sums" | tr -d ' ')"

    echo ""
    if [ "$DRY_RUN" = "true" ]; then
        echo "=== --dry-run: esto es lo que SE SUBIRIA al release $TAG de $RELEASE_REPO ==="
    else
        echo "Subiendo a $RELEASE_REPO $TAG..."
    fi
    for a in "${assets[@]}"; do
        echo "  $(basename "$a")  ($(du -h "$a" | cut -f1))"
    done
    echo "  SHA256SUMS fusionado: $lines lineas:"
    sed 's/^/    /' "$sums"

    gh_write release upload "$TAG" "${assets[@]}" --repo "$RELEASE_REPO" $clobber \
        || { publish_failed "$work"; return 1; }
    if ! gh_write release upload "$TAG" "$sums" --repo "$RELEASE_REPO" --clobber; then
        echo "ERROR: el .zip y el .dmg subieron, pero SHA256SUMS no: el release puede haber quedado"
        echo "       SIN SHA256SUMS (la app igual verifica contra el manifiesto). El fusionado esta en $(pwd)/$sums."
        echo "       Subirlo con: gh release upload $TAG \"$(pwd)/$sums\" --repo $RELEASE_REPO --clobber"
        rm -rf "$work"
        return 1
    fi
    rm -rf "$work"

    echo "Publicando las notas para el usuario (What's new)..."
    local wn_dry=""
    if [ "$DRY_RUN" = "true" ]; then
        wn_dry="--dry-run"
    fi
    if ! sh "$WHATS_NEW_SH" publish "$WHATS_NEW_FILE" "$APP_VERSION" "$RELEASE_REPO" "$TAG" $wn_dry; then
        echo "ERROR: el release $TAG quedo con el .zip y el .dmg, pero sus notas no. Reintentar con el comando de arriba."
        return 1
    fi

    if [ "$DRY_RUN" = "true" ]; then
        echo "  [dry-run] NO se ejecuta: gh workflow run refresh_versions.yml --repo legandrop/LGA_Updates"
        echo ""
        echo "--dry-run: todas las comprobaciones pasaron. No se subio ni se escribio nada en GitHub."
        return 0
    fi

    # Que el release quede como se espera: el .exe de Windows sigue, y estan el .zip, el .dmg y SHA256SUMS.
    names="$("$GH" release view "$TAG" --repo "$RELEASE_REPO" --json assets -q '.assets[].name' | tr -d '\r')" || names=""
    for a in "$EXE_NAME" "$ZIP_NAME" "$DMG_NAME" SHA256SUMS; do
        printf '%s\n' "$names" | grep -qx "$a" || echo "AVISO: el release $TAG no lista $a despues de subir. Revisarlo."
    done

    # Avisarle al manifiesto de versiones que hay algo nuevo (igual que instalador.bat). Sin esto hay que
    # esperar al cron de legandrop/LGA_Updates. Falla en silencio a proposito: el release ya esta publicado
    # y el cron lo levanta igual. El </dev/null evita que gh pregunte por un input y cuelgue el script.
    if "$GH" workflow run refresh_versions.yml --repo legandrop/LGA_Updates >/dev/null 2>&1 </dev/null; then
        echo "Manifiesto de versiones: refresco disparado."
    else
        echo "AVISO: no se pudo disparar el refresco del manifiesto. El cron lo levanta solo."
    fi
    echo "Release publicado: https://github.com/${RELEASE_REPO}/releases/tag/${TAG}"
}

# ==== PUBLICAR: fin

if [ "$PUBLISH" = "true" ]; then
    publish_preflight
fi

# Release en build-release/, un arbol separado del de desarrollo. Universal (arm64 + x86_64).
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

# El release es universal: el ejecutable y TODOS los Mach-O del bundle (despues de macdeployqt y de la
# poda) tienen que traer arm64 y x86_64. Si no, se corta antes de firmar y de empaquetar.
echo "Arquitecturas del ejecutable: $(lipo -archs "deploy/$APP_NAME.app/Contents/MacOS/$APP_NAME")"
if ! validate_universal "deploy/$APP_NAME.app"; then
    echo "ERROR: el bundle no es universal; no se firma ni se empaqueta."
    exit 1
fi

# La firma va DESPUES de copiar todo adentro: cubre el contenido. Identidad estable si existe (la misma
# de compilar.sh), asi el permiso de Accesibilidad sobrevive a cada version; si no, ad-hoc. Para PUBLICAR
# se firma con el certificado FIJADO (por su SHA-1) y no hay alternativa.
if [ "$PUBLISH" = "true" ]; then
    echo "Firmando con el certificado fijado $SIGN_CERT_SHA1 (LGA Code Signing)..."
    codesign --force --deep --sign "$SIGN_CERT_SHA1" "deploy/$APP_NAME.app"
else
    CODESIGN_IDENTITY="${LGA_CODESIGN_IDENTITY:-LGA Code Signing}"
    if security find-identity -v -p codesigning 2>/dev/null | grep -q "\"$CODESIGN_IDENTITY\""; then
        echo "Firmando con '$CODESIGN_IDENTITY'..."
        codesign --force --deep --sign "$CODESIGN_IDENTITY" "deploy/$APP_NAME.app"
    else
        echo "Firmando ad-hoc (no hay identidad '$CODESIGN_IDENTITY' en el llavero)..."
        codesign --force --deep --sign - "deploy/$APP_NAME.app"
    fi
fi
codesign --verify --deep --strict "deploy/$APP_NAME.app"
if [ "$PUBLISH" = "true" ]; then
    verify_signature_pinned "deploy/$APP_NAME.app" || { echo "ERROR: no se publica con esta firma."; exit 1; }
fi
echo "Bundle listo en deploy/$APP_NAME.app"

# ---- Empaquetado (--zip / --dmg / --publish): arma desde el bundle YA firmado ----
if [ "$PACKAGING" = "true" ]; then
    rm -f deploy/SHA256SUMS
    rm -rf deploy/release
fi

if [ "$CREATE_ZIP" = "true" ]; then
    # ditto y NO zip: `zip -r` RESUELVE los symlinks en vez de guardarlos, y un .app de Qt esta lleno
    # (Versions/Current, el binario de cada framework): el bundle llega mas pesado y con la firma invalida.
    rm -f "deploy/$ZIP_NAME"
    if ! (cd deploy && ditto -c -k --sequesterRsrc --keepParent "$APP_NAME.app" "$ZIP_NAME"); then
        echo "ERROR: no se pudo crear el .zip. No se publica nada."
        exit 1
    fi
    echo "ZIP creado: deploy/$ZIP_NAME ($(du -h "deploy/$ZIP_NAME" | cut -f1))"
    # Se comprueba como lo va a ver el usuario: descomprimido, con symlinks, firma y arquitecturas.
    if ! verify_zip "deploy/$ZIP_NAME" "deploy/$APP_NAME.app" "$PUBLISH"; then
        echo "ERROR: el ZIP no paso la verificacion. No se publica nada."
        exit 1
    fi
fi

# El DMG es el artefacto de PRIMERA INSTALACION; el ZIP de arriba es el de actualizacion y los dos
# no son intercambiables.
if [ "$CREATE_DMG" = "true" ]; then
    rm -f "deploy/$DMG_NAME"
    if ! bash ./create_dmg.sh --no-open; then
        echo "ERROR: no se pudo crear el .dmg. No se publica nada."
        exit 1
    fi
fi

# SHA256SUMS de macOS: el auto-update no instala nada sin su hash. Formato sha256sum ("hash  nombre").
# Al publicar, el SHA256SUMS del release lleva TAMBIEN la linea del instalador de Windows:
# publish_release las fusiona.
if [ "$PACKAGING" = "true" ]; then
    (
        cd deploy
        for artifact in "$ZIP_NAME" "$DMG_NAME"; do
            if [ -f "$artifact" ]; then
                shasum -a 256 "$artifact" >> SHA256SUMS
            fi
        done
    )
    echo "SHA256SUMS creado: deploy/SHA256SUMS"
fi

if [ "$PUBLISH" = "true" ]; then
    publish_release || exit 1
fi

# ---- Instalacion: solo sin flags de empaquetado, o con --install explicito ----
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
