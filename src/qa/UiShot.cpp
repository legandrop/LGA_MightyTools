#include "qa/UiShot.h"

#include "app/GeneralPage.h"
#include "app/MainWindow.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "app/TrayMenu.h"
#include "core/I18n.h"
#include "core/UiScale.h"
#include "modules/diskspace/DiskCard.h"
#include "modules/diskspace/DiskState.h"
#include "modules/diskspace/cleanup/CleanupWindow.h"
#include "modules/diskspace/cleanup/SizeListView.h"
#include "ui/CustomTooltip.h"
#include "ui/HelpDialog.h"
#include "ui/TitleBar.h"
#include "ui/UiWidgets.h"
#include "updates/UpdateDialog.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFontInfo>
#include <QFrame>
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
#include <QMessageBox>
#include <QPixmap>
#include <QSaveFile>
#include <QScopedPointer>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
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
    QStringLiteral("tooltip"),           // el tooltip propio, de una linea
    QStringLiteral("tooltip-long"),      // el tooltip propio, con dos acciones y un atajo
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
        fprintf(stderr, "usage: --ui-shot <state|list> <out.png> [--dpr <1..3>] [--ui-scale <0..2>] [--screen-area <w>x<h>]\n");
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
    // --ui-scale (lo aplica main antes de la QApplication, core/UiScale.h): la pantalla de la captura
    // ya tiene ese factor; la imagen se dibuja con el mismo, como la ve el usuario.
    const int uiScale = UiScale::sessionLevel();
    dpr *= UiScale::factor(uiScale);
    if (!outPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) || QFileInfo::exists(outPath)
        || !QFileInfo(outPath).dir().exists()) {
        fprintf(stderr, "ui-shot: output must be a new .png in an existing folder (%s)\n", qPrintable(outPath));
        return 2;
    }

    // El tooltip propio (ui/CustomTooltip), suelto: es una ventana aparte y se captura a si mismo.
    if (state == QLatin1String("tooltip") || state == QLatin1String("tooltip-long")) {
        const QString text = state == QLatin1String("tooltip")
                                 ? I18n::tr("See what is using this drive")
                                 : QStringLiteral("<span style='color:#E8E8E8'><b>Click:</b></span> %1<sbr/>"
                                                  "<span style='color:#E8E8E8'><b>Shift+Click:</b></span> %2"
                                                  "<div align='center'><span style='color:#E8E8E8'><b>Ctrl+Alt+O</b></span></div>")
                                       .arg(I18n::tr("While a drive stays low.<br>Drives are checked every %1 min.").arg(15),
                                            I18n::tr("Remove this rule (deletes nothing)"));
        return CustomTooltip::instance()->debugGrabToFile(text, outPath) ? 0 : 1;
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
    descriptor.insert(QStringLiteral("uiScale"), uiScale);
    descriptor.insert(QStringLiteral("screenDpr"), qApp->devicePixelRatio());
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
            // Para detectar textos cortados (un idioma mas largo): el ancho del texto en una linea contra el
            // ancho del label. Solo sin ajuste de linea y sin texto enriquecido.
            if (!label->wordWrap() && label->textFormat() != Qt::RichText && !label->text().contains(QLatin1Char('<'))) {
                const int textWidth = label->fontMetrics().horizontalAdvance(label->text());
                const QMargins margins = label->contentsMargins();
                const int room = label->width() - margins.left() - margins.right();
                entry.insert(QStringLiteral("textWidth"), textWidth);
                entry.insert(QStringLiteral("clipped"), textWidth > room);
            }
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
    fprintf(stdout, "ui-shot ok state=%s size=%dx%d dpr=%.2f uiScale=%d font=%s\n", qPrintable(state),
            logical.width(), logical.height(), dpr, uiScale, qPrintable(windowFont.family()));
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

// El icono de la bandeja con otro tamano de interfaz (correr con --ui-scale 0, 1 y 2): repite lo que hace
// el plugin de Windows de Qt 6.5 (QWindowsSystemTrayIcon::createIcon) y exige un pixmap del tamano nativo
// exacto. trayIcon() solo, por el mismo camino, es el control que muestra que la guarda puede fallar.
int runTrayIconProbe()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &what) {
        fprintf(stdout, "%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
        if (!ok) {
            ++failures;
        }
    };
    const qreal dpr = qApp->devicePixelRatio();
    const auto viaQt = [](const QIcon &icon, int native) {
        const QSize size = icon.actualSize(QSize(native, native));
        return icon.pixmap(size).size();
    };
    for (const int native : {16, 20, 24, 32}) {
        for (const bool dimmed : {false, true}) {
            const QSize got = viaQt(systemTrayIcon(dimmed), native);
            check(got == QSize(native, native), QStringLiteral("bandeja %1 px%2 con DPR %3: pixmap de %4x%5")
                                                    .arg(native).arg(dimmed ? QStringLiteral(" atenuado") : QString())
                                                    .arg(dpr).arg(got.width()).arg(got.height()));
        }
    }
    const QSize plain = viaQt(trayIcon(false), 16);
    fprintf(stdout, "info trayIcon() sin el motor, 16 px con DPR %.2f: %dx%d (%s)\n", dpr, plain.width(), plain.height(),
            plain == QSize(16, 16) ? "igual" : "distinto: el problema que corrige systemTrayIcon");
    fprintf(stdout, "%s: %d fallas\n", failures == 0 ? "tray-icon ok" : "tray-icon FALLO", failures);
    return failures == 0 ? 0 : 1;
}

