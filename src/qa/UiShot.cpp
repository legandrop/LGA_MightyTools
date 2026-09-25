#include "qa/UiShot.h"

#include "core/AppState.h"
#include "tray/TrayMenu.h"
#include "ui/DiskCard.h"
#include "ui/CalibrationDialog.h"
#include "ui/CalibrationSession.h"
#include "ui/HelpDialog.h"
#include "ui/MainWindow.h"
#include "ui/ShortcutRow.h"
#include "ui/TitleBar.h"
#include "ui/UiWidgets.h"
#include "updates/UpdateDialog.h"

#include <QAbstractButton>
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
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QMenu>
#include <QPixmap>
#include <QSaveFile>
#include <QScopedPointer>
#include <QVBoxLayout>

#include <cstdio>

// Modelo: src/qa/UiShot.cpp de LGA_FolderSwitch (que viene de LGA_VideoDownloader). Nunca se llama
// show() sobre una ventana de nivel superior (WA_DontShowOnScreen + render), asi que no aparece nada
// ni se toma el foco.
//
// Lo que NO se construye aca, a proposito: QSystemTrayIcon, TrayController, HotkeyService,
// NukeWatcher, InputInjector, CalibrationSession y UpdateService. Uno solo de esos pondria un icono
// en la bandeja real, registraria un atajo global, moveria el mouse o saldria a la red.

