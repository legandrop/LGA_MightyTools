#include "app/AppController.h"
#include "app/ExternalDispatch.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "app/SingleInstance.h"
#include "core/AppPaths.h"
#include "core/BuildTree.h"
#include "core/DebugFlags.h"
#include "core/LgaRegistry.h"
#include "platform/AutoStart.h"
#include "platform/ComApartment.h"
#include "qa/Measure.h"
#include "qa/SelfTest.h"
#include "qa/UiShot.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLockFile>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {

/**
 * Marcador de version embebido en el binario. Lo lee un guard del instalador (findstr sobre el exe)
 * para verificar QUE VERSION quedo compilada antes de empaquetar. Mismo mecanismo que
 * LGA_FolderSwitch y LGA_MediaTools_v2. Dos condiciones:
 *  - Literal NARROW (char[]): findstr busca bytes, y un literal UTF-16 no aparece.
 *  - REFERENCIADO: sin uso, el linker lo descarta. El qDebug() de main() lo referencia.
 */
const char kBuildVersionMarker[] = "LGA_MIGHTYTOOLS_BUILD_VERSION=" MIGHTYTOOLS_VERSION;

// Log a archivo: la app no tiene consola, sin esto qDebug es invisible. Se activa con log=true en
// config/debug_flags.txt. La ruta sale de AppPaths, que no depende del directorio de trabajo
// (lanzada por la Run key, el cwd es System32).
void fileMessageHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    static const bool enabled = DebugFlags::isOn(QStringLiteral("log"));
    if (!enabled) {
        return;
    }
    static QFile logFile(AppPaths::logFile());
    static const bool opened = logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (!opened) {
        return;
    }
    QTextStream out(&logFile);
    out << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << ' ' << msg << '\n';
    out.flush();
}

// Lo que hace falta leer del log cuando "no arranca con el sistema".
void logStartupDiagnostics()
{
    const QString stored = AutoStart::storedCommand();
    qInfo() << "Exe:" << QDir::toNativeSeparators(QCoreApplication::applicationFilePath())
            << "| arbol de build:" << AppPaths::isBuildTree() << "| raiz:" << AppPaths::rootDir()
            << "| inicio automatico disponible:" << AutoStart::availability().available;
    qInfo() << "Inicio con la sesion:" << (AutoStart::isEnabled() ? "activo" : "inactivo")
            << "| valor en Run:" << (stored.isEmpty() ? QStringLiteral("(ninguno)") : stored)
            << "| deshabilitado en Task Manager:" << AutoStart::disabledByTaskManager();
}

bool hasArg(int argc, char *argv[], const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

// --simulate-action <accion> [args]: la secuencia real de una herramienta con el inyector en solo
// loguear. "<id>:<accion>" elige la herramienta; sin prefijo, se prueba cada una (la que no conoce
// la accion devuelve 2).
int runSimulateAction(const QStringList &arguments)
{
    const int index = arguments.indexOf(QStringLiteral("--simulate-action"));
    const QString which = arguments.value(index + 1);
    const QStringList rest = arguments.mid(index + 2);
    const QString moduleId = which.contains(QLatin1Char(':')) ? which.section(QLatin1Char(':'), 0, 0) : QString();
    const QString action = which.contains(QLatin1Char(':')) ? which.section(QLatin1Char(':'), 1) : which;
    for (const ModuleDescriptor &d : ModuleRegistry::all()) {
        if (!d.simulateAction || (!moduleId.isEmpty() && d.id != moduleId)) {
            continue;
        }
        const int code = d.simulateAction(action, rest);
        if (code != 2) {
            return code;
        }
    }
    std::fprintf(stderr, "simulate-action: nadie conoce '%s'\n", qPrintable(which));
    return 2;
}

// Fuentes embebidas, icono y hoja de estilo: lo comparten la app, el modo corto y la captura de QA.
void applyAppStyle(QApplication &app)
{
    Theme::apply(app);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/LGA_MightyTools.png")));
}

void setNames()
{
    // Los dos nombres ANTES de cualquier QStandardPaths: AppDataLocation saltea los vacios.
    QCoreApplication::setOrganizationName(QStringLiteral("LGA"));
    QCoreApplication::setApplicationName(QStringLiteral("LGA_MightyTools"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MIGHTYTOOLS_VERSION));
}

} // namespace

