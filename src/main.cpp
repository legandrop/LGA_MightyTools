#include "app/AppController.h"
#include "app/ExternalDispatch.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "app/SingleInstance.h"
#include "app/UninstallCleanup.h"
#include "core/AppPaths.h"
#include "core/AppSettings.h"
#include "core/AutomatedRun.h"
#include "core/BuildTree.h"
#include "core/I18n.h"
#include "core/DebugFlags.h"
#include "core/LgaRegistry.h"
#include "core/UiScale.h"
#ifdef Q_OS_WIN
#include "modules/openinnukex/win/OldClientMigration.h"
#endif
#include "platform/AutoStart.h"
#include "platform/ComApartment.h"
#include "platform/ToastActivation.h"
#include "platform/WindowActivation.h"
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
    qInfo() << "Tamano de interfaz:" << UiScale::sessionLevel() << "| factor:" << UiScale::factor(UiScale::sessionLevel())
            << "| DPR de la pantalla principal:" << qApp->devicePixelRatio()
            << "| settings:" << QDir::toNativeSeparators(AppSettings::filePath());
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

// El valor que sigue a un argumento ("--ui-scale 2"), o vacio.
QByteArray argValue(int argc, char *argv[], const char *name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0) {
            return QByteArray(argv[i + 1]);
        }
    }
    return QByteArray();
}

// --simulate-action <accion> [args] [--settings-file <ruta>]: la secuencia real de una herramienta
// con el inyector en solo loguear. "<id>:<accion>" elige la herramienta; sin prefijo, se prueba cada
// una (la que no conoce la accion devuelve 2). Los settings se leen EN MEMORIA (vacios): la prueba no
// ve el settings.ini del usuario. Con --settings-file se leen de ese archivo, elegido a proposito.
const char kSimulateUsage[] =
    "usage: --simulate-action <tool:action> [args] [--settings-file <settings.ini>]\n"
    "  settings are read in memory (empty) unless --settings-file is given\n";

