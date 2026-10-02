#include "modules/openinnukex/OpenInNukeXDescriptor.h"
#include "core/I18n.h"

#include "modules/openinnukex/NukeBridge.h"
#include "modules/openinnukex/NukeOpener.h"
#include "modules/openinnukex/NukeScanner.h"
#include "modules/openinnukex/NukeXPath.h"
#include "modules/openinnukex/OpenInNukeXMessages.h"
#include "modules/openinnukex/OpenInNukeXModule.h"
#include "modules/openinnukex/OpenInNukeXOperations.h"

#ifdef Q_OS_WIN
#include "modules/openinnukex/win/UserChoiceLatest.h"
#include "modules/openinnukex/win/WinFileAssociation.h"
#elif defined(Q_OS_MACOS)
#include "modules/openinnukex/mac/MacFileAssociation.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QTemporaryDir>
#include <QtGlobal>

#include <algorithm>
#include <memory>

namespace {

bool looksLikeNkFile(const QString &argument)
{
    return argument.endsWith(QStringLiteral(".nk"), Qt::CaseInsensitive);
}

bool isAssociatedWithUs()
{
#ifdef Q_OS_WIN
    return WinFileAssociation::isNkAssociatedWithUs();
#elif defined(Q_OS_MACOS)
    return MacFileAssociation::isDefaultNkHandler();
#else
    return false;
#endif
}

void paintOnxIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    // Mismo glifo que el "onx" del canvas de diseno: un documento con esquina doblada y una
    // flecha adentro apuntando a la derecha (abrir/enviar). viewBox logico de 16x16.
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.topLeft());
    const qreal scale = std::min(rect.width(), rect.height()) / 16.0;
    painter.scale(scale, scale);

    QPen pen(color);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath document;
    document.moveTo(3.5, 1.8);
    document.lineTo(9.0, 1.8);
    document.lineTo(12.5, 5.3);
    document.lineTo(12.5, 14.2);
    document.lineTo(3.5, 14.2);
    document.closeSubpath();
    painter.drawPath(document);

    painter.drawLine(QPointF(6.0, 9.5), QPointF(10.5, 9.5));

    QPainterPath chevron;
    chevron.moveTo(8.6, 7.4);
    chevron.lineTo(10.6, 9.5);
    chevron.lineTo(8.6, 11.6);
    painter.drawPath(chevron);

    painter.restore();
}

// ---- selfTest ---------------------------------------------------------------------------------

void testVersionSortAndScan(const std::function<void(bool, const QString &)> &check)
{
    using SK = std::tuple<int, int, int>;
    check(NukeScanner::versionSortKey(QStringLiteral("16.0v9")) == SK{16, 0, 9},
          QStringLiteral("versionSortKey: 16.0v9 -> (16,0,9)"));
    check(NukeScanner::versionSortKey(QStringLiteral("9.0v1")) == SK{9, 0, 1},
          QStringLiteral("versionSortKey: 9.0v1 -> (9,0,1)"));
    // Malformados: pierden contra cualquier version bien formada (quedan en (0,0,0)).
    for (const QString &bad : {QStringLiteral(""), QStringLiteral("Nuke"), QStringLiteral("abc"),
                                QStringLiteral("16"), QStringLiteral("16.0"), QStringLiteral("v9")}) {
        check(NukeScanner::versionSortKey(bad) == SK{0, 0, 0},
              QStringLiteral("versionSortKey: '%1' malformado -> (0,0,0)").arg(bad));
    }

    NukeVersion a;
    a.version = QStringLiteral("15.1v6");
    a.path = QStringLiteral("/a");
    a.displayName = QStringLiteral("Nuke 15.1v6");
    NukeVersion b;
    b.version = QStringLiteral("16.0v4");
    b.path = QStringLiteral("/b");
    b.displayName = QStringLiteral("Nuke 16.0v4");
    NukeVersion malformed;
    malformed.version = QStringLiteral("noesunaversion");
    malformed.path = QStringLiteral("/c");

    check(NukeScanner::isNewer(b, a), QStringLiteral("isNewer: 16.0v4 es mas nueva que 15.1v6"));
    check(!NukeScanner::isNewer(a, b), QStringLiteral("isNewer: 15.1v6 no es mas nueva que 16.0v4"));
    check(NukeScanner::isNewer(a, malformed), QStringLiteral("isNewer: cualquier version bien formada le gana a una malformada"));
    check(NukeScanner::newest({a, b, malformed}).path == b.path,
          QStringLiteral("newest: elige 16.0v4 entre tres versiones"));
    check(NukeScanner::newest({}).path.isEmpty(), QStringLiteral("newest: lista vacia da NukeVersion vacio"));
}

