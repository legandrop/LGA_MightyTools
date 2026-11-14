#!/bin/bash
# Deploy de macOS de LGA Mighty Tools: compila Release UNIVERSAL (arm64 + x86_64), arma un bundle
# AUTOCONTENIDO en deploy/, lo firma, segun los flags lo empaqueta (ZIP de actualizacion, DMG de
# primera instalacion), lo notariza con Apple y lo publica, y al final lo instala en /Applications y lo
# abre. Esquema de LGA_Base_QT_C_Py/deploy.sh
# (ver su docs/Doc_Deploy_macOS.md) y de LGA_VideoDownloader/deploy.sh para el empaquetado.
#
# En mac la app se prueba INSTALADA, nunca desde build/: el inicio con la sesion (SMAppService), la
# asociacion de los .nk, el navegador por defecto y los permisos de macOS (Accesibilidad) dependen de la
# ruta y de la firma de la copia instalada.
#
# Uso: ./deploy.sh [--no-install] [--no-open] [--zip] [--dmg] [--no-notarize] [--publish [--dry-run] [--replace] [--yes] | --no-publish]
#   (sin flags)   Con terminal mira GitHub y pregunta primero, con una tecla: publicar (o sumar macOS al
#                 release), el bump si esa version ya esta publicada o falta, build local o cancelar;
#                 despues compila, arma, firma, instala en /Applications y abre la app
#   --no-install  Deja el bundle en deploy/ sin copiarlo a /Applications
#   --no-open     Instala pero no abre la app al terminar
#   --zip         Arma deploy/LGA_MightyTools_Mac_v<version>.zip (actualizacion) y lo verifica
#   --dmg         Arma deploy/LGA_MightyTools_Mac_v<version>.dmg (primera instalacion)
#   --no-notarize No manda a notarizar el .app ni el DMG (salen pidiendo el xattr); LGA_NOTARIZE=0 igual
#   --install     Sin efecto: instalar y abrir es lo de siempre, tambien al empaquetar o publicar
#   --publish     Arma el .zip y el .dmg y los publica en el release v<version> de legandrop/LGA_MightyTools
#                 con tools/macos/publish_release.sh (pieza canonica de la Base; lo propio, en
#                 tools/macos/release.conf). Si el release no existe lo crea; si ya existe (lo creo Windows
#                 con instalador.bat) suma lo de macOS: el orden de las plataformas da igual
#   --no-publish  No publicar ni preguntar
#   --dry-run     Con --publish: corre todas las comprobaciones y dice que haria, sin escribir nada
#   --replace     Con --publish: reemplazar los archivos de macOS si el release ya los tiene
#   --yes         Con --publish: no preguntar en el control de las notas (What's new)

set -euo pipefail

APP_NAME="LGA Mighty Tools"
# El nombre de ARCHIVO de los artefactos va sin espacios porque viaja por URL en los releases.
ARTIFACT_NAME="LGA_MightyTools"
INSTALL_DIR="/Applications"

INSTALL_DEFAULT=true
OPEN_AFTER=true
EXPLICIT_INSTALL=false
CREATE_ZIP=false
CREATE_DMG=false
PUBLISH=false
NO_PUBLISH=false
PUBLISH_ARGS=()
# Notarizar cuando hay Developer ID y se arma un ZIP o un DMG. LGA_NOTARIZE=0 lo apaga igual que
# --no-notarize (sirve sin red, o para no esperar a Apple en una prueba). Publicar exige notarizar.
NOTARIZE=true
[ "${LGA_NOTARIZE:-1}" = "0" ] && NOTARIZE=false

usage() {
    sed -n 2,29p "$0" | sed 's/^# \{0,1\}//'
}

# Sin ningun flag y con terminal, mas abajo se pregunta si publicar.
ARG_COUNT=$#

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-install) INSTALL_DEFAULT=false; shift ;;
        --no-open) OPEN_AFTER=false; shift ;;
        --install) EXPLICIT_INSTALL=true; shift ;;
        --zip) CREATE_ZIP=true; shift ;;
        --dmg) CREATE_DMG=true; shift ;;
        --no-notarize) NOTARIZE=false; shift ;;
        --publish) PUBLISH=true; CREATE_ZIP=true; CREATE_DMG=true; shift ;;
        --no-publish) NO_PUBLISH=true; shift ;;
        --dry-run|--replace|--yes) PUBLISH_ARGS+=("$1"); shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Opcion desconocida: $1"; exit 1 ;;
    esac
