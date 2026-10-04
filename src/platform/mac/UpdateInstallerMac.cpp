#include "platform/UpdateInstaller.h"
#include "platform/mac/UpdateHelperMac.h"

#include "core/AppPaths.h"
#include "core/AutomatedRun.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>

namespace {

const QString kBundleId = QStringLiteral("com.lga.mightytools");

// El bundle que esta corriendo: <algo>.app/Contents/MacOS/<ejecutable>. Vacio si la app no corre
// desde un bundle.
QString runningBundle()
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() != QLatin1String("MacOS") || !dir.cdUp() || dir.dirName() != QLatin1String("Contents")
        || !dir.cdUp()) {
        return {};
    }
    const QString path = dir.absolutePath();
    return path.endsWith(QLatin1String(".app"), Qt::CaseInsensitive) ? path : QString();
}

// El reemplazo va en bash y no en C++ porque la app tiene que haber cerrado. Todo lo que puede
// fallar se hace sobre una copia al lado del bundle instalado, sin tocarlo; el intercambio son dos
// renombres en el mismo volumen, y al terminar se decide por lo que quedo en disco, no por
// variables: una senal en el medio no deja la carpeta sin app.
const char kScript[] = R"MACHELPER(#!/bin/bash
# Reemplaza el bundle instalado de LGA Mighty Tools por el de un ZIP ya verificado (SHA-256) y
# vuelve a abrir la app. Lo escribe y lo lanza la propia app al aceptar una actualizacion.
#
# Uso: <script> <zip> <bundle instalado> <bundle id> <version> <pid a esperar> <log>
#               <run|test> <raiz permitida> <titulo del aviso> <texto del aviso>
#
# Modo test (bundles de mentira del self-test): no abre la app, no avisa, no registra nada en el
# sistema, y exige que el destino, el ZIP y el log esten dentro de la raiz permitida.

set -u

ZIP_PATH="${1:-}"
TARGET_APP="${2:-}"
EXPECTED_ID="${3:-}"
EXPECTED_VERSION="${4:-}"
WAIT_PID="${5:-0}"
LOG_PATH="${6:-}"
MODE="${7:-run}"
ALLOWED_ROOT="${8:-}"
NOTICE_TITLE="${9:-}"
NOTICE_BODY="${10:-}"
HELPER_PATH="$0"

PLIST_BUDDY=/usr/libexec/PlistBuddy
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

# Hasta que las guardas pasan, el script no toca nada: ni el ZIP, ni el log, ni la app.
ARMED=false
STAGE_DIR=""
PREVIOUS_APP=""
EXIT_CODE=1
FAIL_REASON="interrumpida"

log() {
    printf '[%s] %s\n' "$(/bin/date '+%Y-%m-%d %H:%M:%S')" "$*"
}

fail() {
    EXIT_CODE="$1"
    FAIL_REASON="$2"
    exit "$1"
}

real_dir() {
    (cd "$1" 2>/dev/null && /bin/pwd -P)
}

plist_value() {
    "$PLIST_BUDDY" -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null
}

# 0 si el proceso termino dentro de los segundos dados.
wait_for_exit() {
    local ticks=$(( $1 * 5 )) i=0
    case "$WAIT_PID" in ''|*[!0-9]*|0) return 0 ;; esac
    while kill -0 "$WAIT_PID" 2>/dev/null; do
        [ "$i" -ge "$ticks" ] && return 1
        /bin/sleep 0.2
        i=$((i + 1))
    done
    return 0
}

# La app ya pidio salir: se avisa, se espera a que termine y se la vuelve a abrir (antes de que
# termine, `open` solo traeria al frente la copia que se esta cerrando).
notify_and_reopen() {
    if [ -n "$NOTICE_BODY" ]; then
        /usr/bin/osascript -e 'on run argv' \
            -e 'display notification (item 2 of argv) with title (item 1 of argv)' \
            -e 'end run' "$NOTICE_TITLE" "$NOTICE_BODY" >/dev/null 2>&1
    fi
    wait_for_exit 30
    [ -d "$TARGET_APP/Contents" ] && /usr/bin/open "$TARGET_APP" --args --relaunch
}