int runRebuildProbe()
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
    QStringList ids;
    for (const ModuleDescriptor &d : host.descriptors()) {
        ids << d.id;
        if (d.id != QLatin1String("linkRedirector") && !enable(host, d.id, QString())) {
            fprintf(stderr, "no se pudo construir %s\n", qPrintable(d.id));
            return 1;
        }
    }
    MainWindow w(&host, MainWindow::Mode::Capture, nullptr);
    w.setAttribute(Qt::WA_DontShowOnScreen, true);
    int rebuilt = 0;
    QObject::connect(&w, &MainWindow::rebuilt, [&rebuilt]() { ++rebuilt; });
    w.show();
    settle(w);
    const auto labelTexts = [&w]() {
        QStringList out;
        for (QLabel *l : w.findChildren<QLabel *>()) {
            out << l->text();
        }
        return out;
    };
    for (int round = 0; round < 6; ++round) {
        const bool es = round % 2 == 0;
        I18n::setLanguage(es ? I18n::Language::Spanish : I18n::Language::English);
        host.refreshDescriptorTexts(ModuleRegistry::all());
        // Como AppController: fuera de la senal, por un timer.
        QTimer::singleShot(0, &w, [&w]() { w.rebuildUi(); });
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
        }
        settle(w);
        check(rebuilt == round + 1, QStringLiteral("ronda %1: rebuilt emitido (%2)").arg(round).arg(rebuilt));
        check(w.generalPage() != nullptr, QStringLiteral("ronda %1: hay pagina General nueva").arg(round));
        const QStringList texts = labelTexts();
        check(texts.contains(es ? QStringLiteral("Idioma") : QStringLiteral("Language")),
              QStringLiteral("ronda %1: rotulo del idioma en %2").arg(round).arg(es ? "es" : "en"));
        check(!texts.contains(es ? QStringLiteral("Language") : QStringLiteral("Idioma")),
              QStringLiteral("ronda %1: sin restos del otro idioma").arg(round));
        for (const QString &id : ids) {
            w.selectPage(id);
            settle(w);
            check(w.currentPage() == id, QStringLiteral("ronda %1: pagina %2").arg(round).arg(id));
            QPixmap pm = w.grab();
            check(!pm.isNull(), QStringLiteral("ronda %1: %2 se pinta").arg(round).arg(id));
        }
        w.selectPage(QStringLiteral("general"));
        settle(w);
        w.grab();
    }
    // Con una pagina de herramienta elegida, con estado de update y con el primer arranque.
    I18n::setLanguage(I18n::Language::English);
    host.refreshDescriptorTexts(ModuleRegistry::all());
    w.rebuildUi();
    w.selectPage(QStringLiteral("nukeShortcuts"));
    w.setUpdateState(UpdateRowState{UpdateRowState::Kind::Available, QStringLiteral("1.01")});
    settle(w);
    I18n::setLanguage(I18n::Language::Spanish);
    host.refreshDescriptorTexts(ModuleRegistry::all());
    w.rebuildUi();
    settle(w);
    check(w.currentPage() == QLatin1String("nukeShortcuts"), QStringLiteral("la pagina elegida sobrevive al rearmado"));
    check(labelTexts().contains(QStringLiteral("Atajos")), QStringLiteral("el panel de la herramienta sale en espanol (%1)").arg(labelTexts().join(QStringLiteral("|")).left(200)));
    w.selectPage(QStringLiteral("general"));
    settle(w);
    check(labelTexts().contains(QStringLiteral("v1.01 disponible")) , QStringLiteral("el estado de update sobrevive (%1)").arg(labelTexts().filter(QStringLiteral("v1.0")).join(QStringLiteral("|"))));
    w.setFirstRun(true);
    w.rebuildUi();
    settle(w);
    check(w.generalPage()->firstRun(), QStringLiteral("el primer arranque sobrevive"));
    check(labelTexts().contains(QStringLiteral("LGA Mighty Tools")), QStringLiteral("la bienvenida sale"));
    // Apagar y prender una herramienta despues de rearmar (el host y la ventana siguen de la mano).
    host.setEnabled(QStringLiteral("diskSpace"), false);
    settle(w);
    w.selectPage(QStringLiteral("diskSpace"));
    settle(w);
    check(labelTexts().contains(QStringLiteral("Disk Space está apagada")), QStringLiteral("apagar despues de rearmar no rompe"));
    // La traduccion de Qt (qtbase_es.qm, instalada con el idioma): botones estandar y menu contextual de los
    // campos salen en espanol, y vuelven a ingles con el idioma.
    const auto qtTexts = []() {
        QMessageBox box;
        box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        QLineEdit edit;
        QStringList menuTexts;
        QMenu *menu = edit.createStandardContextMenu();
        for (QAction *action : menu->actions()) {
            menuTexts << action->text();
        }
        delete menu;
        return QStringList{box.button(QMessageBox::Ok)->text(), box.button(QMessageBox::Cancel)->text(),
                           menuTexts.join(QLatin1Char('|'))};
    };
    I18n::setLanguage(I18n::Language::Spanish);
    const QStringList es = qtTexts();
    check(es.at(0) == QStringLiteral("Aceptar") && es.at(1) == QStringLiteral("Cancelar")
              && es.at(2).contains(QStringLiteral("Deshacer")) && es.at(2).contains(QStringLiteral("Pegar")),
          QStringLiteral("Qt en espanol: botones y menu contextual (%1)").arg(es.join(QStringLiteral(" / "))));
    I18n::setLanguage(I18n::Language::English);
    const QStringList en = qtTexts();
    check(en.at(0) == QStringLiteral("OK") && en.at(1) == QStringLiteral("Cancel")
              && en.at(2).contains(QStringLiteral("Undo")),
          QStringLiteral("Qt de vuelta en ingles (%1)").arg(en.join(QStringLiteral(" / "))));
    fprintf(stdout, "%s: %d fallas\n", failures == 0 ? "rebuild-probe ok" : "rebuild-probe FALLO", failures);
    return failures == 0 ? 0 : 1;
}