void testClaimsExternal(const std::function<void(bool, const QString &)> &check)
{
    check(looksLikeNkFile(QStringLiteral("C:/proyectos/comp.nk")), QStringLiteral("claimsExternal: .nk se reclama"));
    check(looksLikeNkFile(QStringLiteral("C:/proyectos/COMP.NK")), QStringLiteral("claimsExternal: .NK en mayusculas tambien"));
    check(!looksLikeNkFile(QStringLiteral("C:/proyectos/comp.nknk")), QStringLiteral("claimsExternal: rechaza extension parecida"));
    check(!looksLikeNkFile(QStringLiteral("https://ejemplo.com/x.nk/otra.html")), QStringLiteral("claimsExternal: rechaza si no TERMINA en .nk"));
    check(!looksLikeNkFile(QStringLiteral("")), QStringLiteral("claimsExternal: rechaza argumento vacio"));
    check(!looksLikeNkFile(QStringLiteral("C:/proyectos/comp.py")), QStringLiteral("claimsExternal: rechaza otra extension"));
}

void testBridgeSourceRepoGuard(const std::function<void(bool, const QString &)> &check)
{
    // Nunca se instala en ~/.nuke durante una prueba: todo pasa por carpetas temporales propias.
    {
        QTemporaryDir tmp;
        check(tmp.isValid(), QStringLiteral("bridge: carpeta temporal (repo con CMakeLists.txt) creada"));
        QDir(tmp.path()).mkpath(QStringLiteral("QtClient"));
        QFile marker(QDir(tmp.path()).filePath(QStringLiteral("QtClient/CMakeLists.txt")));
        marker.open(QIODevice::WriteOnly);
        marker.close();

        QString detail;
        const NukeBridge::Error err = NukeBridge::install(tmp.path(), &detail, true);
        check(err == NukeBridge::Error::SourceRepo,
              QStringLiteral("bridge: rechaza instalar sobre una carpeta con QtClient/CMakeLists.txt"));
    }
    {
        // La carpeta del plugin es un repo (el caso de la maquina de Lega hasta 2026-10-02).
        QTemporaryDir tmp;
        QDir(tmp.path()).mkpath(QStringLiteral("LGA_OpenInNukeX"));
        QFile gitMarker(QDir(tmp.path()).filePath(QStringLiteral("LGA_OpenInNukeX/.git")));
        gitMarker.open(QIODevice::WriteOnly);
        gitMarker.close();

        QString detail;
        const NukeBridge::Error err = NukeBridge::install(tmp.path(), &detail, true);
        check(err == NukeBridge::Error::SourceRepo,
              QStringLiteral("bridge: rechaza instalar sobre una carpeta del plugin con .git (guarda D-04)"));
    }
    {
        // Un clon de Mighty Tools elegido como .nuke.
        QTemporaryDir tmp;
        QDir(tmp.path()).mkpath(QStringLiteral("nuke_plugin"));
        QFile qrc(QDir(tmp.path()).filePath(QStringLiteral("nuke_plugin/OpenInNukeXBridge.qrc")));
        qrc.open(QIODevice::WriteOnly);
        qrc.close();

        QString detail;
        const NukeBridge::Error err = NukeBridge::install(tmp.path(), &detail, true);
        check(err == NukeBridge::Error::SourceRepo, QStringLiteral("bridge: rechaza instalar en un clon de Mighty Tools"));
    }
    {
        // Una .nuke versionada con git (la de Lega): se instala.
        QTemporaryDir tmp;
        QFile gitMarker(QDir(tmp.path()).filePath(QStringLiteral(".git")));
        gitMarker.open(QIODevice::WriteOnly);
        gitMarker.close();

        QString detail;
        const NukeBridge::Error err = NukeBridge::install(tmp.path(), &detail, true);
        check(err == NukeBridge::Error::None,
              QStringLiteral("bridge: instala en una .nuke versionada con git (detalle: %1)").arg(detail));
    }
    {
        QTemporaryDir tmp; // carpeta limpia: ni CMakeLists.txt ni .git
        check(tmp.isValid(), QStringLiteral("bridge: carpeta temporal limpia creada"));
        QString detail;
        const NukeBridge::Error err = NukeBridge::install(tmp.path(), &detail, true);
        check(err == NukeBridge::Error::None,
              QStringLiteral("bridge: instala en una carpeta temporal limpia (detalle: %1)").arg(detail));

        const NukeBridge::Status status = NukeBridge::inspect(tmp.path());
        check(status.installed(), QStringLiteral("bridge: inspect() ve la instalacion recien hecha como instalada"));
        check(status.installedVersion == NukeBridge::bundledVersion(),
              QStringLiteral("bridge: la version instalada coincide con la embebida"));
    }
}