int main(int argc, char *argv[])
{
    AppPaths::init(argc > 0 ? argv[0] : nullptr);
    // COM en modo apartment, una vez, antes de cualquier modulo (plan 4.4).
    const ComApartment com;

    // Arneses sin pantalla: QCoreApplication, sin plugin de plataforma, sin log a archivo.
    if (hasArg(argc, argv, "--self-test")) {
        QCoreApplication app(argc, argv);
        setNames();
        return SelfTest::run();
    }
    if (hasArg(argc, argv, "--simulate-action")) {
        QCoreApplication app(argc, argv);
        setNames();
        return runSimulateAction(app.arguments());
    }

    // --ui-shot y la medicion no escriben en el debug.log: una corrida de QA no deja rastros fuera
    // de su salida.
    const bool uiShot = hasArg(argc, argv, "--ui-shot");
    const bool uiProbe = hasArg(argc, argv, "--ui-probe");
    const bool measureCycles = hasArg(argc, argv, "--measure-cycles");
    const bool measureIdle = hasArg(argc, argv, "--measure-idle");
    const bool automated = uiShot || uiProbe || measureCycles || measureIdle;
    if (!automated) {
        qInstallMessageHandler(fileMessageHandler);
    }
    qDebug() << kBuildVersionMarker;
    LgaBuildTree::warnIfInvalidOverride();

    QApplication app(argc, argv);
    setNames();
    QApplication::setQuitOnLastWindowClosed(false);

    // Captura y medicion: salen antes del modo corto, de la instancia unica, de la bandeja, de los
    // atajos y del updater. No tocan nada de la copia que el usuario tiene abierta.
    if (automated) {
        applyAppStyle(app);
        if (uiShot) {
            return runUiShot(app.arguments());
        }
        if (uiProbe) {
            return runUiProbe(app.arguments());
        }
        if (measureCycles) {
            return Measure::cycles(app.arguments());
        }
        return Measure::idle(app.arguments());
    }

    // Modo corto (plan 4.5): un .nk o una URL que manda el sistema se atiende ANTES de la instancia
    // unica, sin ventana ni bandeja y sin tocar la copia residente. Si ninguna herramienta la
    // reclama, sigue como un arranque normal.
    const QString external = ExternalDispatch::firstPlainArgument(app.arguments());
    if (!external.isEmpty()) {
        applyAppStyle(app);
        FileSettingsStore store;
        const ExternalDispatch::Outcome outcome =
            ExternalDispatch::run(ModuleRegistry::all(), external, &store, /*dryRun=*/false);
        if (outcome.handled) {
            return outcome.exitCode;
        }
    }

    // Instancia unica. La segunda copia sin argumentos le pide a la residente que muestre la ventana.
    static QLockFile singleInstanceLock(QDir(QDir::tempPath()).filePath(QStringLiteral("com.lga.mightytools.singleton.lock")));
    if (!singleInstanceLock.tryLock(100)) {
        const bool shown = SingleInstance::askResidentToShow();
        qInfo() << "LGA_MightyTools ya esta corriendo; se le pidio la ventana:" << shown;
        return 0;
    }

    // Registro compartido de las apps LGA (Doc_Registro_LGA.md de la Base). Desde un build no se
    // registra, a proposito; el false no es un error.
    LgaRegistry::registerThisApp(QStringLiteral("LGA_MightyTools"), QStringLiteral(MIGHTYTOOLS_VERSION));

    applyAppStyle(app);

    AppController::Options options;
    options.dryRunInput = hasArg(argc, argv, "--dry-run-input") || DebugFlags::isOn(QStringLiteral("dryRunInput"));

    // Al arrancar con la sesion, el shell suele no tener la bandeja lista todavia. El reintento vive
    // DENTRO del event loop (nunca un loop bloqueante antes de exec(): el proceso quedaria sin
    // bombear mensajes y el shell lo mostraria colgado). Patron de FolderSwitch.
    constexpr int kTrayWaitMs = 90000;
    constexpr int kTrayPollMs = 500;
    int trayWaitedMs = 0;
    std::function<void()> pollTray;
    pollTray = [&app, &trayWaitedMs, &pollTray, options]() {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            if (trayWaitedMs > 0) {
                qInfo() << "La bandeja tardo" << trayWaitedMs << "ms en estar disponible.";
            }
            new AppController(options, &app);
            qInfo() << "LGA_MightyTools" << MIGHTYTOOLS_VERSION << "iniciado.";
            logStartupDiagnostics();
            return;
        }
        if (trayWaitedMs >= kTrayWaitMs) {
            // Nada de cartel: es una app de bandeja, un modal al arranque no lo ve nadie.
            qWarning() << "No hay bandeja del sistema despues de esperar" << (kTrayWaitMs / 1000) << "s; se sale.";
            QCoreApplication::exit(1);
            return;
        }
        trayWaitedMs += kTrayPollMs;
        QTimer::singleShot(kTrayPollMs, qApp, pollTray);
    };
    QTimer::singleShot(0, qApp, pollTray);

    return app.exec();
}