// --ui-probe threshold-drag: la marca del umbral de un disco se arrastra con el mouse. Tarjeta interactiva real
// en offscreen, con eventos de mouse mandados a la barra (nunca al escritorio). Mientras se arrastra cambian
// el numero y el texto, sin guardar; al soltar se guarda UNA vez. Probado en GB y en %.
int runThresholdDragProbe()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &what) {
        fprintf(stdout, "%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
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

    DiskState state(nullptr);
    state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
    state.setDriveReadings({probeDrive()}, QStringList(), true, QDateTime::currentDateTime());
    state.setDiskThreshold(QStringLiteral("C:/"), 50, DiskWatch::Unit::GB);
    int saves = 0;
    QObject::connect(&state, &DiskState::changed, [&saves]() { ++saves; });

    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *card = new DiskCard(&state, true, &host);
    layout->addWidget(card);
    QObject::connect(&state, &DiskState::changed, card, &DiskCard::refresh);
    host.resize(440, 260);
    host.show();
    settle();

    auto *bar = card->findChild<UsageBar *>();
    auto *spin = card->findChild<QSpinBox *>(QStringLiteral("threshold"));
    check(bar && spin, QStringLiteral("la fila tiene su barra y su campo"));
    if (!bar || !spin) {
        fprintf(stdout, "threshold-drag FALLO: %d fallas\n", failures);
        return 1;
    }
    check(!bar->testAttribute(Qt::WA_TransparentForMouseEvents) && bar->cursor().shape() == Qt::SizeHorCursor,
          QStringLiteral("la barra toma el mouse y muestra el cursor de arrastre"));

    const auto send = [bar](QEvent::Type type, double fraction, Qt::MouseButtons buttons) {
        const QPointF local(fraction * bar->width(), bar->height() / 2.0);
        QMouseEvent event(type, local, bar->mapToGlobal(local), Qt::LeftButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(bar, &event);
    };
    const auto watchValue = [&state]() { return state.diskWatches().value(0).value; };

    // GB: 931 GB de total. La marca al 90 % del ancho = 10 % libre = 93 GB.
    const int savesBefore = saves;
    send(QEvent::MouseButtonPress, 0.80, Qt::LeftButton);
    send(QEvent::MouseMove, 0.90, Qt::LeftButton);
    settle();
    check(spin->value() == 93, QStringLiteral("GB: arrastrando al 90 % el numero muestra 93 (muestra %1)").arg(spin->value()));
    check(watchValue() == 50 && saves == savesBefore, QStringLiteral("GB: mientras se arrastra no se guarda nada"));
    send(QEvent::MouseButtonRelease, 0.90, Qt::NoButton);
    settle();
    check(watchValue() == 93, QStringLiteral("GB: al soltar se guarda 93 (guardado %1)").arg(watchValue()));
    check(saves == savesBefore + 1, QStringLiteral("GB: se guarda una sola vez (%1)").arg(saves - savesBefore));

    // Extremos: nunca menos de 1 ni mas que el disco.
    send(QEvent::MouseButtonPress, 1.0, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, 1.0, Qt::NoButton);
    settle();
    check(watchValue() == 1, QStringLiteral("GB: al extremo derecho el limite es 1 (%1)").arg(watchValue()));
    send(QEvent::MouseButtonPress, 0.0, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, 0.0, Qt::NoButton);
    settle();
    check(watchValue() == 931, QStringLiteral("GB: al extremo izquierdo el limite es el disco entero (%1)").arg(watchValue()));

    // Porcentaje.
    state.setDiskThreshold(QStringLiteral("C:/"), 10, DiskWatch::Unit::Percent);
    settle();
    bar = card->findChild<UsageBar *>();
    spin = card->findChild<QSpinBox *>(QStringLiteral("threshold"));
    send(QEvent::MouseButtonPress, 0.75, Qt::LeftButton);
    send(QEvent::MouseMove, 0.70, Qt::LeftButton);
    settle();
    check(spin->value() == 30, QStringLiteral("%: arrastrando al 70 % el numero muestra 30 (muestra %1)").arg(spin->value()));
    send(QEvent::MouseButtonRelease, 0.70, Qt::NoButton);
    settle();
    check(watchValue() == 30 && state.diskWatches().value(0).unit == DiskWatch::Unit::Percent,
          QStringLiteral("%: al soltar se guarda 30 % (guardado %1)").arg(watchValue()));

    // Sin moverse: soltar donde ya estaba no guarda.
    const int savesSame = saves;
    send(QEvent::MouseButtonPress, 0.70, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, 0.70, Qt::NoButton);
    settle();
    check(saves == savesSame, QStringLiteral("soltar en el mismo valor no guarda"));

    // La ventana se oculta en medio del arrastre y el soltar nunca llega: el arrastre se corta, no se guarda
    // y el numero vuelve a lo guardado (antes la fila quedaba "editando" para siempre).
    const int savesHide = saves;
    send(QEvent::MouseButtonPress, 0.50, Qt::LeftButton);
    send(QEvent::MouseMove, 0.40, Qt::LeftButton);
    settle();
    host.hide();
    settle();
    check(!bar->isDragging() && spin->value() == 30 && saves == savesHide,
          QStringLiteral("ocultar en medio del arrastre lo corta y vuelve a lo guardado (muestra %1)").arg(spin->value()));
    host.show();
    settle();

    // Un movimiento sin el boton apretado (el soltar se perdio) tambien corta el arrastre.
    send(QEvent::MouseButtonPress, 0.50, Qt::LeftButton);
    send(QEvent::MouseMove, 0.40, Qt::NoButton);
    settle();
    check(!bar->isDragging() && spin->value() == 30, QStringLiteral("moverse sin el boton apretado corta el arrastre (arrastrando %1, muestra %2)").arg(bar->isDragging()).arg(spin->value()));

    // El disco se desenchufa en medio del arrastre: al soltar no se guarda y se vuelve a lo guardado; y un
    // disco desenchufado no se arrastra (ni cursor de arrastre).
    const int savesMissing = saves;
    send(QEvent::MouseButtonPress, 0.50, Qt::LeftButton);
    send(QEvent::MouseMove, 0.40, Qt::LeftButton);
    state.setDriveReadings({}, QStringList(), true, QDateTime::currentDateTime());
    settle();
    bar = card->findChild<UsageBar *>();
    spin = card->findChild<QSpinBox *>(QStringLiteral("threshold"));
    send(QEvent::MouseButtonRelease, 0.40, Qt::NoButton);
    settle();
    check(watchValue() == 30 && spin->value() == 30,
          QStringLiteral("desenchufado en medio del arrastre: no guarda y muestra lo guardado (muestra %1)").arg(spin->value()));
    check(bar->testAttribute(Qt::WA_TransparentForMouseEvents) && bar->cursor().shape() != Qt::SizeHorCursor
              && bar->toolTip().isEmpty(),
          QStringLiteral("desenchufado: la barra no se arrastra, sin cursor ni tooltip de arrastre"));
    Q_UNUSED(savesMissing);

    fprintf(stdout, "%s: %d fallas\n", failures == 0 ? "threshold-drag ok" : "threshold-drag FALLO", failures);
    return failures == 0 ? 0 : 1;
}

// --ui-probe tooltip-hover: el tooltip propio (ui/CustomTooltip) sobre controles reales, en offscreen. El
// cursor que se mueve es el de la plataforma offscreen, nunca el del escritorio. Comprueba que aparece
// recien despues de la demora, donde corresponde, que se oculta al salir y con un click, que el control
// sigue recibiendo sus eventos, y que no queda ningun tooltip nativo.
int runTooltipProbe()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &what) {
        fprintf(stdout, "%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
        if (!ok) {
            ++failures;
        }
    };
    const auto wait = [](int msecs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < msecs) {
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
    };
    const auto tip = []() -> QLabel * {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QLatin1String("tooltipLabel")) {
                return qobject_cast<QLabel *>(widget);
            }
        }
        return nullptr;
    };
    const auto tipVisible = [&tip]() { return tip() && tip()->isVisible(); };
    const auto enter = [](QWidget *widget) {
        const QPointF local = widget->rect().center();
        QCursor::setPos(widget->mapToGlobal(local.toPoint()));
        QEnterEvent event(local, local, widget->mapToGlobal(local));
        QCoreApplication::sendEvent(widget, &event);
    };
    const auto moveTo = [](QWidget *widget, const QPoint &local) {
        QCursor::setPos(widget->mapToGlobal(local));
        QMouseEvent event(QEvent::MouseMove, QPointF(local), QPointF(widget->mapToGlobal(local)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(widget, &event);
    };

    // Un widget que cuenta sus Enter: el filtro del tooltip no se los puede comer.
    struct Counter : QWidget
    {
        using QWidget::QWidget;
        int enters = 0;
        void enterEvent(QEnterEvent *) override { ++enters; }
    };

    DiskState state(nullptr);
    state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
    state.setDriveReadings({probeDrive()}, QStringList(), true, QDateTime::currentDateTime());

    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *card = new DiskCard(&state, true, &host);
    layout->addWidget(card);
    auto *counter = new Counter(&host);
    counter->setFixedSize(60, 20);
    layout->addWidget(counter);
    auto *elided = new ElidedLabel(&host);
    elided->setText(QStringLiteral("C:\\Users\\lega\\AppData\\Local\\Packages\\Some.Long.Package_abcdef\\LocalCache\\Roaming\\App"));
    layout->addWidget(elided);
    auto *list = new SizeListView(&host);
    list->setTree(false);
    list->setColumns(QStringLiteral("Name"), {{QStringLiteral("Size"), 84, SizeListColumn::Kind::Size}});
    QList<SizeListRow> rows;
    for (int i = 0; i < 4; ++i) {
        SizeListRow row;
        row.id = QStringLiteral("r%1").arg(i);
        row.name = QStringLiteral("file%1.bin").arg(i);
        row.tooltip = i == 2 ? row.name : QStringLiteral("C:\\data\\file%1.bin").arg(i);
        if (i == 3) {
            // Como en "What changed": el nombre ES la ruta, y no entra.
            row.name = QStringLiteral("C:\\Users\\lega\\AppData\\Local\\Packages\\Some.Very.Long.Package.Name_abcdefghijk\\LocalCache\\Roaming\\App");
            row.tooltip = row.name;
        }
        row.cells = {QStringLiteral("1.00 GB")};
        rows.append(row);
    }
    list->setRows(rows);
    list->setMinimumHeight(170);
    layout->addWidget(list);
    host.resize(440, 560);
    host.show();
    wait(50);

    // ---- Ningun tooltip nativo en lo que se ve.
    int native = 0;
    for (QWidget *widget : host.findChildren<QWidget *>()) {
        native += widget->toolTip().isEmpty() ? 0 : 1;
    }
    check(native == 0, QStringLiteral("ningun control tiene un tooltip nativo de Qt (%1)").arg(native));

    // ---- Un boton con tooltip: aparece despues de la demora, debajo del boton y centrado.
    auto *explore = card->findChild<QPushButton *>(QStringLiteral("exploreDrive"));
    check(explore != nullptr, QStringLiteral("la fila del disco tiene el boton de lista"));
    if (!explore) {
        fprintf(stdout, "tooltip-hover FALLO: %d fallas\n", failures);
        return 1;
    }
    enter(explore);
    wait(250);
    check(!tipVisible(), QStringLiteral("a los 250 ms todavia no aparece (hay demora)"));
    wait(600);
    check(tipVisible() && tip()->text().contains(I18n::tr("See what is using this drive")),
          QStringLiteral("pasada la demora aparece, con su texto"));
    if (tipVisible()) {
        const QRect shown = tip()->geometry();
        const QPoint anchor = explore->mapToGlobal(QPoint(explore->width() / 2, explore->height()));
        // (Cerca del borde de la ventana el tooltip se corre para no salirse y la flecha lo compensa:
        // lo que tiene que cumplirse es que el boton quede dentro de su ancho.)
        check(shown.top() >= anchor.y() - 12 && shown.top() <= anchor.y() + 4 && shown.left() <= anchor.x() && anchor.x() <= shown.right(),
              QStringLiteral("queda debajo del boton y apuntandole (tooltip %1,%2 %3x%4; ancla %5,%6)")
                  .arg(shown.x()).arg(shown.y()).arg(shown.width()).arg(shown.height()).arg(anchor.x()).arg(anchor.y()));
    }
    // ---- Salir del boton lo oculta en el acto.
    moveTo(&host, QPoint(5, 5));
    wait(20);
    check(!tipVisible(), QStringLiteral("al salir del boton se oculta en el acto"));
    // ---- Con OTRA ventana de la app activa y lejos, el tooltip se sigue acomodando a la ventana de su
    // control (la ventana de limpieza y la principal conviven).
    {
        QWidget other;
        other.resize(200, 100);
        other.move(host.mapToGlobal(QPoint(host.width(), 0)).x() + 700, 0);
        other.show();
        other.activateWindow();
        wait(60);
        const bool otherActive = QApplication::activeWindow() == &other;
        enter(explore);
        wait(850);
        const QPoint anchor = explore->mapToGlobal(QPoint(explore->width() / 2, explore->height()));
        const QRect shown = tipVisible() ? tip()->geometry() : QRect();
        check(tipVisible() && shown.left() <= anchor.x() && anchor.x() <= shown.right(),
              QStringLiteral("con otra ventana activa (%1) el tooltip sigue junto a su boton (tooltip x %2..%3; boton x %4)")
                  .arg(otherActive ? QStringLiteral("activa") : QStringLiteral("no se pudo activar"))
                  .arg(shown.left()).arg(shown.right()).arg(anchor.x()));
        moveTo(&host, QPoint(5, 5));
        other.hide();
        host.activateWindow();
        wait(60);
    }
    // ---- Un click lo oculta, y tambien cancela el que estaba por aparecer.
    enter(explore);
    wait(850);
    const bool shownAgain = tipVisible();
    {
        const QPointF local = explore->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress, local, explore->mapToGlobal(local), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&host, &press);
    }
    wait(20);
    check(shownAgain && !tipVisible(), QStringLiteral("un click lo oculta"));
    enter(explore);
    wait(200);
    {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
        QCoreApplication::sendEvent(&host, &key);
    }
    wait(700);
    check(!tipVisible(), QStringLiteral("una tecla durante la demora lo cancela"));
    moveTo(&host, QPoint(5, 5));

    // ---- El control sigue recibiendo su Enter.
    CustomTooltip::instance()->setToolTip(counter, QStringLiteral("x"));
    enter(counter);
    wait(20);
    // (Mover el cursor de la plataforma tambien genera un Enter: pueden llegar dos.)
    check(counter->enters >= 1, QStringLiteral("el tooltip no se come el Enter del control (%1)").arg(counter->enters));
    moveTo(&host, QPoint(5, 5));
    CustomTooltip::instance()->setToolTip(counter, QString());

    // ---- Texto recortado: tooltip con el texto entero; si entra, ninguno.
    elided->setFixedWidth(120);
    wait(20);
    enter(elided);
    wait(850);
    check(tipVisible() && tip()->text().contains(QStringLiteral("LocalCache")), QStringLiteral("un texto recortado muestra el texto entero"));
    moveTo(&host, QPoint(5, 5));
    wait(20);
    elided->setText(QStringLiteral("corto"));
    wait(20);
    enter(elided);
    wait(850);
    check(!tipVisible(), QStringLiteral("un texto que entra no lleva tooltip"));
    moveTo(&host, QPoint(5, 5));

    // ---- Filas de una lista: la ruta de la fila, anclada a la fila; otra fila, otro tooltip.
    QWidget *viewport = list->viewport();
    const int headerHeight = SizeListView::kHeaderHeight;
    const int rowHeight = SizeListView::kRowHeight;
    moveTo(viewport, QPoint(40, headerHeight + rowHeight / 2));
    wait(850);
    check(tipVisible() && tip()->text().contains(QStringLiteral("file0.bin")) && tip()->text().contains(QStringLiteral("data")),
          QStringLiteral("la fila 0 muestra su ruta"));
    if (tipVisible()) {
        const int rowBottom = viewport->mapToGlobal(QPoint(0, headerHeight + rowHeight)).y();
        check(qAbs(tip()->geometry().top() - rowBottom) <= 12,
              QStringLiteral("el tooltip queda pegado a SU fila (tooltip %1, base de la fila %2)").arg(tip()->geometry().top()).arg(rowBottom));
    }
    moveTo(viewport, QPoint(40, headerHeight + rowHeight + rowHeight / 2));
    wait(20);
    check(!tipVisible(), QStringLiteral("al pasar a otra fila el anterior se oculta"));
    wait(850);
    check(tipVisible() && tip()->text().contains(QStringLiteral("file1.bin")), QStringLiteral("la fila 1 muestra la suya"));
    moveTo(viewport, QPoint(40, headerHeight + 2 * rowHeight + rowHeight / 2));
    wait(850);
    check(!tipVisible(), QStringLiteral("una fila cuya ruta es lo mismo que ya dice no lleva tooltip"));
    moveTo(viewport, QPoint(40, headerHeight + 3 * rowHeight + rowHeight / 2));
    wait(850);
    check(tipVisible() && tip()->text().contains(QStringLiteral("LocalCache")),
          QStringLiteral("una fila cuyo nombre se pinta recortado muestra el nombre entero"));
    // Salir de la lista con un tooltip a la vista.
    const bool visibleBeforeLeaving = tipVisible();
    moveTo(&host, QPoint(5, 5));
    wait(20);
    check(visibleBeforeLeaving && !tipVisible(), QStringLiteral("al salir de la lista con un tooltip a la vista, se oculta"));
    // La ventana del tooltip no toma el mouse: no le saca el hover al control de abajo.
    check(tip() && tip()->windowFlags().testFlag(Qt::WindowTransparentForInput),
          QStringLiteral("la ventana del tooltip es transparente al mouse"));

    fprintf(stdout, "%s: %d fallas\n", failures == 0 ? "tooltip-hover ok" : "tooltip-hover FALLO", failures);
    return failures == 0 ? 0 : 1;
}