done

PACKAGING=false
if [ "$CREATE_ZIP" = "true" ] || [ "$CREATE_DMG" = "true" ]; then
    PACKAGING=true
fi

# Todo deploy de macOS termina con la app instalada en /Applications y abierta, tambien cuando
# empaqueta o publica (regla LGA, ../LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md §6). --install queda
# aceptado por compatibilidad y no cambia nada.
INSTALL="$INSTALL_DEFAULT"
if [ "$EXPLICIT_INSTALL" = "true" ] && [ "$INSTALL_DEFAULT" = "false" ]; then
    echo "ERROR: --install y --no-install se contradicen."
    exit 1
fi
if [ "$PUBLISH" = "true" ] && [ "$NO_PUBLISH" = "true" ]; then
    echo "ERROR: --publish y --no-publish se contradicen."
    exit 1
fi
if [ "$PUBLISH" = "true" ] && [ "$NOTARIZE" != "true" ]; then
    echo "ERROR: --publish exige notarizar (sin --no-notarize ni LGA_NOTARIZE=0)."
    exit 1
fi
if [ "$PUBLISH" != "true" ] && [ ${#PUBLISH_ARGS[@]} -gt 0 ]; then
    echo "ERROR: ${PUBLISH_ARGS[*]} solo se usa con --publish (sin el, este script compila e instala de verdad)."
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

# SHA-1 del certificado Developer ID con el que sale firmado lo que se PUBLICA. Esta FIJADO a proposito
# (ver tools/macos/release.conf, que es su unica fuente: la pieza de publicacion lo exige en el llavero y
# este script lo verifica en el bundle y en el ZIP). Cambiarlo es una decision de Lega.
SIGN_CERT_SHA1="$(source tools/macos/release.conf && printf '%s' "$PINNED_CERT_SHA1" | tr 'a-f' 'A-F')"
if ! [[ "$SIGN_CERT_SHA1" =~ ^[0-9A-Fa-f]{40}$ ]]; then
    echo "ERROR: tools/macos/release.conf no define PINNED_CERT_SHA1 (el certificado fijado)."
    exit 1
fi
echo "Deploy de $APP_NAME v$APP_VERSION"

# Sin flags y con terminal se decide ACA, antes de compilar, si se publica: `publish_release.sh --decide`
# (pieza canonica de la Base) mira el release en GitHub y pregunta con UNA tecla: publicar o sumar macOS al
# release que ya existe (lo pudo crear Windows), el bump (BUMP_CMD de tools/macos/release.conf, que sube el
# numero, commitea y pushea) si esa version ya esta publicada o falta, build local o cancelar. Si vuelve
# con "publicar", el preflight ya paso con la version que haya dejado el bump. Publicar tambien termina
# instalando y abriendo (D-60).
DECIDED=false
if [ "$ARG_COUNT" -eq 0 ] && [ -t 0 ]; then
    DECIDE_RC=0
    bash ./tools/macos/publish_release.sh --decide || DECIDE_RC=$?
    case "$DECIDE_RC" in
        0)
            PUBLISH=true; CREATE_ZIP=true; CREATE_DMG=true; PACKAGING=true; DECIDED=true
            # El bump pudo cambiar la version.
            APP_VERSION="$(tr -d '\r\n' < VERSION)"
            TAG="v${APP_VERSION}"
            ZIP_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.zip"
            DMG_NAME="${ARTIFACT_NAME}_Mac_v${APP_VERSION}.dmg"
            echo "Deploy de $APP_NAME v$APP_VERSION"
            ;;
        10) echo "Deploy local de v$APP_VERSION, sin publicar." ;;
        20)
            echo "Cancelado. No se compilo nada."
            exit 1
            ;;
        *) echo "AVISO: no se pudo decidir la publicacion (ver arriba): deploy local de v$APP_VERSION." ;;
    esac