// Incidente real (2026-09-25): un self-test anterior instalaba en una carpeta temporal para
// probar la guarda de repo y terminaba igual escribiendo el registro LGA COMPARTIDO de verdad
// (%APPDATA%\LGA\nuke.json), porque el guard de entonces (`publishToRegistry`) tenia un default
// que nadie desactivo. Este caso demuestra, con un `RegistryPublisher` DE PRUEBA inyectado (nunca
// `NukeBridge::publishToLgaRegistry`, nunca el archivo real), que `automatedRun` bloquea la
// publicacion — y que la guarda PUEDE fallar de verdad: con automatedRun=false el publisher de
// prueba SI se llama, así se sabe que el chequeo no es un `if` que siempre da lo mismo.
void testAutomatedRunNeverPublishesRegistry(const std::function<void(bool, const QString &)> &check)
{
    int publishCalls = 0;
    QString publishedTo;
    const NukeBridge::RegistryPublisher fakePublisher = [&](const QString &dir) {
        ++publishCalls;
        publishedTo = dir;
        return true;
    };

    {
        QTemporaryDir tmp;
        QString detail;
        publishCalls = 0;
        publishedTo.clear();
        const NukeBridge::Error err = NukeBridge::install(tmp.path(), &detail, /*automatedRun=*/true, fakePublisher);
        check(err == NukeBridge::Error::None, QStringLiteral("automatedRun: la instalacion en la carpeta temporal igual sucede"));
        check(publishCalls == 0,
              QStringLiteral("automatedRun=true: NUNCA se llama al publisher del registro LGA (ni de prueba ni real)"));
    }
    {
        // Mismo publisher DE PRUEBA, nunca el real: demuestra que la guarda puede fallar (con
        // automatedRun=false SI publica), sin arriesgar el nuke.json real en ningun momento.
        QTemporaryDir tmp;
        QString detail;
        publishCalls = 0;
        publishedTo.clear();
        const NukeBridge::Error err = NukeBridge::install(tmp.path(), &detail, /*automatedRun=*/false, fakePublisher);
        check(err == NukeBridge::Error::None, QStringLiteral("sin automatedRun: la instalacion tambien sucede"));
        check(publishCalls == 1 && publishedTo == QDir::cleanPath(tmp.path()),
              QStringLiteral("automatedRun=false: SI llama al publisher inyectado (la guarda distingue los dos casos)"));
    }
}