// --ui-probe cleanup-sort: la ventana de Disk Space se estira y sus listas se ordenan por columna.
//  - Una SizeListView interactiva en offscreen: el click en un titulo pide ordenar por esa columna (y el
//    doble click, dos veces), el click en una fila no, y el titulo muestra la mano.
//  - La ventana de limpieza con datos fijos (sin motor ni disco): Folders, Largest files y What changed
//    ordenados por nombre, por peso y de vuelta, con lo "muted" siempre al final de su nivel.
//  - Cada estado de la ventana al tamano minimo, en ingles y en espanol: los botones de la barra de abajo
//    entran enteros, sin pisarse, y el texto de la barra no queda cortado.
int runCleanupSortProbe()
{
    int failures = 0;
    const auto check = [&failures](bool ok, const QString &what) {
        fprintf(stdout, "%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
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

    // ---- La lista sola: clicks en el encabezado.
    {
        QWidget host;
        auto *layout = new QVBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *list = new SizeListView(&host);
        list->setTree(false);
        list->setColumns(QStringLiteral("Name"), {{QStringLiteral("Size"), 84, SizeListColumn::Kind::Size},
                                                  {QStringLiteral("Modified"), 72, SizeListColumn::Kind::Text}});
        QList<SizeListRow> rows;
        for (int i = 0; i < 3; ++i) {
            SizeListRow row;
            row.id = QStringLiteral("r%1").arg(i);
            row.name = QStringLiteral("file%1.bin").arg(i);
            row.cells = {QStringLiteral("1.00 GB"), QStringLiteral("3 d")};
            rows.append(row);
        }
        list->setRows(rows);
        layout->addWidget(list);
        host.resize(600, 200);
        host.show();
        settle();
        QList<int> requested;
        QObject::connect(list, &SizeListView::sortRequested, [&requested](int column) { requested.append(column); });
        QWidget *viewport = list->viewport();
        const auto press = [viewport](QEvent::Type type, const QPoint &pos) {
            QMouseEvent event(type, QPointF(pos), viewport->mapToGlobal(QPointF(pos)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &event);
        };
        const int headerY = SizeListView::kHeaderHeight / 2;
        const int width = viewport->width();
        // Columnas contra el borde derecho: Modified termina en width-16, Size 12 + 72 antes.
        const int modifiedX = width - 16 - 10;
        const int sizeX = width - 16 - 72 - 12 - 10;
        check(list->headerColumnAt(30) == SizeListView::kNameColumn && list->headerColumnAt(sizeX) == 0
                  && list->headerColumnAt(modifiedX) == 1 && list->headerColumnAt(width - 4) == SizeListView::kNoColumn,
              QStringLiteral("cada zona del encabezado es su columna (nombre, Size, Modified; el margen, ninguna)"));
        press(QEvent::MouseButtonPress, QPoint(30, headerY));
        press(QEvent::MouseButtonPress, QPoint(sizeX, headerY));
        press(QEvent::MouseButtonPress, QPoint(modifiedX, headerY));
        check(requested == QList<int>({SizeListView::kNameColumn, 0, 1}),
              QStringLiteral("un click en cada titulo pide ordenar por esa columna"));
        requested.clear();
        press(QEvent::MouseButtonPress, QPoint(30, SizeListView::kHeaderHeight + SizeListView::kRowHeight / 2));
        check(requested.isEmpty() && list->selectedIds() == QStringList({QStringLiteral("r0")}),
              QStringLiteral("un click en una fila la elige y no ordena"));
        press(QEvent::MouseButtonPress, QPoint(sizeX, headerY));
        press(QEvent::MouseButtonDblClick, QPoint(sizeX, headerY));
        check(requested == QList<int>({0, 0}), QStringLiteral("dos clicks rapidos en un titulo son dos pedidos"));
        QMouseEvent move(QEvent::MouseMove, QPointF(sizeX, headerY), viewport->mapToGlobal(QPointF(sizeX, headerY)), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &move);
        check(viewport->cursor().shape() == Qt::PointingHandCursor, QStringLiteral("el titulo muestra la mano"));
        list->setSort(1, false);
        check(list->sortColumn() == 1 && !list->sortDescending(), QStringLiteral("setSort guarda columna y sentido"));
        list->setInteractive(false);
        requested.clear();
        press(QEvent::MouseButtonPress, QPoint(sizeX, headerY));
        check(requested.isEmpty(), QStringLiteral("sin interaccion (captura, borrando) el encabezado no ordena"));
    }

    // ---- Un texto con el lugar justo no se recorta (con el tamano de interfaz 1 o 2 los anchos tienen
    // decimales; correr la sonda tambien con --ui-scale 1).
    {
        QWidget host;
        auto *label = new ElidedLabel(&host);
        label->setObjectName(QStringLiteral("meterText"));
        host.show();
        int cut = 0;
        QString firstCut;
        const QStringList texts = {QStringLiteral("C:\\Users\\lega\\AppData\\Local\\Temp\\work\\C--Portable-LGA-ShotDocs"),
                                   QStringLiteral("C:\\Users\\lega\\AppData\\Local\\UA Midi Control"),
                                   QStringLiteral("C:\\Users\\lega\\AppData\\Local\\npm-cache"),
                                   QStringLiteral("C:\\$Recycle.Bin\\S-1-5-21-1907397018-2288654118-380003385-1001"),
                                   QStringLiteral("Scanned today 12:41 · 4.69 M files · 7 s")};
        for (int extra = 0; extra < 40; ++extra) {
            for (const QString &base : texts) {
                const QString text = base + QString(extra, QLatin1Char('x'));
                label->setText(text);
                label->resize(label->sizeHint());
                if (label->shownText() != text) {
                    ++cut;
                    firstCut = firstCut.isEmpty() ? text : firstCut;
                }
            }
        }
        check(cut == 0, QStringLiteral("un texto en el ancho de su sizeHint no se recorta (%1 de 200 cortados%2)")
                            .arg(cut)
                            .arg(firstCut.isEmpty() ? QString() : QStringLiteral(": ") + firstCut));
    }

    // ---- La ventana con datos fijos.
    const auto namesInOrder = [](const QList<SizeListRow> &rows, bool descending) {
        // Hermanos de cada nivel (las filas "muted" no cuentan): cada uno respecto del anterior del mismo nivel.
        QHash<int, QString> last;
        for (const SizeListRow &row : rows) {
            for (auto it = last.begin(); it != last.end();) {
                it = it.key() > row.depth ? last.erase(it) : std::next(it);
            }
            if (row.muted) {
                continue;
            }
            if (last.contains(row.depth)) {
                const int order = QString::compare(last.value(row.depth), row.name, Qt::CaseInsensitive);
                if (descending ? order < 0 : order > 0) {
                    return false;
                }
            }
            last.insert(row.depth, row.name);
        }
        return true;
    };
    const auto mutedLast = [](const QList<SizeListRow> &rows) {
        // Una fila "muted" cierra su nivel: la que sigue es de un nivel de arriba (o no hay).
        for (int i = 0; i + 1 < rows.size(); ++i) {
            if (rows.at(i).muted && rows.at(i + 1).depth >= rows.at(i).depth && !rows.at(i + 1).muted) {
                return false;
            }
        }
        return true;
    };
    const auto ids = [](const QList<SizeListRow> &rows) {
        QStringList out;
        for (const SizeListRow &row : rows) {
            out.append(row.id);
        }
        return out;
    };
    {
        DiskState state(nullptr);
        CleanupWindow window(&state, nullptr, true);
        window.applyFixture(QStringLiteral("folders"));
        SizeListView *folders = window.listForTab(CleanupWindow::Folders);
        const QStringList bySize = ids(folders->rows());
        check(folders->rows().size() > 5 && folders->sortColumn() == 0 && folders->sortDescending(),
              QStringLiteral("Folders arranca ordenada por Size, de mayor a menor (%1 filas)").arg(folders->rows().size()));
        window.sortList(CleanupWindow::Folders, SizeListView::kNameColumn);
        check(folders->sortColumn() == SizeListView::kNameColumn && !folders->sortDescending() && namesInOrder(folders->rows(), false)
                  && mutedLast(folders->rows()) && folders->rows().size() == bySize.size(),
              QStringLiteral("Folders por nombre: A-Z en cada nivel, lo gris al final, las mismas filas"));
        window.sortList(CleanupWindow::Folders, SizeListView::kNameColumn);
        check(folders->sortDescending() && namesInOrder(folders->rows(), true) && mutedLast(folders->rows()),
              QStringLiteral("Folders, segundo click en Name: Z-A"));
        window.sortList(CleanupWindow::Folders, 0);
        check(ids(folders->rows()) == bySize && folders->sortDescending(), QStringLiteral("Folders, de vuelta a Size: el orden de fabrica"));
        window.sortList(CleanupWindow::Folders, 0);
        QStringList reversedTop;
        for (const SizeListRow &row : folders->rows()) {
            if (row.depth == 0 && !row.muted) {
                reversedTop.prepend(row.id);
            }
        }
        QStringList top;
        for (const QString &id : bySize) {
            for (const SizeListRow &row : folders->rows()) {
                if (row.id == id && row.depth == 0 && !row.muted) {
                    top.append(id);
                }
            }
        }
        check(!folders->sortDescending() && reversedTop == top, QStringLiteral("Folders, segundo click en Size: de menor a mayor"));
        window.sortList(CleanupWindow::Folders, 3);
        check(folders->sortColumn() == 3 && folders->sortDescending() && mutedLast(folders->rows()),
              QStringLiteral("Folders por Modified: lo mas nuevo primero"));
        window.sortList(CleanupWindow::Folders, 9);
        check(folders->sortColumn() == 3, QStringLiteral("una columna que no existe no cambia nada"));

        // Escaneando no hay Modified: se ve Size, y un click en Size invierte lo que se ve.
        window.applyFixture(QStringLiteral("scanning"));
        check(folders->sortColumn() == 0 && folders->sortDescending(), QStringLiteral("escaneando, lo ordenado por Modified se ve por Size"));
        window.sortList(CleanupWindow::Folders, 0);
        check(folders->sortColumn() == 0 && !folders->sortDescending(),
              QStringLiteral("escaneando, el primer click en Size invierte lo que se ve"));

        window.applyFixture(QStringLiteral("files"));
        SizeListView *files = window.listForTab(CleanupWindow::Files);
        const int fileCount = int(files->rows().size());
        window.sortList(CleanupWindow::Files, SizeListView::kNameColumn);
        check(fileCount > 3 && namesInOrder(files->rows(), false) && files->rows().size() == fileCount,
              QStringLiteral("Largest files por nombre: A-Z, los mismos %1 archivos").arg(fileCount));

        window.applyFixture(QStringLiteral("changes"));
        SizeListView *changes = window.listForTab(CleanupWindow::Changes);
        const QStringList byChange = ids(changes->rows());
        window.sortList(CleanupWindow::Changes, SizeListView::kNameColumn);
        check(byChange.size() > 2 && namesInOrder(changes->rows(), false), QStringLiteral("What changed por carpeta: A-Z"));
        window.sortList(CleanupWindow::Changes, 0);
        check(ids(changes->rows()) == byChange, QStringLiteral("What changed por Change: lo que mas crecio primero, como de fabrica"));
        window.sortList(CleanupWindow::Changes, 1);
        check(changes->sortColumn() == 1 && changes->rows().size() == byChange.size(), QStringLiteral("What changed por Size now"));
    }

    // ---- Al tamano minimo, en los dos idiomas.
    const I18n::Language before = I18n::language();
    for (const I18n::Language language : {I18n::Language::English, I18n::Language::Spanish}) {
        I18n::setLanguage(language);
        const QString lang = language == I18n::Language::Spanish ? QStringLiteral("es") : QStringLiteral("en");
        for (const QString &fixture : CleanupWindow::fixtureStates()) {
            DiskState state(nullptr);
            CleanupWindow window(&state, nullptr, true);
            window.applyFixture(fixture);
            window.setMinimumSize(0, 0);
            window.setFixedSize(CleanupWindow::kMinWidth, CleanupWindow::kMinHeight);
            window.show();
            settle();
            auto *bar = window.findChild<QFrame *>(QStringLiteral("actionBar"));
            QList<QWidget *> parts;
            for (QWidget *child : bar->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
                if (child->isVisible()) {
                    parts.append(child);
                }
            }
            std::sort(parts.begin(), parts.end(), [](QWidget *a, QWidget *b) { return a->x() < b->x(); });
            bool fits = !parts.isEmpty() && parts.last()->geometry().right() < bar->width() - 8;
            QString squeezed;
            for (int i = 0; i < parts.size(); ++i) {
                QWidget *part = parts.at(i);
                const int wanted = qobject_cast<QLabel *>(part) ? part->minimumSizeHint().width() : part->sizeHint().width();
                if (part->width() < wanted) {
                    fits = false;
                    squeezed += QStringLiteral(" %1(%2<%3)").arg(part->objectName()).arg(part->width()).arg(wanted);
                }
                if (i > 0 && parts.at(i - 1)->geometry().right() >= part->x()) {
                    fits = false;
                }
            }
            check(fits, QStringLiteral("%1, %2 al minimo (%3x%4): la barra de abajo entra entera%5")
                            .arg(fixture, lang)
                            .arg(CleanupWindow::kMinWidth)
                            .arg(CleanupWindow::kMinHeight)
                            .arg(squeezed));
        }
    }
    I18n::setLanguage(before);

    // ---- La ventana de verdad (sin abrirla en un disco): estirable, con su minimo.
    {
        DiskState state(nullptr);
        CleanupWindow window(&state, nullptr, false);
        check(window.minimumSize() == QSize(CleanupWindow::kMinWidth, CleanupWindow::kMinHeight)
                  && window.maximumSize().width() > CleanupWindow::kWidth && window.size() == QSize(CleanupWindow::kWidth, CleanupWindow::kHeight),
              QStringLiteral("la ventana se estira: minimo %1x%2, arranca en %3x%4")
                  .arg(CleanupWindow::kMinWidth)
                  .arg(CleanupWindow::kMinHeight)
                  .arg(CleanupWindow::kWidth)
                  .arg(CleanupWindow::kHeight));
    }

    fprintf(stdout, "%s: %d fallas\n", failures == 0 ? "cleanup-sort ok" : "cleanup-sort FALLO", failures);
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
    if (probe == QLatin1String("threshold-drag")) {
        return runThresholdDragProbe();
    }
    if (probe == QLatin1String("tooltip-hover")) {
        return runTooltipProbe();
    }
    if (probe == QLatin1String("rebuild-lang")) {
        return runRebuildProbe();
    }
    if (probe == QLatin1String("match-words-typing")) {
        return runMatchWordsProbe();
    }
    if (probe == QLatin1String("tray-icon")) {
        return runTrayIconProbe();
    }
    if (probe == QLatin1String("cleanup-sort")) {
        return runCleanupSortProbe();
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