fi

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
# firma del bundle (el certificado hoja que lleva adentro), no solo que la identidad exista en el llavero.
# Con Developer ID el requisito designado ya no nombra el certificado (lo ata al Team ID), asi que se
# extrae el certificado hoja y se compara su SHA-1. Sin `| grep -q`: con pipefail da falso (SIGPIPE).
verify_signature_pinned() {  # <.app>
    local app="$1" info auth tmp leaf=""
    info="$(codesign -dvvv "$app" 2>&1)" || { echo "ERROR: codesign no pudo leer la firma de $app"; return 1; }
    if [[ "$info" == *"Signature=adhoc"* ]]; then
        echo "ERROR: el bundle esta firmado ad-hoc: no se publica (los usuarios perderian sus permisos de macOS)."
        return 1
    fi
    if [[ "$info" != *"(runtime)"* ]]; then
        echo "ERROR: el bundle no tiene hardened runtime: Apple no lo notarizaria."
        return 1
    fi
    auth="$(printf '%s\n' "$info" | sed -n 's/^Authority=//p' | head -1)"
    tmp="$(mktemp -d "${TMPDIR:-/tmp}/lga_cert_XXXXXX")" || return 1
    if codesign -d --extract-certificates="$tmp/cert" "$app" >/dev/null 2>&1 && [ -f "$tmp/cert0" ]; then
        leaf="$(shasum -a 1 "$tmp/cert0" | awk '{print toupper($1)}')"
    fi
    rm -rf "$tmp"
    if [ "$leaf" != "$SIGN_CERT_SHA1" ]; then
        echo "ERROR: el certificado del bundle no es el fijado."
        echo "       bundle: ${leaf:-ninguno} (autoridad: ${auth:-ninguna})"
        echo "       fijado: $SIGN_CERT_SHA1 (Developer ID)"
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

# Una mitad del --self-test sobre el binario del bundle: <etiqueta> <bin> [prefijo de arch...]. Sin
# pantalla (offscreen), con tope de 300 s (en mac no hay `timeout`: alarm de perl). Pasa solo si sale
# con 0 Y imprime "self-test ok: 0 fallas". Es lo que se publica, ya firmado.
self_test_half() {
    local label="$1" bin="$2" log rc=0
    shift 2
    log="$(mktemp "${TMPDIR:-/tmp}/lga_selftest_XXXXXX")" || return 1
    QT_QPA_PLATFORM=offscreen perl -e 'alarm 300; exec @ARGV' "$@" "$bin" --self-test >"$log" 2>&1 || rc=$?
    if [ "$rc" -eq 0 ] && grep -q '^self-test ok: 0 fallas' "$log"; then
        echo "  [$label] $(grep -E '^self-test' "$log" | tail -1)"
        rm -f "$log"
        return 0
    fi
    echo "ERROR: el self-test [$label] fallo (salida $rc). Ultimas lineas:"
    tail -30 "$log" | sed 's/^/       /'
    rm -f "$log"
    return 1
}

# El release es universal y las dos mitades tienen que andar: el self-test corre con arm64 y con x86_64
# (Rosetta). Si falla cualquiera, no se empaqueta ni se publica. Sin Rosetta la mitad x86_64 no se
# puede probar: se avisa y se sigue.
run_self_tests() {  # <.app>
    local bin="$1/Contents/MacOS/$APP_NAME"
    if [ ! -x "$bin" ]; then
        echo "ERROR: no se encontro el ejecutable $bin para el self-test."
        return 1
    fi
    echo "Self-test de las dos mitades del binario universal (--self-test, sin pantalla)..."
    if [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = "1" ]; then
        self_test_half "arm64 nativo" "$bin" arch -arm64 || return 1
        if arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
            self_test_half "x86_64 con Rosetta" "$bin" arch -x86_64 || return 1
        else
            echo "AVISO: no hay Rosetta en esta Mac: la mitad x86_64 NO se probo (softwareupdate --install-rosetta)."
        fi
    else
        self_test_half "x86_64 nativo" "$bin" || return 1
        echo "AVISO: esta Mac es Intel: la mitad arm64 NO se puede probar aca."
    fi
}

# ==== VERIFICACIONES: fin


# ==== PUBLICAR
# Publicar es tools/macos/publish_release.sh, la pieza canonica de la Base (copia IDENTICA; lo propio de
# esta app esta en tools/macos/release.conf). Se llama dos veces: --preflight ANTES de compilar (gh y la
# identidad, arbol limpio, el bump con ./bump_version.sh --check, el tag, el release, el certificado
# fijado y las notas; junta todos los problemas y corta sin gastar el build) y --expect-head con el .zip
# y el .dmg ya armados y verificados (vuelve a comprobar todo, porque Windows pudo publicar en el medio,
# y publica). Contrato: ../LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md §5.
# Con --decide el preflight ya corrio: no se repite.
PUBLISH_HEAD=""
if [ "$PUBLISH" = "true" ]; then
    PUBLISH_HEAD="$(git rev-parse HEAD)"
    if [ "$DECIDED" != "true" ]; then
        bash ./tools/macos/publish_release.sh --preflight ${PUBLISH_ARGS[@]+"${PUBLISH_ARGS[@]}"}
    fi
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

# La firma va DESPUES de copiar todo adentro: cubre el contenido. La hace tools/macos/sign_bundle.sh
# (pieza canonica de la Base, copia IDENTICA), que elige la identidad: Developer ID si esta en el llavero
# (hardened runtime, sello de tiempo, de adentro hacia afuera: es lo que se puede notarizar), si no
# «LGA Code Signing» y si no ad-hoc. Los entitlements que pide el hardened runtime (Apple Events al
# Finder y microfono) estan en resources/macos/Entitlements.plist. Para PUBLICAR tiene que ser el
# certificado FIJADO de tools/macos/release.conf, y no hay alternativa.
ENTITLEMENTS_FILE="resources/macos/Entitlements.plist"
SIGN_ARGS=()
[ -f "$ENTITLEMENTS_FILE" ] && SIGN_ARGS+=(--entitlements "$ENTITLEMENTS_FILE")
bash ./tools/macos/sign_bundle.sh "deploy/$APP_NAME.app" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}
bash ./tools/macos/verify_bundle_signature.sh "deploy/$APP_NAME.app"
SIGN_KIND="$(bash ./tools/macos/sign_bundle.sh --kind)"
if [ "$PUBLISH" = "true" ]; then
    verify_signature_pinned "deploy/$APP_NAME.app" || { echo "ERROR: no se publica con esta firma."; exit 1; }
