#include "actions/ActionRunner.h"
#include "app/ModuleRegistry.h"
#include "core/AppPaths.h"
#include "core/AppState.h"
#include "core/DiskMonitor.h"
#include "core/DiskSpace.h"
#include "core/BuildTree.h"
#include "core/DebugFlags.h"
#include "core/LgaRegistry.h"
#include "core/Shortcut.h"
#include "platform/AutoStart.h"
#include "platform/InputInjector.h"
#include "platform/NukeWatcher.h"
#include "qa/UiShot.h"
#include "tray/TrayController.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
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

// --self-test: la logica que no necesita pantalla, contra el codigo de produccion (no una replica).
// Cada guarda tiene su caso negativo. Sale 0 si todo paso.
int runSelfTest()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
        if (!ok) {
            ++failures;
        }
    };

    // Nuke por el nombre del ejecutable.
    for (const char *yes : {"Nuke15.1.exe", "Nuke16.0.exe", "nuke14.0.exe", "Nuke15.1", "Nuke15.1v4", "Nuke.exe"}) {
        check(NukeWatcher::isNukeExecutable(QString::fromLatin1(yes)), QStringLiteral("es Nuke: %1").arg(QLatin1String(yes)));
    }
    for (const char *no : {"LGA_MightyTools.exe", "NukeShortcuts.exe", "NukeX.exe", "Nuke15.1.exe.bak",
                           "explorer.exe", "Nukeitall.exe", ""}) {
        check(!NukeWatcher::isNukeExecutable(QString::fromLatin1(no)), QStringLiteral("no es Nuke: '%1'").arg(QLatin1String(no)));
    }

    // Atajos: ida y vuelta por el texto del .ini, y textos invalidos.
    for (const Shortcut &s : {Shortcut::defaultAddKeyframe(), Shortcut::defaultFrameDopeSheet()}) {
        check(Shortcut::fromPortableString(s.toPortableString()) == s,
              QStringLiteral("ida y vuelta: %1").arg(s.toPortableString()));
    }
    check(Shortcut::defaultAddKeyframe().toPortableString() == QLatin1String("Ctrl+Shift+D"),
          QStringLiteral("texto portable de Add keyframe"));
    for (const char *bad : {"", "Ctrl+Shift+", "Ctrl+Shift+!", "Ctrl+Space", "Ctrl+A, Ctrl+B", "basura"}) {
        check(!Shortcut::fromPortableString(QString::fromLatin1(bad)).isValid(),
              QStringLiteral("atajo invalido rechazado: '%1'").arg(QLatin1String(bad)));
    }

    // Punto calibrado: fraccion <-> nativo, con marcos en otra posicion y otro tamano.
    const QRect frameA(100, 50, 2000, 1000);
    const QPoint click(1880, 770);
    const QPointF spot = ActionRunner::nativeToSpot(frameA, click);
    check(ActionRunner::spotToNative(frameA, spot) == click, QStringLiteral("ida y vuelta del punto en el mismo marco"));
    const QRect frameB(-1920, 0, 1000, 500); // otro monitor, a la izquierda, mitad de tamano
    const QPoint moved = ActionRunner::spotToNative(frameB, spot);
    check(moved == QPoint(-1920 + 890, 360), QStringLiteral("el punto sigue a la ventana: %1,%2").arg(moved.x()).arg(moved.y()));
    const QPointF invalid = ActionRunner::nativeToSpot(QRect(), click);
    check(invalid.x() < 0, QStringLiteral("marco vacio no da un punto valido"));

    // ---- Espacio en disco: umbral, formato y cuando avisar.
    constexpr qint64 kGiB = qint64(1024) * 1024 * 1024;
    DriveInfo cache;
    cache.root = QStringLiteral("D:/");
    cache.label = QStringLiteral("D:");
    cache.totalBytes = 2000 * kGiB;
    cache.freeBytes = 99 * kGiB;
    DiskWatch gb{QStringLiteral("D:/"), 100, DiskWatch::Unit::GB, QString()};
    check(DiskSpace::isLow(gb, cache), QStringLiteral("99 GB libres con umbral 100 GB: bajo"));
    cache.freeBytes = 100 * kGiB;
    check(!DiskSpace::isLow(gb, cache), QStringLiteral("100 GB libres con umbral 100 GB: justo en el borde no esta bajo"));
    DiskWatch pct{QStringLiteral("D:/"), 5, DiskWatch::Unit::Percent, QString()};
    check(!DiskSpace::isLow(pct, cache), QStringLiteral("100 GB de 2000 (5%) con umbral 5%: justo en el borde no esta bajo"));
    cache.freeBytes = 99 * kGiB;
    check(DiskSpace::isLow(pct, cache), QStringLiteral("99 GB de 2000 con umbral 5%: bajo"));
    pct.value = 4;
    check(!DiskSpace::isLow(pct, cache), QStringLiteral("99 GB de 2000 con umbral 4%: no esta bajo"));
    DriveInfo unread;
    check(!DiskSpace::isLow(gb, unread), QStringLiteral("disco sin lectura: nunca bajo"));
    check(DiskSpace::clampValue(0, DiskWatch::Unit::GB) == 1 && DiskSpace::clampValue(150, DiskWatch::Unit::Percent) == 99,
          QStringLiteral("umbral acotado: 0 GB -> 1, 150% -> 99"));
    check(DiskSpace::formatBytes(182 * kGiB) == QLatin1String("182 GB"), QStringLiteral("formato: 182 GB"));
    check(DiskSpace::formatBytes(1863 * kGiB) == QLatin1String("1.82 TB"),
          QStringLiteral("formato: 1863 GiB -> '%1'").arg(DiskSpace::formatBytes(1863 * kGiB)));
    check(DiskSpace::formatBytes(2048 * kGiB) == QLatin1String("2 TB"), QStringLiteral("formato: 2 TB sin decimales de mas"));
    check(DiskSpace::formatBytes(kGiB / 2) == QLatin1String("512 MB"), QStringLiteral("formato: 512 MB"));
    check(DiskSpace::thresholdText(pct) == QLatin1String("4%"), QStringLiteral("texto del umbral en %"));
    DiskWatch::Unit unit = DiskWatch::Unit::GB;
    check(DiskSpace::unitFromString(QStringLiteral("%"), &unit) && unit == DiskWatch::Unit::Percent,
          QStringLiteral("unidad leida del .ini: %"));
    check(!DiskSpace::unitFromString(QStringLiteral("gb"), &unit), QStringLiteral("unidad invalida rechazada: gb en minuscula"));
    check(DiskSpace::isValidInterval(15) && !DiskSpace::isValidInterval(7), QStringLiteral("intervalo: 15 vale, 7 no"));
    check(DiskSpace::intervalText(360) == QLatin1String("6 hours") && DiskSpace::intervalText(60) == QLatin1String("1 hour"),
          QStringLiteral("texto del intervalo en horas"));

    const QDateTime t0(QDate(2026, 9, 24), QTime(12, 0));
    DiskSpace::AlertState alert;
    check(DiskSpace::shouldNotify(true, alert, t0), QStringLiteral("aviso: cruza el umbral"));
    check(!DiskSpace::shouldNotify(false, alert, t0), QStringLiteral("aviso: no bajo, no avisa"));
    alert.wasLow = true;
    alert.lastNotified = t0;
    check(!DiskSpace::shouldNotify(true, alert, t0.addSecs(5 * 3600)), QStringLiteral("aviso: sigue bajo a las 5 h, no repite"));
    check(DiskSpace::shouldNotify(true, alert, t0.addSecs(6 * 3600)), QStringLiteral("aviso: sigue bajo a las 6 h, repite"));
    alert.wasLow = false;
    check(DiskSpace::shouldNotify(true, alert, t0.addSecs(60)), QStringLiteral("aviso: subio y volvio a bajar, avisa al cruzar"));

    // ---- DiskMonitor entero con discos falsos y un reloj que se adelanta a mano.
    {
        AppState state(AppState::Persistence::None);
        state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
        state.setDiskThreshold(QStringLiteral("C:/"), 10, DiskWatch::Unit::Percent);
        state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
        const QList<DiskWatch> watches = state.diskWatches();
        check(watches.size() == 2 && watches.at(1).unit == DiskWatch::Unit::Percent && watches.at(1).value == 10,
              QStringLiteral("un disco nuevo toma el umbral del ultimo (10%)"));
        state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
        check(state.diskWatches().size() == 2, QStringLiteral("el mismo disco no se agrega dos veces"));

        QHash<QString, DriveInfo> disks;
        DriveInfo c;
        c.root = QStringLiteral("C:/");
        c.label = QStringLiteral("C:");
        c.totalBytes = 1000 * kGiB;
        c.freeBytes = 500 * kGiB;
        disks.insert(c.root, c);
        DriveInfo d = c;
        d.root = QStringLiteral("D:/");
        d.label = QStringLiteral("D:");
        d.freeBytes = 50 * kGiB; // 5%: bajo con el 10%
        disks.insert(d.root, d);
        QDateTime clock = t0;
        DiskMonitor::Sources sources;
        sources.listAll = [&disks]() { return disks.values(); };
        sources.query = [&disks](const QString &root, DriveInfo *drive) {
            if (!disks.contains(root)) {
                return false;
            }
            *drive = disks.value(root);
            return true;
        };
        sources.now = [&clock]() { return clock; };
        DiskMonitor monitor(&state, sources);
        QStringList notified;
        QObject::connect(&monitor, &DiskMonitor::lowSpace,
                         [&notified](const DriveInfo &drive, const DiskWatch &) { notified.append(drive.root); });

        monitor.checkNow(false);
        check(notified.isEmpty() && state.lowWatches().size() == 1,
              QStringLiteral("monitor: la lectura sin aviso marca D: bajo y no notifica"));
        monitor.checkNow(true);
        check(notified == QStringList{QStringLiteral("D:/")}, QStringLiteral("monitor: primer chequeo avisa D: y no C:"));
        clock = clock.addSecs(15 * 60);
        monitor.checkNow(true);
        check(notified.size() == 1, QStringLiteral("monitor: 15 min despues, sigue bajo y no repite"));
        clock = clock.addSecs(6 * 3600);
        monitor.checkNow(true);
        check(notified.size() == 2, QStringLiteral("monitor: 6 h despues, repite"));
        disks.remove(QStringLiteral("D:/"));
        clock = clock.addSecs(6 * 3600);
        monitor.checkNow(true);
        check(notified.size() == 2 && state.lowWatches().isEmpty(),
              QStringLiteral("monitor: D: desenchufado no avisa ni cuenta como bajo"));
        d.freeBytes = 400 * kGiB;
        disks.insert(d.root, d);
        clock = clock.addSecs(60);
        monitor.checkNow(true);
        check(notified.size() == 2, QStringLiteral("monitor: D: vuelve con espacio, no avisa"));
        d.freeBytes = 20 * kGiB;
        disks.insert(d.root, d);
        clock = clock.addSecs(60);
        monitor.checkNow(true);
        check(notified.size() == 3, QStringLiteral("monitor: D: vuelve a bajar, avisa de nuevo al cruzar"));
        state.setDiskThreshold(QStringLiteral("D:/"), 1, DiskWatch::Unit::Percent);
        clock = clock.addSecs(7 * 3600);
        monitor.checkNow(true);
        check(notified.size() == 3, QStringLiteral("monitor: con el umbral en 1% (2% libre), D: ya no esta bajo"));
        state.removeDiskWatch(QStringLiteral("C:/"));
        check(state.diskWatches().size() == 1 && !state.isWatched(QStringLiteral("C:/")),
              QStringLiteral("dejar de vigilar C:"));
        monitor.refreshAll();
        check(state.drives().size() == 2, QStringLiteral("el listado completo trae tambien los discos sin vigilar"));
    }

    std::printf("%s: %d fallas\n", failures == 0 ? "self-test ok" : "self-test FALLO", failures);
    return failures == 0 ? 0 : 1;
}

