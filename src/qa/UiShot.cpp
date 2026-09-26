#include "qa/UiShot.h"

#include "app/MainWindow.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "app/TrayMenu.h"
#include "modules/diskspace/DiskCard.h"
#include "modules/diskspace/DiskState.h"
#include "ui/HelpDialog.h"
#include "ui/TitleBar.h"
#include "ui/UiWidgets.h"
#include "updates/UpdateDialog.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QSaveFile>
#include <QScopedPointer>
#include <QTextEdit>
#include <QVBoxLayout>
#include <qpa/qwindowsysteminterface.h>

#include <cstdio>

// Modelo: src/qa/UiShot.cpp de LGA_FolderSwitch (que viene de LGA_VideoDownloader). Nunca se llama
// show() sobre una ventana de nivel superior (WA_DontShowOnScreen + render), asi que no aparece nada
// ni se toma el foco.
//
// Lo que NO se construye aca, a proposito: QSystemTrayIcon, AppController, UpdateService, el
// servicio de atajos del sistema ni ningun modulo prendido de verdad. El host corre en modo captura:
// los modulos se construyen SIN start() y solo muestran el estado de prueba que les fija el arnes.
// Todo el estado sale de settings en memoria: nunca se lee ni se escribe settings.ini.

namespace {

// Estados del host. Los de cada herramienta son "<id>:<estado>" con los de su captureStates().
const QStringList kHostStates = {
    QStringLiteral("window"),            // forma A con la primera herramienta elegida (datos del canvas)
    QStringLiteral("general"),           // General con herramientas prendidas
    QStringLiteral("general-checking"),  // "Check now" en curso (D-13)
    QStringLiteral("general-latest"),    // "vX is the latest version"
    QStringLiteral("general-available"), // "vX is available" con "Update"
    QStringLiteral("first-run"),         // primer arranque: todo apagado, bienvenida
    QStringLiteral("help"),              // ayuda unica entera
    QStringLiteral("help-capped"),       // ayuda acotada a 480: scroll interno, Close a la vista
    QStringLiteral("help-over-window"),  // ayuda sobre el velo, acotada a la ventana
    QStringLiteral("hover-help"),
    QStringLiteral("hover-close"),
    QStringLiteral("tray-menu"),         // menu de la bandeja con secciones e iconos activo/atenuado
    QStringLiteral("tray-menu-empty"),   // menu con todo apagado
    QStringLiteral("update-dialog"),
};

// Estado de cada herramienta en la ventana del canvas (seccion 1 A): Nuke Shortcuts prendida, un
// disco bajo, Link Redirector apagado. Las demas, con su primer estado de captura.
QString canvasState(const QString &id)
{
    if (id == QLatin1String("nukeShortcuts")) {
        return QStringLiteral("on");
    }
    if (id == QLatin1String("diskSpace")) {
        return QStringLiteral("low");
    }
    // Los estados normales del canvas (pOnx('ok'), pFs('on')); si la herramienta todavia no los
    // declara, va con el primero que tenga.
    if (id == QLatin1String("openInNukeX")) {
        return QStringLiteral("ok");
    }
    if (id == QLatin1String("folderSwitch")) {
        return QStringLiteral("on");
    }
    return QString();
}

bool canvasOff(const QString &id)
{
    return id == QLatin1String("linkRedirector");
}

// Prende para captura una herramienta con su estado (vacio = el primero que declara).
bool enable(ModuleHost &host, const QString &id, const QString &state)
{
    if (!host.enableForCapture(id, QString())) {
        return false;
    }
    Module *module = host.module(id);
    if (!state.isEmpty()) {
        return module->applyCaptureState(state);
    }
    const QStringList states = module->captureStates();
    return states.isEmpty() || module->applyCaptureState(states.first());
}

bool canvasFixture(ModuleHost &host, const QString &exceptId)
{
    for (const ModuleDescriptor &d : host.descriptors()) {
        if (d.id == exceptId || canvasOff(d.id)) {
            continue;
        }
        if (!host.enableForCapture(d.id, QString())) {
            fprintf(stderr, "ui-shot: %s could not be built for capture\n", qPrintable(d.id));
            return false;
        }
        const QString wanted = canvasState(d.id);
        const bool known = host.module(d.id)->captureStates().contains(wanted);
        if (!enable(host, d.id, known ? wanted : QString())) {
            fprintf(stderr, "ui-shot: %s rejected its canvas state\n", qPrintable(d.id));
            return false;
        }
    }
    return true;
}

QJsonObject geometryOf(const QWidget *widget, const QWidget *root)
{
    const QPoint topLeft = widget->mapTo(root, QPoint(0, 0));
    return QJsonObject{{QStringLiteral("x"), topLeft.x()},
                       {QStringLiteral("y"), topLeft.y()},
                       {QStringLiteral("w"), widget->width()},
                       {QStringLiteral("h"), widget->height()}};
}

QJsonObject fontOf(const QWidget *widget)
{
    const QFontInfo info(widget->font());
    return QJsonObject{{QStringLiteral("family"), info.family()},
                       {QStringLiteral("weight"), info.weight()},
                       {QStringLiteral("pixelSize"), info.pixelSize()}};
}

void settle(QWidget &root)
{
    // Los layouts de un widget nunca mostrado se resuelven en el primer render: se hacen varias
    // pasadas con los eventos pendientes procesados para no capturar un estado intermedio.
    for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents();
        QPixmap warmup(1, 1);
        root.render(&warmup);
    }
    QCoreApplication::sendPostedEvents();
}