fi
echo "Bundle listo en deploy/$APP_NAME.app"

# Antes de empaquetar o publicar: el --self-test de las DOS mitades del binario universal, sobre una
# COPIA byte a byte (ditto) del bundle ya firmado: nunca se ejecuta nada adentro de deploy/, porque una
# app que corre puede escribir en su bundle y romper la firma (Doc_Deploy_macOS.md §2). Si falla alguna
# mitad, no se empaqueta ni se publica. La copia va en build-release/ y NO en la carpeta temporal del
# sistema: el self-test de Disk Space prueba la guarda de esa carpeta y falla si la app corre desde adentro.
if [ "$PACKAGING" = "true" ]; then
    SELFTEST_DIR="$(mktemp -d "$PWD/build-release/selftest_app_XXXXXX")"
    ditto "deploy/$APP_NAME.app" "$SELFTEST_DIR/$APP_NAME.app"
    SELFTEST_RC=0
    run_self_tests "$SELFTEST_DIR/$APP_NAME.app" || SELFTEST_RC=$?
    rm -rf "$SELFTEST_DIR"
    [ "$SELFTEST_RC" -eq 0 ] || { echo "ERROR: el self-test fallo; no se empaqueta ni se publica."; exit 1; }
fi

# Notarizacion (tools/macos/notarize.sh, pieza canonica): primero el .app, asi el ticket queda abrochado
# adentro del ZIP y del DMG; despues el DMG, que es lo que baja el usuario. Sin eso Gatekeeper no deja
# abrir la app en otra Mac sin el `xattr`. Un rechazo de Apple corta una publicacion; en un build local
# avisa y sigue.
NOTARIZED=false
notarize_or_warn() {  # <ruta>; 0 = notarizado
    local rc=0
    bash ./tools/macos/notarize.sh "$1" || rc=$?
    [ $rc -eq 0 ] && return 0
    if [ "$PUBLISH" = "true" ]; then
        echo "ERROR: no se publica un release sin notarizar (notarize.sh salio con $rc)."
        exit 1
    fi
    echo "AVISO: $(basename "$1") sale SIN notarizar: en otra Mac va a pedir el 'xattr'."
    return 1
}
if [ "$PACKAGING" = "true" ]; then
    if [ "$SIGN_KIND" != "developer-id" ]; then
        if [ "$PUBLISH" = "true" ]; then
            echo "ERROR: no hay Developer ID en el llavero: no se publica sin notarizar."
            exit 1
        fi
        echo "AVISO: firma '$SIGN_KIND' (no Developer ID): el ZIP y el DMG salen sin notarizar."
    elif [ "$NOTARIZE" = "true" ]; then
        notarize_or_warn "deploy/$APP_NAME.app" && NOTARIZED=true
        bash ./tools/macos/verify_bundle_signature.sh "deploy/$APP_NAME.app"
    else
        echo "AVISO: --no-notarize: el ZIP y el DMG salen sin notarizar (piden el 'xattr')."
    fi