// --simulate-action <add-keyframe|frame-dope-sheet>: corre la secuencia REAL de ActionRunner con un
// InputInjector en dry-run (no mueve el mouse ni aprieta nada) e imprime los pasos.
int runSimulateAction(const QString &which)
{
    InputInjector injector(true);
    ActionRunner runner(&injector);
    QEventLoop loop;
    QObject::connect(&runner, &ActionRunner::finished, &loop, &QEventLoop::quit);
    bool started = false;
    if (which == QLatin1String("add-keyframe")) {
        started = runner.runAddKeyframe();
    } else if (which == QLatin1String("frame-dope-sheet")) {
        started = runner.runFrameDopeSheet(QRect(0, 0, 3440, 1440), QPointF(0.89, 0.72));
    } else {
        std::fprintf(stderr, "usage: --simulate-action <add-keyframe|frame-dope-sheet>\n");
        return 2;
    }
    if (!started) {
        std::fprintf(stderr, "simulate-action: la secuencia no arranco\n");
        return 1;
    }
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    for (const QString &step : injector.steps()) {
        std::printf("%s\n", qPrintable(step));
    }
    return runner.isBusy() ? 1 : 0;
}

// Fuentes embebidas, icono y hoja de estilo: lo comparten la app y la captura de QA.
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

    // Arneses sin pantalla: QCoreApplication, sin plugin de plataforma, sin log a archivo.
    if (hasArg(argc, argv, "--self-test")) {
        QCoreApplication app(argc, argv);
        setNames();
        return runSelfTest();
    }
    if (hasArg(argc, argv, "--simulate-action")) {
        QCoreApplication app(argc, argv);
        setNames();
        const QStringList args = app.arguments();
        return runSimulateAction(args.value(args.indexOf(QStringLiteral("--simulate-action")) + 1));
    }

    // --ui-shot no escribe en el debug.log: una captura no deja rastros fuera del PNG y su .json.
    const bool uiShot = hasArg(argc, argv, "--ui-shot");
    const bool uiProbe = hasArg(argc, argv, "--ui-probe");
    if (!uiShot && !uiProbe) {
        qInstallMessageHandler(fileMessageHandler);
    }
    qDebug() << kBuildVersionMarker;
    LgaBuildTree::warnIfInvalidOverride();

    QApplication app(argc, argv);
    setNames();
    QApplication::setQuitOnLastWindowClosed(false);

    // Captura de QA: sale antes de la instancia unica, de la bandeja, de los atajos y del updater.
    // Dibujar un estado no toca nada de la copia que el usuario tiene abierta.
    if (uiShot) {
        applyAppStyle(app);
        return runUiShot(app.arguments());
    }
    if (uiProbe) {
        applyAppStyle(app);
        return runUiProbe(app.arguments());
    }

    // Instancia unica.
    static QLockFile singleInstanceLock(QDir(QDir::tempPath()).filePath(QStringLiteral("com.lga.mightytools.singleton.lock")));
    if (!singleInstanceLock.tryLock(100)) {
        qWarning() << "LGA_MightyTools ya esta corriendo; esta instancia sale para no duplicar.";
        return 0;
    }

    // Registro compartido de las apps LGA (Doc_Registro_LGA.md de la Base). Desde un build no se
    // registra, a proposito; el false no es un error.
    LgaRegistry::registerThisApp(QStringLiteral("LGA_MightyTools"), QStringLiteral(MIGHTYTOOLS_VERSION));

    applyAppStyle(app);

    const bool dryRunInput = hasArg(argc, argv, "--dry-run-input") || DebugFlags::isOn(QStringLiteral("dryRunInput"));

    // Al arrancar con la sesion, el shell suele no tener la bandeja lista todavia. El reintento vive
    // DENTRO del event loop (nunca un loop bloqueante antes de exec(): el proceso quedaria sin
    // bombear mensajes y el shell lo mostraria colgado). Patron de FolderSwitch.
    constexpr int kTrayWaitMs = 90000;
    constexpr int kTrayPollMs = 500;
    int trayWaitedMs = 0;
    std::function<void()> pollTray;
    pollTray = [&app, &trayWaitedMs, &pollTray, dryRunInput]() {
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            if (trayWaitedMs > 0) {
                qInfo() << "La bandeja tardo" << trayWaitedMs << "ms en estar disponible.";
            }
            new TrayController(dryRunInput, &app);
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