finish() {
    trap - EXIT HUP INT TERM
    if [ "$ARMED" != true ]; then
        # Rechazada por las guardas: no se toca nada. En modo real la app ya esta cerrando, asi que
        # igual se avisa y se la reabre.
        printf 'Actualizacion rechazada (codigo %s): %s\n' "$EXIT_CODE" "$FAIL_REASON"
        [ "$MODE" = "run" ] && notify_and_reopen
        exit "$EXIT_CODE"
    fi

    # Se decide por lo que hay en disco. La version anterior solo existe apartada entre el primer
    # renombre y la limpieza final.
    if [ -n "$PREVIOUS_APP" ] && [ -d "$PREVIOUS_APP" ]; then
        if [ -d "$TARGET_APP/Contents" ]; then
            # Punto de no retorno pasado: la nueva esta en su lugar. Lo que sigue es limpieza y
            # ninguna falla de aca deshace la actualizacion.
            /usr/bin/xattr -dr com.apple.quarantine "$TARGET_APP" 2>/dev/null
            /bin/rm -rf "$STAGE_DIR"
            /bin/rm -f "$ZIP_PATH"
            log "Actualizacion terminada: $TARGET_APP en v$EXPECTED_VERSION."
            if [ "$MODE" != "test" ]; then
                /usr/bin/touch "$TARGET_APP" 2>/dev/null
                [ -x "$LSREGISTER" ] && "$LSREGISTER" -f "$TARGET_APP" >/dev/null 2>&1
                # Con `open` y no con el binario: asi macOS atribuye los permisos a la app.
                /usr/bin/open "$TARGET_APP" --args --relaunch || log "No se pudo reabrir la app."
            fi
            /bin/rm -f "$HELPER_PATH"
            exit 0
        fi
        # La instalada quedo apartada y la nueva no llego a su lugar: se devuelve.
        [ -e "$TARGET_APP" ] && /bin/rm -rf "$TARGET_APP"
        if /bin/mv "$PREVIOUS_APP" "$TARGET_APP"; then
            log "Version anterior restaurada."
        else
            # Unica copia de la app: no se borra nada.
            log "NO se pudo restaurar la version anterior. Quedo en: $PREVIOUS_APP"
            STAGE_DIR=""
        fi
    fi

    [ -n "$STAGE_DIR" ] && /bin/rm -rf "$STAGE_DIR"
    /bin/rm -f "$ZIP_PATH"
    log "La actualizacion no se hizo (codigo $EXIT_CODE): $FAIL_REASON"
    [ "$MODE" != "test" ] && notify_and_reopen
    /bin/rm -f "$HELPER_PATH"
    exit "$EXIT_CODE"
}
trap finish EXIT
trap 'exit 1' HUP INT TERM