// Un lienzo oscuro para lo que en la app es una ventana propia (el menu, la burbuja, un dialogo).
QWidget *makeCanvas()
{
    auto *canvas = new QWidget;
    canvas->setObjectName(QStringLiteral("central"));
    canvas->setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas->setStyleSheet(QStringLiteral("QWidget#central { background-color: #101010; }"));
    return canvas;
}

QStringList allStates(const ModuleHost &host)
{
    QStringList states = kHostStates;
    for (const ModuleDescriptor &d : host.descriptors()) {
        states << QStringLiteral("off:%1").arg(d.id);
        if (d.offNotice) {
            states << QStringLiteral("off:%1:<notice>").arg(d.id);
        }
        if (Module *module = host.module(d.id)) {
            for (const QString &state : module->captureStates()) {
                states << QStringLiteral("%1:%2").arg(d.id, state);
            }
        }
    }
    return states;
}

// Disco de prueba de la sonda: nunca uno real.
DriveInfo probeDrive()
{
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    DriveInfo drive;
    drive.root = QStringLiteral("C:/");
    drive.label = QStringLiteral("C:");
    drive.name = QStringLiteral("Windows");
    drive.totalBytes = qint64(931 * kGiB);
    drive.freeBytes = qint64(182 * kGiB);
    return drive;
}

QList<HelpSection> helpSections(const ModuleHost &host)
{
    const auto providers = ModuleRegistry::helpProviders();
    QList<HelpSection> sections;
    for (const ModuleDescriptor &d : host.descriptors()) {
        if (providers.contains(d.id)) {
            sections.append(providers.value(d.id)(host.reader(d.id)));
        } else {
            sections.append(HelpSection{d.title, {}, d.description});
        }
    }
    return sections;
}

} // namespace