fi

# ---- Empaquetado (--zip / --dmg / --publish): arma desde el bundle YA firmado ----
if [ "$PACKAGING" = "true" ]; then
    # deploy/release/ era del script de publicacion anterior: se borra si quedo de una corrida vieja.
    rm -f deploy/SHA256SUMS deploy/SHA256SUMS.mac
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
    # El DMG se firma y se notariza aparte (el ticket del .app ya va adentro): es lo primero que mira
    # Gatekeeper en la Mac de quien lo baja.
    if [ "$NOTARIZED" = "true" ]; then
        bash ./tools/macos/sign_bundle.sh "deploy/$DMG_NAME"
        notarize_or_warn "deploy/$DMG_NAME" || true
    fi
fi

# SHA256SUMS de macOS: el auto-update no instala nada sin su hash. Formato sha256sum ("hash  nombre").
# Al publicar, publish_release.sh lo rehace y lo fusiona con el del release, que lleva TAMBIEN la linea
# del instalador de Windows: deploy/SHA256SUMS queda como el que se subio.
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


# Si la publicacion falla, igual se instala la app (el build esta bien) y el script sale con error al final.
PUBLISH_FAILED=false
if [ "$PUBLISH" = "true" ]; then
    if ! bash ./tools/macos/publish_release.sh --expect-head "$PUBLISH_HEAD" ${PUBLISH_ARGS[@]+"${PUBLISH_ARGS[@]}"}; then
        PUBLISH_FAILED=true
    fi
fi

# ---- Instalacion: siempre, salvo --no-install (D-60) ----
if [ "$INSTALL" = "true" ]; then
    # Cerrar las copias abiertas antes de pisar la instalada: primero pedirle a la residente que salga sola
    # (--quit, como «Quit» del menu, se lleva el icono de la barra), y por las dudas por ruta exacta. La de
    # build/ tambien: es residente y de instancia unica, y con ella abierta la instalada no se quedaria.
    TARGET="$INSTALL_DIR/$APP_NAME.app"
    for COPY in "$TARGET" "$PWD/build/$APP_NAME.app"; do
        BIN="$COPY/Contents/MacOS/$APP_NAME"
        if pgrep -f "$BIN" >/dev/null 2>&1; then
            "$BIN" --quit >/dev/null 2>&1 || true
            sleep 1
            pkill -f "$BIN" 2>/dev/null || true
        fi
    done

    # Instalar y abrir: la pieza canonica de la Base (cierre por ruta de lo que quede, ditto, lsregister,
    # verificacion de la firma y `open`, asi macOS atribuye los permisos a la app y no a la terminal).
    INSTALL_ARGS=(--dest "$INSTALL_DIR")
    [ "$OPEN_AFTER" = "false" ] && INSTALL_ARGS+=(--no-open)
    bash "./tools/macos/install_to_applications.sh" "deploy/$APP_NAME.app" "${INSTALL_ARGS[@]}" || exit 1
fi

if [ "$PUBLISH_FAILED" = "true" ]; then
    echo "ERROR: la publicacion del release $TAG fallo (ver arriba)."
    [ "$INSTALL" = "true" ] && echo "       La app quedo instalada igual."
    exit 1
fi