void testBridgeChipState(const std::function<void(bool, const QString &)> &check)
{
    NukeBridge::Status status;
    check(NukeBridge::chipState(status) == NukeBridge::ChipState::NotInstalled,
          QStringLiteral("chip: carpeta vacia -> Not installed"));

    status.filesPresent = true;
    status.pathRegistered = true;
    status.installedVersion = QString();
    check(NukeBridge::chipState(status) == NukeBridge::ChipState::InstalledUnknownVersion,
          QStringLiteral("chip: VERSION ilegible -> Installed, unknown version"));

    status.installedVersion = NukeBridge::bundledVersion();
    check(NukeBridge::chipState(status) == NukeBridge::ChipState::Installed,
          QStringLiteral("chip: misma version que el bundle -> Installed"));

    // Solo se ofrece actualizar si la instalada es MENOR que la embebida (comparacion por segmento).
    status.installedVersion = QStringLiteral("0.1");
    check(NukeBridge::chipState(status) == NukeBridge::ChipState::UpdateAvailable,
          QStringLiteral("chip: instalada 0.1 menor que la embebida %1 -> Update available").arg(NukeBridge::bundledVersion()));

    status.installedVersion = NukeBridge::bundledVersion() + QStringLiteral(".1");
    check(NukeBridge::chipState(status) == NukeBridge::ChipState::Installed,
          QStringLiteral("chip: instalada MAS NUEVA (%1) -> Installed, nunca Update available").arg(status.installedVersion));

    status.installedVersion = NukeBridge::bundledVersion() + QStringLiteral(".0");
    check(NukeBridge::chipState(status) == NukeBridge::ChipState::Installed,
          QStringLiteral("chip: %1 es la misma version que %2 -> Installed").arg(status.installedVersion, NukeBridge::bundledVersion()));

    status.installedVersion = NukeBridge::bundledVersion() + QStringLiteral("-viejo");
    check(NukeBridge::chipState(status) == NukeBridge::ChipState::InstalledUnknownVersion,
          QStringLiteral("chip: version no numerica -> Installed, unknown version"));
}

void testNukeXPath(const std::function<void(bool, const QString &)> &check)
{
    // SIEMPRE una ruta temporal propia: nunca se toca el nukeXpath.txt real de la maquina.
    QTemporaryDir tmp;
    const QString filePath = QDir(tmp.path()).filePath(QStringLiteral("nukeXpath.txt"));

    check(NukeXPath::read(filePath).isEmpty(), QStringLiteral("nukeXpath: leer un archivo que no existe da vacio"));

    check(NukeXPath::write(filePath, QStringLiteral("C:/Nuke16.0v4/Nuke16.0.exe")),
          QStringLiteral("nukeXpath: escribe la ruta en el archivo temporal"));
    check(NukeXPath::read(filePath) == QStringLiteral("C:/Nuke16.0v4/Nuke16.0.exe"),
          QStringLiteral("nukeXpath: relee exactamente lo que se escribio"));

    // healStalePath: ruta muerta + una version encontrada -> repone con la mas nueva y persiste.
    NukeVersion onlyVersion;
    onlyVersion.version = QStringLiteral("17.0v2");
    onlyVersion.path = QDir(tmp.path()).filePath(QStringLiteral("fake_nuke_exe"));
    QFile fakeExe(onlyVersion.path);
    fakeExe.open(QIODevice::WriteOnly);
    fakeExe.close();

    const QString healed = NukeXPath::healStalePath(filePath, QStringLiteral("C:/ruta/que/no/existe.exe"), {onlyVersion});
    check(healed == onlyVersion.path, QStringLiteral("healStalePath: repone con la version encontrada"));
    check(NukeXPath::read(filePath) == onlyVersion.path, QStringLiteral("healStalePath: persiste la reposicion en el archivo"));

    // Ruta valida: no la toca.
    const QString untouched = NukeXPath::healStalePath(filePath, onlyVersion.path, {onlyVersion});
    check(untouched == onlyVersion.path, QStringLiteral("healStalePath: con una ruta valida no la cambia"));

    // Sin ruta y sin versiones: no rompe, devuelve vacio.
    check(NukeXPath::healStalePath(filePath, QString(), {}).isEmpty(),
          QStringLiteral("healStalePath: sin ruta y sin versiones devuelve vacio"));
}