namespace {

const QStringList kStates = {
    QStringLiteral("on"),
    QStringLiteral("outside-nuke"),
    QStringLiteral("paused"),
    QStringLiteral("taken"),
    QStringLiteral("permission"),
    QStringLiteral("not-calibrated"),
    QStringLiteral("recording"),
    QStringLiteral("rejected"),
    QStringLiteral("help"),
    // Hover sin mouse: se marca el widget como "debajo del mouse" y se dibuja. Prueba la pintura
    // del hover, no que el evento llegue.
    QStringLiteral("hover-help"),
    QStringLiteral("hover-close"),
    QStringLiteral("tray-menu"),
    QStringLiteral("calibrate-dialog"),
    QStringLiteral("calibrate-bubble"),
    QStringLiteral("calibrate-bubble-outside"),
    QStringLiteral("update-dialog"),
    // Chequeo de espacio en disco (discos de prueba, nunca los de la maquina).
    QStringLiteral("disks"),
    QStringLiteral("disks-low"),
    QStringLiteral("disks-missing"),
    QStringLiteral("disks-empty"),
    QStringLiteral("disks-add-menu"),
    QStringLiteral("tray-menu-disk-low"),
};

// Discos de prueba en GiB: los mismos numeros que el diseno aprobado.
DriveInfo fixtureDrive(const char *root, const char *label, const char *name, double totalGb, double freeGb)
{
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    DriveInfo drive;
    drive.root = QString::fromLatin1(root);
    drive.label = QString::fromLatin1(label);
    drive.name = QString::fromLatin1(name);
    drive.totalBytes = qint64(totalGb * kGiB);
    drive.freeBytes = qint64(freeGb * kGiB);
    return drive;
}

// Carga los discos del estado pedido en AppState. No lee ningun disco real.
void applyDiskFixture(AppState &state, const QString &name)
{
    QList<DriveInfo> drives = {
        fixtureDrive("C:/", "C:", "Windows", 931, 182),
        fixtureDrive("D:/", "D:", "Cache", 1863, name == QLatin1String("disks-low") || name == QLatin1String("tray-menu-disk-low") ? 42 : 640),
        fixtureDrive("E:/", "E:", "Renders", 3726, 1210),
        fixtureDrive("F:/", "F:", "Backup", 7452, 3100),
    };
    if (name == QLatin1String("disks-missing")) {
        drives.removeAt(2); // E: desenchufado
    }
    if (name == QLatin1String("disks-empty")) {
        return;
    }
    state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
    state.setDiskThreshold(QStringLiteral("C:/"), 50, DiskWatch::Unit::GB);
    state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
    state.setDiskThreshold(QStringLiteral("D:/"), 100, DiskWatch::Unit::GB);
    if (name != QLatin1String("disks") && name != QLatin1String("disks-add-menu")) {
        state.addDiskWatch(QStringLiteral("E:/"), QStringLiteral("Renders"));
        state.setDiskThreshold(QStringLiteral("E:/"), 15, DiskWatch::Unit::Percent);
    }
    state.setDriveReadings(drives, QStringList(), true, QDateTime(QDate(2026, 9, 24), QTime(12, 41)));
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

// Un lienzo oscuro para lo que en la app es una ventana propia (el menu, la burbuja).
QWidget *makeCanvas()
{
    auto *canvas = new QWidget;
    canvas->setObjectName(QStringLiteral("central"));
    canvas->setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas->setStyleSheet(QStringLiteral("QWidget#central { background-color: #101010; }"));
    return canvas;
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
    if (index < 0 || index + 2 >= args.size()) {
        fprintf(stderr, "usage: --ui-shot <%s> <out.png> [--dpr <1..3>]\n", qPrintable(kStates.join('|')));
        return 2;
    }
    const QString state = args.at(index + 1);
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
    if (!kStates.contains(state)) {
        fprintf(stderr, "ui-shot: unknown state '%s'\n", qPrintable(state));
        return 2;
    }
    if (!outPath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) || QFileInfo::exists(outPath)
        || !QFileInfo(outPath).dir().exists()) {
        fprintf(stderr, "ui-shot: output must be a new .png in an existing folder (%s)\n", qPrintable(outPath));
        return 2;
    }

    // AppState sin persistencia: nunca lee ni escribe settings.ini. Todo el estado es del fixture.
    AppState appState(AppState::Persistence::None);
    appState.setNukeInFront(true);
    appState.setDopeSheetSpot(QPointF(0.89, 0.72));
    appState.setRegistration(ShortcutAction::AddKeyframe, AppState::Registration::Registered);
    appState.setRegistration(ShortcutAction::FrameDopeSheet, AppState::Registration::Registered);
    if (state == QLatin1String("outside-nuke")) {
        // Lo que se ve siempre con la ventana abierta: Nuke no esta al frente y los atajos estan
        // sueltos. Tiene que decir "activos" igual (no "esperando a Nuke").
        appState.setNukeInFront(false);
        appState.setRegistration(ShortcutAction::AddKeyframe, AppState::Registration::Idle);
        appState.setRegistration(ShortcutAction::FrameDopeSheet, AppState::Registration::Idle);
    } else if (state == QLatin1String("paused")) {
        appState.setEnabled(false);
    } else if (state == QLatin1String("taken")) {
        appState.setRegistration(ShortcutAction::FrameDopeSheet, AppState::Registration::Failed);
    } else if (state == QLatin1String("permission")) {
        appState.setAccessibilityGranted(false);
    }
    if (state.startsWith(QLatin1String("disks")) || state == QLatin1String("tray-menu-disk-low")) {
        applyDiskFixture(appState, state);
    }

    MainWindow mainWindow(&appState, MainWindow::Mode::Capture);
    mainWindow.setAttribute(Qt::WA_DontShowOnScreen, true);
    mainWindow.applyAutoStartFixture(true, true);
    mainWindow.refresh();
    settle(mainWindow);

    QWidget *root = &mainWindow;
    QScopedPointer<QWidget> canvas;
    QScopedPointer<AppState> altState;
    QScopedPointer<MainWindow> altWindow;

    if (state == QLatin1String("not-calibrated")) {
        // El fixture comun ya guarda un punto y AppState no lo borra: otro estado, sin punto.
        altState.reset(new AppState(AppState::Persistence::None));
        altState->setNukeInFront(true);
        altState->setRegistration(ShortcutAction::AddKeyframe, AppState::Registration::Registered);
        altState->setRegistration(ShortcutAction::FrameDopeSheet, AppState::Registration::Registered);
        altWindow.reset(new MainWindow(altState.data(), MainWindow::Mode::Capture));
        altWindow->setAttribute(Qt::WA_DontShowOnScreen, true);
        altWindow->applyAutoStartFixture(true, true);
        altWindow->refresh();
        settle(*altWindow);
        root = altWindow.data();
    } else if (state == QLatin1String("recording")) {
        mainWindow.shortcutRow(ShortcutAction::AddKeyframe)->showRecordingFixture(QStringLiteral("Ctrl + Shift + ..."));
        mainWindow.refresh();
        settle(mainWindow);
    } else if (state == QLatin1String("rejected")) {
        mainWindow.shortcutRow(ShortcutAction::AddKeyframe)
            ->setError(QStringLiteral("Ctrl+Alt+K is taken by another app. Kept Ctrl+Shift+D."));
        mainWindow.refresh();
        settle(mainWindow);
    } else if (state == QLatin1String("hover-help") || state == QLatin1String("hover-close")) {
        const QString name = state == QLatin1String("hover-help") ? QStringLiteral("Help") : QStringLiteral("Close");
        for (QAbstractButton *button : mainWindow.titleBar()->findChildren<QAbstractButton *>()) {
            if (button->accessibleName() == name) {
                button->setAttribute(Qt::WA_UnderMouse, true);
                button->update();
            }
        }
        settle(mainWindow);
    } else if (state == QLatin1String("help")) {
        // Velo y dialogo como hijos comunes de la ventana, no ventanas propias: se dibujan con el
        // mismo render y no hay nada que mostrar.
        auto *scrim = new Scrim(mainWindow.centralWidget());
        scrim->setVisible(true);
        auto *help = new HelpDialog(appState.shortcut(ShortcutAction::AddKeyframe),
                                    appState.shortcut(ShortcutAction::FrameDopeSheet), mainWindow.centralWidget());
        help->setWindowFlags(Qt::Widget);
        help->fitHeight();
        help->move((mainWindow.width() - help->width()) / 2, (mainWindow.height() - help->height()) / 2);
        help->setVisible(true);
        settle(mainWindow);
    } else if (state == QLatin1String("disks-add-menu")) {
        // El menu de "Add drive..." como widget, armado por la misma tarjeta que lo abre en la app.
        canvas.reset(makeCanvas());
        auto *layout = new QVBoxLayout(canvas.data());
        layout->setContentsMargins(20, 20, 20, 20);
        auto *menu = new QMenu(canvas.data());
        menu->setWindowFlags(Qt::Widget);
        mainWindow.diskCard()->fillAddMenu(menu);
        if (menu->actions().size() > 1) {
            menu->setActiveAction(menu->actions().at(1));
        }
        layout->addWidget(menu);
        root = canvas.data();
        settle(*root);
        root->adjustSize();
        settle(*root);
    } else if (state == QLatin1String("tray-menu") || state == QLatin1String("tray-menu-disk-low")) {
        canvas.reset(makeCanvas());
        auto *layout = new QVBoxLayout(canvas.data());
        layout->setContentsMargins(20, 16, 20, 20);
        layout->setSpacing(14);
        // Iconos de la bandeja activo y en pausa, como los pinta TrayController (sin QSystemTrayIcon).
        auto *icons = new QHBoxLayout();
        icons->setSpacing(18);
        for (const bool paused : {false, true}) {
            auto *icon = new QLabel(canvas.data());
            icon->setObjectName(paused ? QStringLiteral("trayIconPaused") : QStringLiteral("trayIconOn"));
            QPixmap px = trayIcon(paused).pixmap(QSize(16, 16), dpr);
            px.setDevicePixelRatio(dpr);
            icon->setPixmap(px);
            icons->addWidget(icon);
        }
        icons->addStretch(1);
        layout->addLayout(icons);
        auto *menu = new QMenu(canvas.data());
        menu->setWindowFlags(Qt::Widget);
        TrayMenuActions actions = buildTrayMenu(menu);
        refreshTrayMenu(actions, true);
        if (state == QLatin1String("tray-menu-disk-low")) {
            refreshTrayDiskWarnings(menu, actions, diskWarningLines(appState));
        }
        menu->setActiveAction(actions.settings);
        layout->addWidget(menu);
        root = canvas.data();
        settle(*root);
        root->adjustSize();
        settle(*root);
    } else if (state == QLatin1String("calibrate-dialog")) {
        auto *dialog = new CalibrationDialog();
        dialog->setAttribute(Qt::WA_DontShowOnScreen, true);
        dialog->fitHeight();
        canvas.reset(dialog);
        root = canvas.data();
        settle(*root);
    } else if (state == QLatin1String("calibrate-bubble") || state == QLatin1String("calibrate-bubble-outside")) {
        canvas.reset(makeCanvas());
        auto *layout = new QVBoxLayout(canvas.data());
        layout->setContentsMargins(20, 20, 20, 20);
        auto *bubble = new CalibrationBubble(canvas.data());
        bubble->setWindowFlags(Qt::Widget);
        if (state == QLatin1String("calibrate-bubble")) {
            bubble->showOverNuke(QPointF(0.89, 0.72));
        } else {
            bubble->showOutside(true);
        }
        layout->addWidget(bubble);
        root = canvas.data();
        settle(*root);
        root->adjustSize();
        settle(*root);
    } else if (state == QLatin1String("update-dialog")) {
        canvas.reset(createUpdateAvailableDialog(nullptr, QStringLiteral("LGA Mighty Tools"), QStringLiteral("2.1"),
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

int runUiProbe(const QStringList &args)
{
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        fprintf(stderr, "ui-probe: requires QT_QPA_PLATFORM=offscreen (platform is '%s')\n",
                qPrintable(QGuiApplication::platformName()));
        return 2;
    }
    const QString probe = args.value(args.indexOf(QStringLiteral("--ui-probe")) + 1);
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

    // AppState sin persistencia y un disco de prueba: no se lee ni se escribe nada real.
    AppState state(AppState::Persistence::None);
    state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
    state.setDriveReadings({fixtureDrive("C:/", "C:", "Windows", 931, 182)}, QStringList(), true, QDateTime::currentDateTime());

    // La tarjeta interactiva real (con sus connects y su filtro de clicks), sola en una ventana de la
    // plataforma offscreen: show() ahi no llega a ninguna pantalla.
    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    auto *card = new DiskCard(&state, true, &host);
    layout->addWidget(card);
    QObject::connect(&state, &AppState::changed, card, &DiskCard::refresh);
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