int runUiShot(const QStringList &args)
{
    // Segundo cinturon ademas de run_headless.ps1: este exe viaja en el instalador, y con la
    // plataforma de Windows un error de este camino podria mostrar algo en el escritorio.
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        fprintf(stderr, "ui-shot: requires QT_QPA_PLATFORM=offscreen (platform is '%s')\n",
                qPrintable(QGuiApplication::platformName()));
        return 2;
    }
    const int index = args.indexOf(QStringLiteral("--ui-shot"));
    const QString state = args.value(index + 1);

    MemorySettingsStore store;
    HostOptions options;
    options.captureMode = true;
    ModuleHost host(ModuleRegistry::all(), &store, options);

    if (state == QLatin1String("list")) {
        // Para listar los estados de cada herramienta hay que construirlas (en captura, sin start).
        for (const ModuleDescriptor &d : host.descriptors()) {
            host.enableForCapture(d.id, QString());
        }
        for (const QString &s : allStates(host)) {
            fprintf(stdout, "%s\n", qPrintable(s));
        }
        return 0;
    }
    if (index < 0 || index + 2 >= args.size()) {
        fprintf(stderr, "usage: --ui-shot <state|list> <out.png> [--dpr <1..3>]\n");
        return 2;
    }
    const QString outPath = QFileInfo(args.at(index + 2)).absoluteFilePath();
    qreal dpr = 1.0;
    const int dprIndex = args.indexOf(QStringLiteral("--dpr"));
    if (dprIndex >= 0) {
        bool ok = false;
        dpr = args.value(dprIndex + 1).toDouble(&ok);
        if (!ok || dpr < 1.0 || dpr > 3.0) {
            fprintf(stderr, "ui-shot: invalid --dpr\n");
            return 2;
        }
    }
    if (!outPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) || QFileInfo::exists(outPath)
        || !QFileInfo(outPath).dir().exists()) {
        fprintf(stderr, "ui-shot: output must be a new .png in an existing folder (%s)\n", qPrintable(outPath));
        return 2;
    }

    // "<id>:<estado>" de una herramienta, u "off:<id>[:<aviso>]".
    QString moduleId;
    QString moduleState;
    QString offId;
    QString offNotice;
    if (state.startsWith(QLatin1String("off:"))) {
        const QStringList parts = state.split(QLatin1Char(':'));
        offId = parts.value(1);
        offNotice = parts.value(2);
        if (!host.descriptor(offId)) {
            fprintf(stderr, "ui-shot: unknown tool '%s'\n", qPrintable(offId));
            return 2;
        }
    } else if (state.contains(QLatin1Char(':'))) {
        moduleId = state.section(QLatin1Char(':'), 0, 0);
        moduleState = state.section(QLatin1Char(':'), 1);
        if (!host.descriptor(moduleId)) {
            fprintf(stderr, "ui-shot: unknown tool '%s'\n", qPrintable(moduleId));
            return 2;
        }
    } else if (!kHostStates.contains(state)) {
        fprintf(stderr, "ui-shot: unknown state '%s' (--ui-shot list)\n", qPrintable(state));
        return 2;
    }

    const bool firstRun = state == QLatin1String("first-run") || state == QLatin1String("tray-menu-empty");
    if (!firstRun && !canvasFixture(host, offId.isEmpty() ? moduleId : offId)) {
        return 1;
    }
    if (!moduleId.isEmpty() && !enable(host, moduleId, moduleState)) {
        // Un estado que no existe es un error: nunca se reemplaza por otro.
        fprintf(stderr, "ui-shot: %s does not know the state '%s'\n", qPrintable(moduleId), qPrintable(moduleState));
        return 2;
    }

    MainWindow mainWindow(&host, MainWindow::Mode::Capture, nullptr);
    mainWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
    mainWindow.generalPage()->setAutoStart(!firstRun, true);
    mainWindow.generalPage()->setCheckUpdatesAtStartup(true);
    // El canvas muestra "v1.00 · up to date" (chequeo hecho); el primer arranque, todavia sin chequear.
    if (!firstRun) {
        mainWindow.setUpdateState(UpdateRowState{UpdateRowState::Kind::Latest, QStringLiteral(MIGHTYTOOLS_VERSION)});
    }
    mainWindow.setFirstRun(firstRun);
    if (state == QLatin1String("general-checking")) {
        mainWindow.setUpdateState(UpdateRowState{UpdateRowState::Kind::Checking, QString()});
    } else if (state == QLatin1String("general-available")) {
        mainWindow.setUpdateState(UpdateRowState{UpdateRowState::Kind::Available, QStringLiteral("1.01")});
    }

    QString page = MainWindow::kGeneral;
    if (state == QLatin1String("window") && !host.descriptors().isEmpty()) {
        page = host.descriptor(QStringLiteral("nukeShortcuts")) ? QStringLiteral("nukeShortcuts") : host.descriptors().first().id;
    } else if (!offId.isEmpty()) {
        page = offId;
        mainWindow.setOffNoticeCaptureState(offId, offNotice.isEmpty() ? QStringLiteral("none") : offNotice);
    } else if (!moduleId.isEmpty()) {
        page = moduleId;
    }
    mainWindow.selectPage(page);
    settle(mainWindow);

    QWidget *root = &mainWindow;
    QScopedPointer<QWidget> canvas;

    if (!moduleId.isEmpty()) {
        // Lo que no es el panel (dialogo, burbuja, menu) lo arma el modulo sin exec() ni show().
        canvas.reset(makeCanvas());
        QWidget *widget = host.module(moduleId)->createCaptureWidget(moduleState, canvas.data());
        if (widget) {
            auto *layout = new QVBoxLayout(canvas.data());
            layout->setContentsMargins(20, 20, 20, 20);
            widget->setWindowFlags(Qt::Widget);
            layout->addWidget(widget);
            root = canvas.data();
            settle(*root);
            root->adjustSize();
            settle(*root);
        } else {
            canvas.reset();
        }
    } else if (state == QLatin1String("hover-help") || state == QLatin1String("hover-close")) {
        const QString name = state == QLatin1String("hover-help") ? QStringLiteral("Help") : QStringLiteral("Close");
        for (QAbstractButton *button : mainWindow.titleBar()->findChildren<QAbstractButton *>()) {
            if (button->accessibleName() == name) {
                button->setAttribute(Qt::WA_UnderMouse, true);
                button->update();
            }
        }
        settle(mainWindow);
    } else if (state == QLatin1String("help") || state == QLatin1String("help-capped")) {
        // El dialogo solo, sobre el lienzo oscuro: "help" entero (sin tope), "help-capped" acotado a
        // 480 como en una pantalla baja (el cuerpo scrollea, encabezado y Close a la vista).
        canvas.reset(makeCanvas());
        auto *layout = new QVBoxLayout(canvas.data());
        layout->setContentsMargins(20, 20, 20, 20);
        auto *help = new HelpDialog(helpSections(host), canvas.data());
        help->setWindowFlags(Qt::Widget);
        help->fitHeight(state == QLatin1String("help-capped") ? 480 : 0);
        layout->addWidget(help);
        root = canvas.data();
        settle(*root);
        root->adjustSize();
        settle(*root);
    } else if (state == QLatin1String("help-over-window")) {
        // Velo y dialogo como hijos comunes de la ventana, no ventanas propias: se dibujan con el
        // mismo render. Acotado al alto de la ventana, como se acota a la pantalla en la app.
        auto *scrim = new Scrim(mainWindow.centralWidget());
        scrim->setVisible(true);
        auto *help = new HelpDialog(helpSections(host), mainWindow.centralWidget());
        help->setWindowFlags(Qt::Widget);
        help->fitHeight(mainWindow.height() - 48);
        help->move((mainWindow.width() - help->width()) / 2, qMax(0, (mainWindow.height() - help->height()) / 2));
        help->setVisible(true);
        settle(mainWindow);
    } else if (state.startsWith(QLatin1String("tray-menu"))) {
        canvas.reset(makeCanvas());
        auto *layout = new QVBoxLayout(canvas.data());
        layout->setContentsMargins(20, 16, 20, 20);
        layout->setSpacing(14);
        // Iconos de la bandeja activo y atenuado, como los pinta AppController (sin QSystemTrayIcon).
        auto *icons = new QHBoxLayout();
        icons->setSpacing(18);
        for (const bool dimmed : {false, true}) {
            auto *icon = new QLabel(canvas.data());
            icon->setObjectName(dimmed ? QStringLiteral("trayIconDimmed") : QStringLiteral("trayIconOn"));
            QPixmap px = trayIcon(dimmed).pixmap(QSize(16, 16), dpr);
            px.setDevicePixelRatio(dpr);
            icon->setPixmap(px);
            icons->addWidget(icon);
        }
        auto *tooltip = new QLabel(trayTooltip(&host), canvas.data());
        tooltip->setObjectName(QStringLiteral("meta"));
        icons->addWidget(tooltip);
        icons->addStretch(1);
        layout->addLayout(icons);
        auto *menu = new QMenu(canvas.data());
        menu->setWindowFlags(Qt::Widget);
        const TrayMenuActions actions = fillTrayMenu(menu, &host);
        menu->setActiveAction(actions.settings);
        layout->addWidget(menu);
        root = canvas.data();
        settle(*root);
        root->adjustSize();
        settle(*root);
    } else if (state == QLatin1String("update-dialog")) {
        canvas.reset(createUpdateAvailableDialog(nullptr, QStringLiteral("LGA Mighty Tools"), QStringLiteral("1.01"),
                                                 QStringLiteral(MIGHTYTOOLS_VERSION)));
        canvas->setAttribute(Qt::WA_DontShowOnScreen, true);
        root = canvas.data();
        settle(*root);
        root->adjustSize();
        settle(*root);
    }
    QWidget &window = *root;

    const QSize logical = window.size();
    QPixmap pixmap(logical * dpr);
    pixmap.setDevicePixelRatio(dpr);
    // Magenta: si algo queda sin pintar, salta a la vista en vez de confundirse con el fondo.
    pixmap.fill(Qt::magenta);
    window.render(&pixmap, QPoint(), QRegion(), QWidget::DrawWindowBackground | QWidget::DrawChildren);

    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGB32);
    if (!image.save(outPath, "PNG")) {
        fprintf(stderr, "ui-shot: could not save %s\n", qPrintable(outPath));
        return 1;
    }
    const QImage check(outPath);
    if (check.size() != logical * dpr) {
        fprintf(stderr, "ui-shot: saved image has unexpected size\n");
        return 1;
    }

    // Guarda de fuente: sin Inter la captura no sirve de evidencia (todo sale "un poco distinto").
    const QFontInfo windowFont(window.font());
    const bool fontOk = windowFont.family() == QLatin1String("Inter");

    QJsonObject descriptor;
    descriptor.insert(QStringLiteral("state"), state);
    descriptor.insert(QStringLiteral("dpr"), dpr);
    descriptor.insert(QStringLiteral("logical"), QJsonArray{logical.width(), logical.height()});
    descriptor.insert(QStringLiteral("physical"), QJsonArray{check.width(), check.height()});
    descriptor.insert(QStringLiteral("pid"), qint64(QCoreApplication::applicationPid()));
    descriptor.insert(QStringLiteral("version"), QStringLiteral(MIGHTYTOOLS_VERSION));
    descriptor.insert(QStringLiteral("platform"), QGuiApplication::platformName());
    descriptor.insert(QStringLiteral("fixture"), true);
    descriptor.insert(QStringLiteral("page"), mainWindow.currentPage());
    descriptor.insert(QStringLiteral("toolsOn"), QJsonArray::fromStringList(host.runningIds()));
    descriptor.insert(QStringLiteral("windowFont"), fontOf(&window));
    QJsonArray tree;
    for (QWidget *widget : window.findChildren<QWidget *>()) {
        if (!widget->isVisibleTo(&window)) {
            continue;
        }
        QJsonObject entry = geometryOf(widget, &window);
        entry.insert(QStringLiteral("class"), QString::fromLatin1(widget->metaObject()->className()));
        entry.insert(QStringLiteral("name"), widget->objectName());
        if (auto *label = qobject_cast<QLabel *>(widget)) {
            entry.insert(QStringLiteral("text"), label->text().left(80));
            entry.insert(QStringLiteral("font"), fontOf(widget));
        } else if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
            entry.insert(QStringLiteral("text"), button->text().left(80));
            entry.insert(QStringLiteral("checked"), button->isChecked());
            entry.insert(QStringLiteral("font"), fontOf(widget));
        }
        tree.append(entry);
    }
    descriptor.insert(QStringLiteral("widgets"), tree);

    QSaveFile json(outPath + QStringLiteral(".json"));
    if (!json.open(QIODevice::WriteOnly) || json.write(QJsonDocument(descriptor).toJson()) < 0 || !json.commit()) {
        fprintf(stderr, "ui-shot: could not write the .json descriptor\n");
        return 1;
    }
    if (!fontOk) {
        fprintf(stderr, "ui-shot: font resolved to '%s', expected Inter\n", qPrintable(windowFont.family()));
        return 1;
    }
    fprintf(stdout, "ui-shot ok state=%s size=%dx%d dpr=%.2f font=%s\n", qPrintable(state), logical.width(),
            logical.height(), dpr, qPrintable(windowFont.family()));
    return 0;
}