void testEmbeddedPayload(const std::function<void(bool, const QString &)> &check)
{
    check(QFileInfo::exists(QStringLiteral(":/bridge/init.py")), QStringLiteral("payload: init.py embebido presente"));
    check(QFileInfo::exists(QStringLiteral(":/bridge/LGA_QtAdapter_OpenInNukeX.py")),
          QStringLiteral("payload: LGA_QtAdapter_OpenInNukeX.py embebido presente"));
    check(QFileInfo::exists(QStringLiteral(":/bridge/menu.py")), QStringLiteral("payload: menu.py embebido presente"));
    check(QFileInfo::exists(QStringLiteral(":/bridge/LGA_KeyframeToggle.py")),
          QStringLiteral("payload: LGA_KeyframeToggle.py embebido presente"));
    check(!NukeBridge::bundledVersion().isEmpty(), QStringLiteral("payload: VERSION embebido legible y no vacio"));
}

} // namespace

ExternalResult openInNukeXRunExternal(const ExternalRequest &request)
{
    if (!looksLikeNkFile(request.argument)) {
        return ExternalResult::NotMine;
    }

    const QString nukeXPathFile = NukeXPath::defaultFilePath();
    const bool showLaunchNotice = request.value ? request.value(QStringLiteral("showLaunchNotice"), false).toBool() : false;

    if (!request.moduleEnabled) {
        // D-05: paso directo del host. Sin bridge, sin reglas, y SIN NINGUN MENSAJE (ni de exito
        // ni de error): el modulo esta apagado, no le corresponde opinar.
        if (request.dryRun) {
            qInfo("[openInNukeX] (dry-run, apagado) lanzaria NukeX directo con %s", qUtf8Printable(request.argument));
            if (request.finished) {
                request.finished(0);
            }
            return ExternalResult::Done;
        }
        const QString nukePath = NukeXPath::read(nukeXPathFile);
        int exitCode = 1;
        if (!nukePath.isEmpty() && QFile::exists(nukePath)) {
            QString error;
            exitCode = NukeOpener::launchNukeXProcess(nukePath, request.argument, &error) ? 0 : 1;
        }
        if (request.finished) {
            request.finished(exitCode);
        }
        return ExternalResult::Done;
    }

    if (request.dryRun) {
        qInfo("[openInNukeX] (dry-run) mandaria run_script||%s al Nuke Bridge (localhost:54325) y, si no "
              "contesta, lanzaria NukeX con la ruta de nukeXpath.txt",
              qUtf8Printable(request.argument));
        if (request.finished) {
            request.finished(0);
        }
        return ExternalResult::Done;
    }

    // Sin padre: en el modo corto de Windows nadie mas lo posee (el proceso sale solo al
    // terminar); en la app residente de mac tambien se autodestruye por deleteLater al terminar.
    auto *opener = new NukeOpener();
    NukeOpener::Options options;
    options.nkFilePath = request.argument;
    options.nukeXPathFile = nukeXPathFile;
    options.showLaunchNotice = showLaunchNotice;

    const std::function<void(int)> finished = request.finished;
    opener->open(options, [opener, finished](int exitCode) {
        opener->deleteLater();
        if (finished) {
            finished(exitCode);
        }
    });
    return ExternalResult::Pending;
}

HelpSection openInNukeXHelp(const SettingsReader &value)
{
    Q_UNUSED(value);
    // Texto EXACTO del canvas de diseno, seccion 6 (Ayuda). hb()/strong() resaltan el mismo
    // termino que el canvas, con Theme::kTextBright.
    return HelpSection{
        QStringLiteral("Open in NukeX"),
        {
            I18n::tr("Press %1 so .nk files open with Mighty Tools.").arg(HelpSection::strong(I18n::tr("Apply"))),
            I18n::tr("Pick the %1 to use when none is open.").arg(HelpSection::strong(I18n::tr("NukeX version"))),
            I18n::tr("Install the %1 so scripts open in the NukeX you already have open.")
                .arg(HelpSection::strong(QStringLiteral("Nuke Bridge"))),
        },
        QString(),
    };
}