# ---------------------------------------------------------------- guardas
[ -f "$ZIP_PATH" ] || fail 2 "no existe el ZIP"
[ -n "$EXPECTED_ID" ] && [ -n "$EXPECTED_VERSION" ] || fail 2 "falta el identificador o la version"
case "$MODE" in run|test) ;; *) fail 2 "modo desconocido" ;; esac
case "$TARGET_APP" in /*.[aA][pP][pP]) ;; *) fail 2 "el destino no es un bundle" ;; esac
[ -d "$TARGET_APP" ] && [ ! -L "$TARGET_APP" ] || fail 2 "el bundle instalado no existe"
TARGET_PARENT="$(real_dir "$(/usr/bin/dirname "$TARGET_APP")")"
[ -n "$TARGET_PARENT" ] || fail 2 "no se pudo resolver la carpeta de instalacion"
TARGET_APP="$TARGET_PARENT/$(/usr/bin/basename "$TARGET_APP")"

if [ "$MODE" = "test" ]; then
    ROOT_REAL=""
    [ -n "$ALLOWED_ROOT" ] && ROOT_REAL="$(real_dir "$ALLOWED_ROOT")"
    [ -n "$ROOT_REAL" ] && [ "$ROOT_REAL" != "/" ] || fail 4 "modo test sin raiz permitida"
    ZIP_DIR="$(real_dir "$(/usr/bin/dirname "$ZIP_PATH")")"
    LOG_DIR="$(real_dir "$(/usr/bin/dirname "$LOG_PATH")")"
    for checked in "$TARGET_PARENT" "$ZIP_DIR" "$LOG_DIR"; do
        case "$checked/" in
            "$ROOT_REAL"/*) ;;
            *) fail 4 "fuera de la carpeta de pruebas: $checked" ;;
        esac
    done
fi
ARMED=true

if [ -n "$LOG_PATH" ]; then
    /bin/mkdir -p "$(/usr/bin/dirname "$LOG_PATH")" 2>/dev/null
    if : > "$LOG_PATH" 2>/dev/null; then
        exec >> "$LOG_PATH" 2>&1
    fi
fi
log "Actualizando a v$EXPECTED_VERSION. zip=$ZIP_PATH destino=$TARGET_APP"

# ---------------------------------------------------------------- preparar la copia nueva
[ "$(plist_value "$TARGET_APP" CFBundleIdentifier)" = "$EXPECTED_ID" ] \
    || fail 6 "el bundle instalado no es $EXPECTED_ID"
[ -w "$TARGET_PARENT" ] || fail 8 "sin permiso de escritura en $TARGET_PARENT"
STAGE_DIR="$(/usr/bin/mktemp -d "$TARGET_PARENT/.lga-update.XXXXXX")" \
    || fail 8 "no se pudo crear la carpeta de trabajo en $TARGET_PARENT"
/usr/bin/ditto -x -k "$ZIP_PATH" "$STAGE_DIR/new" || fail 5 "el ZIP no se pudo descomprimir"

NEW_APP=""
FOUND=0
for candidate in "$STAGE_DIR/new"/*.app; do
    [ -d "$candidate" ] || continue
    NEW_APP="$candidate"
    FOUND=$((FOUND + 1))
done
[ "$FOUND" -eq 1 ] || fail 6 "el ZIP no trae exactamente una app"
[ "$(plist_value "$NEW_APP" CFBundleIdentifier)" = "$EXPECTED_ID" ] \
    || fail 6 "la app del ZIP no es $EXPECTED_ID"
[ "$(plist_value "$NEW_APP" CFBundleShortVersionString)" = "$EXPECTED_VERSION" ] \
    || fail 6 "la app del ZIP no es la v$EXPECTED_VERSION"
NEW_EXECUTABLE="$(plist_value "$NEW_APP" CFBundleExecutable)"
[ -n "$NEW_EXECUTABLE" ] && [ -x "$NEW_APP/Contents/MacOS/$NEW_EXECUTABLE" ] \
    || fail 6 "la app del ZIP no tiene ejecutable"
/usr/bin/codesign --verify --deep --strict "$NEW_APP" || fail 7 "la firma de la app nueva no verifica"

# Misma identidad que la instalada: si cambia, macOS la trata como otra app y el usuario pierde
# los permisos que ya le dio. Una instalada firmada ad-hoc no tiene identidad con que comparar.
INSTALLED_REQUIREMENT="$(/usr/bin/codesign -d -r- "$TARGET_APP" 2>/dev/null | /usr/bin/sed -n 's/^designated => //p')"
case "$INSTALLED_REQUIREMENT" in
    ''|*cdhash*)
        log "La instalada no tiene una identidad de firma estable: no se compara."
        ;;
    *)
        /usr/bin/codesign --verify -R="$INSTALLED_REQUIREMENT" "$NEW_APP" \
            || fail 7 "la app nueva esta firmada con otra identidad que la instalada"
        ;;
esac

# ---------------------------------------------------------------- esperar a que la app cierre
WAIT_SECONDS=30
if [ "$MODE" = "test" ] && [ -n "${LGA_UPDATE_WAIT_SECONDS:-}" ]; then
    WAIT_SECONDS="$LGA_UPDATE_WAIT_SECONDS"
fi
# Nunca se mata: si no cierra, se deja todo como estaba.
wait_for_exit "$WAIT_SECONDS" || fail 3 "la app sigue abierta"

# ---------------------------------------------------------------- intercambio
PREVIOUS_APP="$STAGE_DIR/previous.app"
/bin/mv "$TARGET_APP" "$PREVIOUS_APP" || fail 8 "no se pudo apartar la version instalada"
/bin/mv "$NEW_APP" "$TARGET_APP" || fail 8 "no se pudo poner la version nueva"
EXIT_CODE=0
exit 0
)MACHELPER";

} // namespace

namespace MacUpdateHelper {

QString scriptText()
{
    return QString::fromUtf8(kScript);
}

QString writeScript(const QString &dir)
{
    const QString path = QDir(dir).filePath(
        QStringLiteral("LGA_MightyTools_update_%1.sh").arg(QDateTime::currentMSecsSinceEpoch()));
    QSaveFile file(path);
    const QByteArray bytes = scriptText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return {};
    }
    return path;
}

QStringList arguments(const QString &scriptPath, const Job &job)
{
    return {scriptPath,
            job.zipPath,
            job.targetBundle,
            job.bundleId,
            job.version,
            QString::number(job.waitPid),
            job.logPath,
            job.testMode ? QStringLiteral("test") : QStringLiteral("run"),
            job.allowedRoot,
            job.noticeTitle,
            job.noticeBody};
}

bool allowed(const Job &job, QString *why)
{
    const auto refuse = [why](const QString &reason) {
        if (why) {
            *why = reason;
        }
        return false;
    };
    if (!AutomatedRun::active()) {
        return true;
    }
    if (!job.testMode) {
        return refuse(QStringLiteral("corrida automatizada sin modo test"));
    }
    if (job.bundleId == kBundleId) {
        return refuse(QStringLiteral("corrida automatizada con el identificador de la app real"));
    }
    const QString sandbox = AutomatedRun::sandbox();
    if (sandbox.isEmpty() || QDir::cleanPath(job.allowedRoot) != QDir::cleanPath(sandbox)) {
        return refuse(QStringLiteral("la raiz permitida no es la carpeta de pruebas"));
    }
    for (const QString &path : {job.targetBundle, job.zipPath, job.logPath}) {
        if (!AutomatedRun::mayModify(path)) {
            return refuse(QStringLiteral("fuera de la carpeta de pruebas: %1").arg(path));
        }
    }
    return true;
}

} // namespace MacUpdateHelper

namespace UpdateInstaller {

Blocker blocker()
{
    if (AppPaths::isBuildTree()) {
        return Blocker::DevelopmentCopy;
    }
    const QString bundle = runningBundle();
    if (bundle.isEmpty()) {
        return Blocker::DevelopmentCopy;
    }
    const QFileInfo parent(QFileInfo(bundle).absolutePath());
    // Gatekeeper corre una app recien bajada desde una copia de solo lectura hasta que se la mueve;
    // desde la imagen de disco tampoco hay donde escribir.
    if (bundle.contains(QLatin1String("/AppTranslocation/"))
        || (bundle.startsWith(QLatin1String("/Volumes/")) && !parent.isWritable())) {
        return Blocker::MoveToApplications;
    }
    if (!parent.isWritable()) {
        return Blocker::FolderNotWritable;
    }
    return Blocker::None;
}

Result launch(const QString &packagePath, const QString &version, const QString &noticeTitle,
              const QString &noticeBody)
{
    Result result;
    if (AutomatedRun::active()) {
        result.detail = QStringLiteral("automated run");
        return result;
    }
    if (blocker() != Blocker::None) {
        result.detail = QStringLiteral("this copy cannot update itself");
        return result;
    }

    MacUpdateHelper::Job job;
    job.zipPath = packagePath;
    job.targetBundle = runningBundle();
    job.bundleId = kBundleId;
    job.version = version;
    job.waitPid = QCoreApplication::applicationPid();
    job.logPath = QDir(QFileInfo(AppPaths::logFile()).absolutePath()).filePath(QStringLiteral("update.log"));
    job.noticeTitle = noticeTitle;
    job.noticeBody = noticeBody;

    const QString script = MacUpdateHelper::writeScript(QFileInfo(packagePath).absolutePath());
    if (script.isEmpty()) {
        result.detail = QStringLiteral("could not write the update script");
        return result;
    }
    result.started = QProcess::startDetached(QStringLiteral("/bin/bash"), MacUpdateHelper::arguments(script, job));
    if (!result.started) {
        QFile::remove(script);
        result.detail = QStringLiteral("could not start the update script");
    }
    return result;
}

} // namespace UpdateInstaller