// Match words de Link Redirector con clicks y teclas que entran por el sistema de ventanas (no un
// sendEvent): Qt aplica la politica de foco real, la de un click de Lega. Ventana offscreen.
int runMatchWordsProbe()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &what) {
        fprintf(stdout, "%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
        if (!ok) {
            ++failures;
        }
    };
    MemorySettingsStore store;
    HostOptions options;
    options.captureMode = true;
    ModuleHost host(ModuleRegistry::all(), &store, options);
    if (!enable(host, QStringLiteral("linkRedirector"), QString())) {
        fprintf(stderr, "ui-probe: linkRedirector could not be built\n");
        return 1;
    }
    MainWindow mainWindow(&host, MainWindow::Mode::Capture, nullptr);
    mainWindow.selectPage(QStringLiteral("linkRedirector"));
    mainWindow.show();
    mainWindow.activateWindow();
    settle(mainWindow);
    auto *edit = mainWindow.findChild<QTextEdit *>(QStringLiteral("linkRedirectorMatchWords"));
    check(edit != nullptr, QStringLiteral("el panel tiene el campo Match words"));
    if (!edit) {
        return 1;
    }
    QWindow *window = mainWindow.windowHandle();
    check(window != nullptr, QStringLiteral("la ventana tiene su QWindow"));
    if (!window) {
        return 1;
    }
    fprintf(stdout, "info focusPolicy=%d viewportPolicy=%d readOnly=%d enabled=%d visible=%d inputMethod=%d\n", int(edit->focusPolicy()),
            int(edit->viewport()->focusPolicy()), edit->isReadOnly(), edit->isEnabled(), edit->isVisible(), edit->testAttribute(Qt::WA_InputMethodEnabled));
    const QPoint local = edit->viewport()->mapTo(&mainWindow, QPoint(20, 10));
    QWidget *under = mainWindow.childAt(local);
    fprintf(stdout, "info childAt=%s (%s)\n", under ? under->metaObject()->className() : "null",
            under ? qPrintable(under->objectName()) : "");
    const QPointF pos(local);
    QWindowSystemInterface::handleMouseEvent(window, pos, window->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton,
                                             QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent(window, pos, window->mapToGlobal(pos), Qt::NoButton, Qt::LeftButton,
                                             QEvent::MouseButtonRelease);
    QWindowSystemInterface::flushWindowSystemEvents();
    settle(mainWindow);
    QWidget *focused = QApplication::focusWidget();
    check(focused == edit || (focused && edit->isAncestorOf(focused)),
          QStringLiteral("un click en el campo le da el teclado (foco en %1)")
              .arg(focused ? QString::fromLatin1(focused->metaObject()->className()) + QLatin1Char(' ') + focused->objectName()
                           : QStringLiteral("nadie")));
    for (const QChar c : QStringLiteral("abc")) {
        const int key = Qt::Key_A + (c.unicode() - 'a');
        QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyPress, key, Qt::NoModifier, QString(c));
        QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyRelease, key, Qt::NoModifier, QString(c));
    }
    QWindowSystemInterface::flushWindowSystemEvents();
    settle(mainWindow);
    check(edit->toPlainText().contains(QStringLiteral("abc")),
          QStringLiteral("lo tipeado entra en el campo (texto: '%1')").arg(edit->toPlainText()));
    fprintf(stdout, "%s: %d fallas\n", failures == 0 ? "ui-probe ok" : "ui-probe FALLO", failures);
    return failures == 0 ? 0 : 1;
}