ModuleDescriptor openInNukeXDescriptor()
{
    ModuleDescriptor descriptor;
    descriptor.id = QStringLiteral("openInNukeX");
    descriptor.title = QStringLiteral("Open in NukeX");
    descriptor.description = I18n::tr(
        "Opens Nuke files with a double-click from Explorer or Finder, directly in NukeX, reusing the session already open. If none is open, it starts a new one.");
    descriptor.offBullets = {
        I18n::tr("Takes over the .nk file association"),
        I18n::tr("Opens scripts in a running NukeX through the Nuke Bridge"),
        I18n::tr("Installs the Nuke Bridge in your .nuke folder"),
    };
    descriptor.platforms = PlatformWindows | PlatformMac;
    descriptor.paintIcon = paintOnxIcon;
    descriptor.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<OpenInNukeXModule>(context);
    };

    descriptor.offNotice = [](const QString &captureState) -> ModuleOffNotice {
        ModuleOffNotice notice;
        notice.title = I18n::tr("The .nk association is still set");
        notice.caption = I18n::tr("Double-clicked .nk files open straight in NukeX, without the Nuke Bridge.");
        notice.actionText = I18n::tr("Release .nk association");
        notice.visible = captureState.isEmpty() ? isAssociatedWithUs() : (captureState == QStringLiteral("associated"));
        return notice;
    };

    descriptor.releaseSystem = [](QString *error) -> bool {
        // Apply y la desinstalacion del cliente viejo escriben la misma asociacion en HKCU: con una en curso (por ejemplo
        // la herramienta se apago con el Apply corriendo) no se escribe nada en paralelo.
        if (OpenInNukeXOperations::busy()) {
            if (error) {
                *error = I18n::tr("An association change is still running. Try again in a moment.");
            }
            return false;
        }
#ifdef Q_OS_WIN
        return WinFileAssociation::releaseAssociation(error);
#else
        // mac: Launch Services no tiene un "soltar" simetrico a setDefaultApplicationAtURL sin
        // elegir otra app a mano; D-09 sigue abierta para esta plataforma.
        Q_UNUSED(error);
        return true;
#endif
    };

    descriptor.claimsExternal = looksLikeNkFile;
    descriptor.runExternal = openInNukeXRunExternal;

    descriptor.selfTest = [](const std::function<void(bool, const QString &)> &check) {
        testVersionSortAndScan(check);
        testClaimsExternal(check);
        testBridgeSourceRepoGuard(check);
        testAutomatedRunNeverPublishesRegistry(check);
        testBridgeChipState(check);
        testNukeXPath(check);
        testEmbeddedPayload(check);
#ifdef Q_OS_WIN
        // Vectores puros del hash de UserChoiceLatest/UserChoice (D-03), verificados contra
        // Windows: no lee el registro, propios de win/UserChoiceLatest.cpp.
        UserChoiceLatest::runSelfTestVectors(check);
#endif
    };

    descriptor.simulateAction = [](const QString &action, const QStringList &args) -> int {
        if (action != QStringLiteral("open")) {
            qWarning("[openInNukeX] simulate-action desconocida: %s", qUtf8Printable(action));
            return 2;
        }
        if (args.isEmpty()) {
            qWarning("[openInNukeX] simulate-action 'open' necesita un archivo .nk");
            return 2;
        }
        qInfo("[openInNukeX] (simulate-action, solo loguear) open %s: TCP a localhost:54325 "
              "(run_script||...) o, sin bridge, NukeX --nukex con la ruta de nukeXpath.txt",
              qUtf8Printable(args.first()));
        return 0;
    };

    return descriptor;
}