int runSimulateAction(const QStringList &arguments)
{
    QStringList args = arguments;
    const int fileIndex = args.indexOf(QStringLiteral("--settings-file"));
    if (fileIndex >= 0) {
        if (fileIndex + 1 >= args.size()) {
            std::fprintf(stderr, "%s", kSimulateUsage);
            return 2;
        }
        AppSettings::useFile(args.at(fileIndex + 1));
        args.remove(fileIndex, 2);
    } else {
        AppSettings::useMemoryOnly();
    }
    const int index = args.indexOf(QStringLiteral("--simulate-action"));
    const QString which = args.value(index + 1);
    if (which.isEmpty()) {
        std::fprintf(stderr, "%s", kSimulateUsage);
        return 2;
    }
    const QStringList rest = args.mid(index + 2);
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

    // --uninstall-cleanup (lo llama el desinstalador): ANTES de todo lo demas (modo corto, registro
    // LGA, instancia unica, ventanas). QCoreApplication, sin dialogos; el detalle va a stdout y, con
    // log=true, al debug.log. Sale 0 si borro todo lo propio, 1 si algo fallo.
    if (hasArg(argc, argv, "--uninstall-cleanup")) {
        QCoreApplication app(argc, argv);
        setNames();
        AppSettings::useMemoryOnly();
        if (DebugFlags::isOn(QStringLiteral("log"))) {
            qInstallMessageHandler(fileMessageHandler);
        }
        return UninstallCleanup::runFromCommandLine();
    }

    // Mudanza del cliente viejo de Open in NukeX (plan seccion 9). Los llama el instalador: mismo modo
    // corto que --uninstall-cleanup (sin ventanas, sin instancia unica, sin registro LGA), pero con el
    // settings.ini de verdad. Solo Windows.
    const bool migrateOld = hasArg(argc, argv, "--migrate-openinnukex");
    const bool removeOld = hasArg(argc, argv, "--remove-old-client");
    if (migrateOld || removeOld) {
        QCoreApplication app(argc, argv);
        setNames();
        if (DebugFlags::isOn(QStringLiteral("log"))) {
            qInstallMessageHandler(fileMessageHandler);
        }
#ifdef Q_OS_WIN
        return removeOld ? OldClientMigration::runRemoveFromCommandLine(app.arguments())
                         : OldClientMigration::runMigrateFromCommandLine(app.arguments());
#else
        std::printf("--migrate-openinnukex / --remove-old-client: solo Windows\n");
        return 0;
#endif
    }

    // Arneses sin pantalla: QCoreApplication, sin plugin de plataforma, sin log a archivo.
    if (hasArg(argc, argv, "--self-test")) {
        // Antes que nada: lo que borra o abre algo del sistema queda inerte (core/AutomatedRun.h).
        AutomatedRun::enable();
        QCoreApplication app(argc, argv);
        setNames();
        AppSettings::useMemoryOnly();
        return SelfTest::run();
    }
    if (hasArg(argc, argv, "--simulate-action")) {
        AutomatedRun::enable();
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
    const bool measureFonts = hasArg(argc, argv, "--measure-fonts");
    const bool notifyPreview = hasArg(argc, argv, "--notify-preview");
    const bool automated = uiShot || uiProbe || measureCycles || measureIdle || measureFonts || notifyPreview;
    if (automated) {
        AutomatedRun::enable();
    } else {
        qInstallMessageHandler(fileMessageHandler);
    }
    qDebug() << kBuildVersionMarker;
    LgaBuildTree::warnIfInvalidOverride();

    // Tamano de la interfaz (core/UiScale.h): Qt lo lee al crear la QApplication. La app lo lee de su
    // settings.ini (los nombres van antes, para la ruta; sin nada guardado, el 1). Las capturas lo
    // eligen con --ui-scale <0..2> y sin el flag van en 0, el tamano del diseno aprobado.
    setNames();
    if (automated) {
        UiScale::applyBeforeApp(argValue(argc, argv, "--ui-scale").toInt());
    } else {
        UiScale::applyBeforeApp(UiScale::readSavedLevel());
    }
    QApplication app(argc, argv);
    setNames();
    UiScale::clearEnvironmentAfterApp();
    QApplication::setQuitOnLastWindowClosed(false);

    // Captura y medicion: salen antes del modo corto, de la instancia unica, de la bandeja, de los
    // atajos y del updater. No tocan nada de la copia que el usuario tiene abierta.
    if (automated) {
        // Ni leer ni escribir el settings.ini del usuario.
        AppSettings::useMemoryOnly();
        applyAppStyle(app);
        // --lang es: la captura en espanol (sin settings.ini no hay idioma guardado).
        const int langIndex = app.arguments().indexOf(QStringLiteral("--lang"));
        if (langIndex > 0) {
            I18n::setLanguage(I18n::fromCode(app.arguments().value(langIndex + 1)));
        }
        if (uiShot) {
            return runUiShot(app.arguments());
        }
        if (uiProbe) {
            return runUiProbe(app.arguments());
        }
        if (measureCycles) {
            return Measure::cycles(app.arguments());
        }
        if (measureFonts) {
            return Measure::fonts(app.arguments());
        }
        if (notifyPreview) {
            return Measure::notifyPreview(app.arguments());
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
        // Los cuadros de este camino (navegador no disponible, NukeX no configurado) salen en el idioma elegido.
        I18n::setLanguage(I18n::fromCode(store.value(QStringLiteral("app/language")).toString()));
        const ExternalDispatch::Outcome outcome =
            ExternalDispatch::run(ModuleRegistry::all(), external, &store, /*dryRun=*/false);
        if (outcome.handled) {
            return outcome.exitCode;
        }
    }

    // Instancia unica. La segunda copia sin argumentos le pide a la residente que muestre la ventana.
    static QLockFile singleInstanceLock(QDir(QDir::tempPath()).filePath(QStringLiteral("com.lga.mightytools.singleton.lock")));
    const bool launchedByNotice = hasArg(argc, argv, "-Embedding") || hasArg(argc, argv, "--toast-activated");
    // --relaunch: la lanzo la copia que se reinicia (otro tamano de interfaz). Espera a que esa copia
    // termine de cerrar y suelte la instancia unica, y despues abre la ventana.
    const bool relaunch = hasArg(argc, argv, "--relaunch");
    if (!singleInstanceLock.tryLock(relaunch ? 20000 : 100)) {
        if (launchedByNotice) {
            // Windows lanzo esta copia por un click en un aviso, pero ya hay otra residente (de otro
            // exe: la anotacion apunta aca). El click se pierde; no se abre la ventana de la otra.
            qInfo() << "Lanzada por un aviso con otra copia residente; se sale sin hacer nada";
            return 0;
        }
        // Windows solo deja pasar al frente la ventana de otro proceso si el que tiene el foco (esta
        // copia, que lanzo el usuario) lo autoriza.
        WindowActivation::allowAnyProcessToActivate();
        const bool shown = SingleInstance::askResidentToShow();
        qInfo() << "LGA_MightyTools ya esta corriendo; se le pidio la ventana:" << shown;
        return 0;
    }

    // Registro compartido de las apps LGA (Doc_Registro_LGA.md de la Base). Desde un build no se
    // registra, a proposito; el false no es un error.
    LgaRegistry::registerThisApp(QStringLiteral("LGA_MightyTools"), QStringLiteral(MIGHTYTOOLS_VERSION));

    applyAppStyle(app);

    // Los clicks en los avisos, desde ya: con la app lanzada por Windows desde un aviso, el click
    // llega enseguida y queda guardado hasta que existen la bandeja y las herramientas. Solo si la
    // anotacion ante Windows es de este exe (si no, la escribe el primer aviso).
    if (launchedByNotice || ToastActivation::registeredForThisExe()) {
        ToastActivation::listen();
    }

    AppController::Options options;
    options.dryRunInput = hasArg(argc, argv, "--dry-run-input") || DebugFlags::isOn(QStringLiteral("dryRunInput"));

    // El canal de la instancia unica escucha desde ya, aunque la bandeja no este lista: un pedido de
    // otra copia en ese rato queda anotado y la ventana se abre apenas existe.
    AppController *controller = nullptr;
    bool showPending = relaunch;
    auto *server = new SingleInstanceServer(&app);
    QObject::connect(server, &SingleInstanceServer::showRequested, &app, [&controller, &showPending]() {
        if (controller) {
            controller->showSettings();
        } else {
            showPending = true;
        }
    });

    // Al arrancar con la sesion, el shell suele no tener la bandeja lista todavia. El reintento vive
    // DENTRO del event loop (nunca un loop bloqueante antes de exec(): el proceso quedaria sin
    // bombear mensajes y el shell lo mostraria colgado). Patron de FolderSwitch.
    constexpr int kTrayWaitMs = 90000;
    constexpr int kTrayPollMs = 500;
    int trayWaitedMs = 0;
    std::function<void()> pollTray;
    pollTray = [&app, &trayWaitedMs, &pollTray, options, &controller, &showPending]() {
        const bool trayReady = QSystemTrayIcon::isSystemTrayAvailable();
        if (trayReady || trayWaitedMs >= kTrayWaitMs) {
            AppController::Options start = options;
            if (trayReady) {
                if (trayWaitedMs > 0) {
                    qInfo() << "La bandeja tardo" << trayWaitedMs << "ms en estar disponible.";
                }
            } else {
                // Sin bandeja la app quedaria corriendo invisible y sin forma de llegar a ella salvo
                // abrir otra copia: se abre la ventana. Antes salia con 1, y un inicio con la sesion
                // en un shell lento perdia las herramientas prendidas de todo el dia.
                qWarning() << "No hay bandeja del sistema despues de esperar" << (kTrayWaitMs / 1000)
                           << "s; se abre la ventana para que la app no quede invisible.";
                start.openWindow = true;
            }
            start.openWindow = start.openWindow || showPending;
            controller = new AppController(start, &app);
            qInfo() << "LGA_MightyTools" << MIGHTYTOOLS_VERSION << "iniciado.";
            logStartupDiagnostics();
            return;
        }
        trayWaitedMs += kTrayPollMs;
        QTimer::singleShot(kTrayPollMs, qApp, pollTray);
    };
    QTimer::singleShot(0, qApp, pollTray);

    const int code = app.exec();
    ToastActivation::stopListening();
    return code;
}