int runUiProbe(const QStringList &args)
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        fprintf(stderr, "ui-probe: requires QT_QPA_PLATFORM=offscreen (platform is '%s')\n",
                qPrintable(QGuiApplication::platformName()));
        return 2;
    }
    const QString probe = args.value(args.indexOf(QStringLiteral("--ui-probe")) + 1);
    if (probe == QLatin1String("match-words-typing")) {
        return runMatchWordsProbe();
    }
    if (probe != QLatin1String("threshold-focus")) {
        fprintf(stderr, "ui-probe: unknown case '%s' (threshold-focus)\n", qPrintable(probe));
        return 2;
    }

    int failures = 0;
    const auto check = [&failures](bool ok, const char *what) {
        fprintf(stdout, "%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) {
            ++failures;
        }
    };
    const auto settle = []() {
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
        }
    };

    // DiskState sin persistencia y un disco de prueba: no se lee ni se escribe nada real.
    DiskState state(nullptr);
    state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
    state.setDriveReadings({probeDrive()}, QStringList(), true, QDateTime::currentDateTime());

    // La tarjeta interactiva real (con sus connects y su filtro de clicks), sola en una ventana de la
    // plataforma offscreen: show() ahi no llega a ninguna pantalla.
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *card = new DiskCard(&state, true, &host);
    layout->addWidget(card);
    QObject::connect(&state, &DiskState::changed, card, &DiskCard::refresh);
    host.resize(440, 260);
    host.show();
    host.activateWindow();
    settle();

    auto *spin = card->findChild<QSpinBox *>(QStringLiteral("threshold"));
    auto *edit = spin ? spin->findChild<QLineEdit *>() : nullptr;
    check(spin && edit, "la fila del disco tiene su campo de umbral");
    if (!spin || !edit) {
        return 1;
    }
    const auto editing = [spin]() {
        QWidget *focused = QApplication::focusWidget();
        return focused && (focused == spin || spin->isAncestorOf(focused));
    };
    const auto type = [&](const QString &text) {
        spin->setFocus(Qt::MouseFocusReason);
        settle();
        edit->selectAll();
        edit->insert(text);
    };
    const auto press = [&](int key) {
        QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : spin;
        QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &down);
        QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
        if (QApplication::focusWidget()) {
            QCoreApplication::sendEvent(QApplication::focusWidget(), &up);
        }
        settle();
    };
    const auto threshold = [&state]() { return state.diskWatches().value(0).value; };

    // Al abrir la ventana nada tiene el teclado: Qt le da el foco solo al primer control que acepta
    // Tab, y el campo del umbral no tiene que ser ese.
    check(QApplication::activeWindow() == &host, "la ventana de prueba esta activa");
    check(!editing(), "al abrir la ventana, el campo no tiene el teclado");

    // Otra ventana al frente y de vuelta: al reactivarse tampoco lo toma.
    QWidget other;
    other.resize(100, 100);
    other.show();
    other.activateWindow();
    settle();
    host.activateWindow();
    settle();
    check(QApplication::activeWindow() == &host, "la ventana de prueba vuelve a estar activa");
    check(!editing(), "al volver a la ventana, el campo no tiene el teclado");
    other.hide();
    host.activateWindow();
    settle();

    // Un click REAL en el campo tiene que seguir activandolo. Qt da foco por click solo a los eventos
    // que llegan del sistema de ventanas (un sendEvent no cuenta), asi que aca se verifica lo que Qt
    // mira en ese momento: que el campo y su spin box (el campo le pasa el foco a el) acepten foco
    // por click. El click con el mouse lo prueba Lega.
    check((edit->focusPolicy() & Qt::ClickFocus) == Qt::ClickFocus
              && (spin->focusPolicy() & Qt::ClickFocus) == Qt::ClickFocus,
          "el campo acepta foco por click");
    check((spin->focusPolicy() & Qt::TabFocus) == 0, "el campo no acepta foco por Tab");

    // Precondicion: sin esto los chequeos de "solto el teclado" no prueban nada.
    type(QStringLiteral("75"));
    check(editing(), "al escribir, el campo tiene el teclado");

    press(Qt::Key_Return);
    check(threshold() == 75, "Enter guarda el valor escrito (75)");
    check(!editing(), "Enter suelta el campo");

    type(QStringLiteral("20"));
    press(Qt::Key_Escape);
    check(threshold() == 75, "Escape no guarda lo escrito (sigue en 75)");
    check(spin->value() == 75, "Escape vuelve el campo al valor guardado");
    check(!editing(), "Escape suelta el campo");

    type(QStringLiteral("30"));
    QPushButton *add = card->addButton();
    const QPointF inside(add->width() / 2.0, add->height() / 2.0);
    QMouseEvent click(QEvent::MouseButtonPress, inside, add->mapToGlobal(inside), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(add, &click);
    settle();
    check(threshold() == 30, "un click afuera guarda el valor escrito (30)");
    check(!editing(), "un click afuera suelta el campo");

    type(QStringLiteral("40"));
    QMouseEvent clickInside(QEvent::MouseButtonPress, QPointF(5, 5), edit->mapToGlobal(QPointF(5, 5)), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(edit, &clickInside);
    settle();
    check(editing(), "un click adentro del mismo campo lo deja escribiendo");

    fprintf(stdout, "%s: %d fallas\n", failures == 0 ? "ui-probe ok" : "ui-probe FALLO", failures);
    return failures == 0 ? 0 : 1;
}
