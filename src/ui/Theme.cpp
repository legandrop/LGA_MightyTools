#include "ui/Theme.h"

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFontDatabase>
#include <QList>
#include <QPair>
#include <QPalette>
#include <QScreen>
#include <QStyleFactory>
#include <QWidget>

namespace {

// En esta app nada toma foco de teclado: Tab no recorre controles y ningun boton queda marcado al
// abrir una ventana. Se aplica a TODO widget al pulirse (antes de mostrarse por primera vez), asi
// tambien cubre los QMessageBox y el progreso del update. La excepcion son los campos donde se
// escribe texto (WA_InputMethodEnabled): el umbral de cada disco, las rutas de Open in NukeX y Match
// words de Link Redirector.
class NoKeyboardFocus : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Polish && watched->isWidgetType()) {
            auto *widget = static_cast<QWidget *>(watched);
            // Un widget con focus proxy no se toca: Qt le copia la politica a su proxy, y el viewport
            // de un QTextEdit (proxy = el propio campo) le sacaba el foco a Match words.
            if (!widget->testAttribute(Qt::WA_InputMethodEnabled) && !widget->focusProxy()) {
                widget->setFocusPolicy(Qt::NoFocus);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

// QFont y QSS solo aceptan pixeles enteros. Los tamanos del diseno (13.5px, 12.5px) se expresan
// en puntos segun el DPI logico (96 en Windows: 1px = 0.75pt). Mismo criterio que VideoDownloader.
qreal pointsPerPixel()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInchY() : 96.0;
    return 72.0 / (dpi > 0 ? dpi : 96.0);
}

QString fs(qreal px)
{
    return QString::number(px * pointsPerPixel(), 'f', 3) + QStringLiteral("pt");
}

const QString kBricolage = QStringLiteral("Bricolage Grotesque");

bool bricolageLoaded()
{
    return QFontDatabase::families().contains(kBricolage);
}

} // namespace

namespace Theme {

QFont uiFont(qreal pixelSize, int weight)
{
    QFont font(QStringLiteral("Inter"));
    font.setPointSizeF(pixelSize * pointsPerPixel());
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

QString titleFamily()
{
    return bricolageLoaded() ? kBricolage : QStringLiteral("Inter");
}

int titleWeight(int weight)
{
    // Inter viene en Regular, Medium y SemiBold: un 700 u 800 saldria sintetizado.
    return bricolageLoaded() ? weight : qMin(weight, int(QFont::DemiBold));
}

QFont titleFont(qreal pixelSize, int weight)
{
    QFont font(titleFamily());
    font.setPointSizeF(pixelSize * pointsPerPixel());
    font.setWeight(static_cast<QFont::Weight>(titleWeight(weight)));
    return font;
}

void apply(QApplication &app)
{
    // Inter embebida (los mismos archivos que VideoDownloader): el diseno se mide con ella y no
    // todas las maquinas la tienen instalada.
    for (const char *file : {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf", ":/fonts/Inter-SemiBold.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(file)) < 0) {
            qWarning("No se pudo cargar la fuente embebida %s", file);
        }
    }
    // Bricolage Grotesque (titulos, D-73): tres pesos estaticos en resources/resources.qrc. Son instancias
    // de Google Fonts con el tamano optico por defecto (14): pedidas con otro opsz, cada peso se registra
    // como otra familia («Bricolage Grotesque 16pt») y titleFont() no las encuentra. Se cargan solo si
    // estan: sin ellos titleFont() y la hoja de estilo caen a Inter SemiBold.
    for (const char *file : {":/fonts/BricolageGrotesque-SemiBold.ttf", ":/fonts/BricolageGrotesque-Bold.ttf",
                             ":/fonts/BricolageGrotesque-ExtraBold.ttf"}) {
        const QString path = QString::fromLatin1(file);
        if (QFile::exists(path) && QFontDatabase::addApplicationFont(path) < 0) {
            qWarning("No se pudo cargar la fuente embebida %s", file);
        }
    }

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // La paleta es la red de todo lo que ninguna regla de la hoja nombra (un QMessageBox, el
    // QProgressDialog del update, un tooltip): sin la vieja regla global de QWidget, eso caeria
    // al gris claro de Fusion.
    QPalette palette;
    palette.setColor(QPalette::Window, color(kWindow));
    palette.setColor(QPalette::WindowText, color(kText));
    palette.setColor(QPalette::Base, color(kField));
    palette.setColor(QPalette::AlternateBase, color(kCard));
    palette.setColor(QPalette::Text, color(kText));
    palette.setColor(QPalette::PlaceholderText, color(kTextPlaceholder));
    palette.setColor(QPalette::Button, color(kTile));
    palette.setColor(QPalette::ButtonText, color(kTextStrong));
    palette.setColor(QPalette::Highlight, color(kSelectionBg));
    palette.setColor(QPalette::HighlightedText, color(kTextBright));
    palette.setColor(QPalette::ToolTipBase, color(kTooltipBg));
    palette.setColor(QPalette::ToolTipText, color(kText));
    palette.setColor(QPalette::Link, color(kLink));
    palette.setColor(QPalette::Disabled, QPalette::Text, color(kTextPlaceholder));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, color(kTextPlaceholder));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, color(kTextPlaceholder));
    app.setPalette(palette);

    app.setFont(uiFont(14));
    app.setStyleSheet(styleSheet());
    app.installEventFilter(new NoKeyboardFocus(&app));
}

QString styleSheet()
{
    // Reglas por objectName/propiedad, NUNCA "QWidget { background }" global: esa regla pinta cada
    // label y contenedor con el fondo de la ventana y deja huecos de otro color adentro de las
    // tarjetas (LGA_Base_QT_C_Py/docs/Doc_Dialogs.md, "Un contentWidget propio tiene que declarar su
    // propio fondo"). Lo que no nombra ninguna regla toma la paleta de apply().
    //
    // Tamanos de letra: cada rol conserva el de antes del Pliego (D-73: no bajan); donde el diseno es mas
    // grande (titulos de tarjeta, nombres del indice) va el del diseno.
    QString qss = QStringLiteral(R"QSS(
QMainWindow, QWidget#central { background-color: @desk; }
QWidget#content { background-color: @window; }
QLabel { background: transparent; color: @text; }
/* El tooltip es propio (ui/CustomTooltip): su fondo, borde y flecha se pintan por codigo. */
QLabel#tooltipLabel { color: @text; background-color: transparent; font-size: @fs13; font-weight: 400; }

/* Barra de titulo propia: la tinta de la mesa, el nombre en la fuente de titulos. */
QLabel#titleBarTitle { color: @textBright; font-family: "@titleFamily"; font-size: @fs14; font-weight: @tw700; }
QFrame#titleBarSeparator { background-color: @divider; border: none; }

/* Tarjeta (`.card`): radio 10 y borde apenas mas claro que el fondo; warn y err lo tinen. */
QFrame#card { background-color: @card; border: 1px solid @cardBorder; border-radius: 10px; }
QLabel#cardTitle { color: @textBright; font-family: "@titleFamily"; font-size: @fs16; font-weight: @tw700; }
QLabel#caption { color: @textMuted; font-size: @fs13; }
QLabel#caption[tone="err"] { color: @error; }
QLabel#optionLabel, ElidedLabel#optionLabel { color: @text; font-size: @fs13_5; }
QLabel#meta { color: @textFaint; font-size: @fs12; }
QFrame#divider { background-color: @divider; border: none; min-height: 1px; max-height: 1px; }


QFrame#card[tone="warn"] { border: 1px solid @warnBorder; }
QFrame#card[tone="err"] { border: 1px solid @errCardBorder; }
/* El estado vivo de una herramienta, subido al encabezado de su hoja (`.live`): sin caja en calma; con un
   aviso o un error, la misma tarjeta con borde de color que en el resto de las herramientas. */
QFrame#card[live="true"] { background-color: transparent; border: none; }
QFrame#card[live="true"][alert="warn"] { background-color: @card; border: 1px solid @warnBorder; }
QFrame#card[live="true"][alert="err"] { background-color: @card; border: 1px solid @errCardBorder; }
QLabel#liveTitle { color: @textBright; font-size: @fs14; font-weight: 600; }

/* Fila de un atajo: nombre, teclas y el lapiz. Grabando, las teclas se reemplazan por el campo
   con borde magenta y un rombo magenta (StatusDot). */
QLabel#shortcutName { color: @textStrong; font-size: @fs14; font-weight: 500; }
QFrame#recorder { background-color: @field; border: 1px solid @accent; border-radius: 5px; min-height: 26px; max-height: 26px; }
QLabel#recorderText { color: @textStrong; font-size: @fs12_5; }

/* Tarjeta del Dope Sheet */
QLabel#spotValue { color: @textStrong; font-size: @fs14; font-weight: 600; }

/* Tarjeta de espacio en disco. */
QLabel#caption[tone="warn"] { color: @warn; }
QLabel#driveLetter { color: @textBright; font-family: "@titleFamily"; font-size: @fs21; font-weight: @tw800; }
QLabel#driveLetter[dim="true"] { color: @textPlaceholder; }
ElidedLabel#driveName { color: @textStrong; font-size: @fs14; font-weight: 500; }
ElidedLabel#driveName[dim="true"] { color: @textFaint; }
QSpinBox#threshold {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 6px; color: @textStrong;
    font-size: @fs13; padding: 0px 8px 0px 4px; selection-background-color: @selectionBg; selection-color: @textBright;
}
QSpinBox#threshold:focus { border-color: @accent; }
/* Segmentado GB | % (`.seg`): una caja de campo con dos botones; el elegido, en magenta (Â«ElegidoÂ»). */
QPushButton#segButton {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 0px; color: @textFaint;
    padding: 0px 9px; min-height: 26px; max-height: 26px; font-size: @fs12; font-weight: 600;
}
QPushButton#segButton[pos="left"] { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }
QPushButton#segButton[pos="right"] { border-top-right-radius: 6px; border-bottom-right-radius: 6px; border-left: none; }
QPushButton#segButton:hover { color: @textStrong; }
QPushButton#segButton:checked { background-color: @chosenBg; border: 1px solid @chosenBorder; color: @chosenText; }
QPushButton#segButton[pos="right"]:checked { border-left: 1px solid @chosenBorder; }
/* Switch segmentado (SegmentedSwitch): la misma caja de campo; el segmento elegido, en magenta (Â«ElegidoÂ»). */
QWidget#segmentSwitch { background-color: @field; border: 1px solid @fieldBorder; border-radius: 6px; }
QPushButton#segment {
    background-color: transparent; color: @textFaint; border: 1px solid transparent; border-radius: 4px;
    padding: 0px 9px; min-height: 20px; max-height: 20px; font-size: @fs12_5; font-weight: 600;
}
QPushButton#segment:hover { color: @textStrong; }
QPushButton#segment:disabled { color: @textPlaceholder; }
QPushButton#segment:checked { background-color: @chosenBg; border: 1px solid @chosenBorder; color: @chosenText; }
/* Desplegable (`.pick`). */
QPushButton#fieldButton {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 6px; color: @textStrong;
    padding: 0px 9px 0px 10px; min-height: 26px; max-height: 26px; font-size: @fs13; font-weight: 400;
}
QPushButton#fieldButton:hover { border-color: @borderStrong; background-color: #15151A; }
QPushButton#linkButton {
    background-color: transparent; border: none; color: @link; padding: 0px; min-height: 18px; max-height: 18px;
    font-size: @fs13; font-weight: 600; text-align: left;
}
QPushButton#linkButton:hover { color: @linkHover; }

/* Calibrador: pasos numerados en su caja magenta y la burbuja que sigue al puntero. */
QLabel#stepBadge { background-color: @primary; color: @textOnAccent; border-radius: 4px; min-width: 22px; max-width: 22px; min-height: 22px; max-height: 22px; font-size: @fs11; font-weight: 600; }
QLabel#stepText { color: @text; font-size: @fs13; }
QLabel#bubbleTitle { color: @textBright; font-size: @fs13; font-weight: 600; }
QLabel#bubbleTitle[tone="err"] { color: @error; }
QLabel#bubbleMeta { color: @textFaint; font-size: @fs12; }

/* Botones (`.btn`): misma caja en todos los estados. */
QPushButton {
    background-color: @tile; color: @textStrong; border: 1px solid @borderStrong; border-radius: 6px;
    padding: 0px 14px; min-height: 28px; max-height: 28px; font-size: @fs13; font-weight: 500;
}
QPushButton:hover { background-color: #33333B; }
QPushButton:pressed { background-color: #24242A; }
QPushButton:disabled { background-color: #1C1C21; color: @textPlaceholder; border-color: #26262D; }
QPushButton[variant="primary"] { background-color: @primary; border: 1px solid @primaryBorder; color: @textOnAccent; font-weight: 600; }
QPushButton[variant="primary"]:hover { background-color: @primaryHover; }
QPushButton[variant="primary"]:pressed { background-color: #9E2860; }
QPushButton[variant="primary"]:disabled { background-color: #3A1A2B; border-color: #4A2338; color: #9A7387; }
QPushButton[variant="ghost"] { background-color: transparent; border: 1px solid transparent; color: @textMuted; padding: 0px 8px; }
QPushButton[variant="ghost"]:hover { background-color: @tile; color: @textStrong; }
QPushButton[btnSize="sm"] { min-height: 24px; max-height: 24px; font-size: @fs12_5; padding: 0px 10px; }
QPushButton[btnSize="icon"] { min-height: 26px; max-height: 26px; min-width: 26px; max-width: 26px; padding: 0px; border-radius: 6px; }
/* "Elegido" entre varios botones del mismo tipo, sin ser la accion principal: la version de Nuke elegida
   en Open in NukeX. Magenta, como todo lo que elige el usuario. */
QPushButton[chosen="true"] { background-color: @chosenBg; border: 1px solid @chosenBorder; color: @chosenText; }
QPushButton[chosen="true"]:hover { background-color: @chosenHover; }

/* Campo de solo lectura y campos de texto de una linea (`.fld`). */
QFrame#field { background-color: @field; border: 1px solid @fieldBorder; border-radius: 6px; min-height: 28px; max-height: 28px; }
ElidedLabel#fieldValue { color: @textStrong; font-size: @fs13_5; }
ElidedLabel#fieldValue[empty="true"] { color: @textPlaceholder; }
QLineEdit#pathField, QLineEdit#codeLine {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 6px; color: @textStrong;
    padding: 0px 9px; min-height: 28px; max-height: 28px; font-size: @fs13;
    selection-background-color: @selectionBg; selection-color: @textBright;
}
QLineEdit#pathField:focus, QLineEdit#codeLine:focus { border-color: @accent; }

/* Chips y badges (`.bdg`): 22 de alto con el borde. ok = cian, warn = amarillo lleno, err = rojo. */
QFrame#chip { background-color: @tile; border: 1px solid @borderStrong; border-radius: 4px; min-height: 20px; max-height: 20px; }
QFrame#chip QLabel { color: @textStrong; font-size: @fs12; font-weight: 600; }
QFrame#chip[tone="ok"] { background-color: transparent; border-color: @okBorder; }
QFrame#chip[tone="ok"] QLabel { color: @ok; }
QFrame#chip[tone="err"] { background-color: @errBg; border-color: @errBorder; }
QFrame#chip[tone="err"] QLabel { color: @error; }
QFrame#chip[tone="src"] { background-color: transparent; border-color: @borderStrong; }
QFrame#chip[tone="src"] QLabel { color: @textMuted; font-weight: 500; }
QFrame#chip[tone="warn"] { background-color: @warn; border-color: @warn; }
QFrame#chip[tone="warn"] QLabel { color: @warnInk; }
/* Tecla (`.kc`): borde de 1, borde inferior grueso y oscuro. */
QFrame#chip[tone="key"] { background-color: @tile; border: 1px solid @keyBorder; border-bottom: 3px solid @keyBottom; border-radius: 5px; min-height: 20px; max-height: 20px; }
QFrame#chip[tone="key"] QLabel { color: @textStrong; }
QFrame#chip[tone="keyDim"] { background-color: transparent; border: 1px dashed @borderStrong; border-radius: 5px; min-height: 22px; max-height: 22px; }
QFrame#chip[tone="keyDim"] QLabel { color: @textFaint; }

/* Menu del tray y desplegables: la tinta de la mesa, hover magenta (la eleccion del usuario). Sin radio:
   un QMenu de nivel superior sin translucidez pinta las esquinas de negro. */
QMenu { background-color: #1B1B20; border: 1px solid @borderStrong; padding: 5px 0px; }
QMenu::item { color: @textStrong; padding: 5px 16px 5px 14px; background: transparent; font-size: @fs14; }
QMenu::item:selected { background-color: @primary; color: @textOnAccent; }
QMenu::item:disabled { color: @textFaint; }
QMenu::separator { height: 1px; background: @divider; margin: 5px 8px; }
/* Item marcable (el combo de Link Redirector, el intervalo de Disk Space): la marca es un tilde magenta,
   no el cuadrado/tilde nativo de Fusion. Sin marca en el desmarcado. */
QMenu::indicator { width: 14px; height: 14px; }
QMenu::indicator:unchecked { image: none; }
QMenu::indicator:checked { image: url(:/icons/check_link.png); }

/* Dialogos: el de update, los QMessageBox y el progreso de descarga (la hoja). */
QDialog#updateDialog, QMessageBox, QProgressDialog { background-color: @dialog; }
QLabel#dialogTitle { color: @textBright; font-family: "@titleFamily"; font-size: @fs16; font-weight: @tw700; }
QProgressBar { background-color: @tile; border: none; border-radius: 1px; min-height: 8px; max-height: 8px; color: transparent; }
QProgressBar::chunk { background-color: @ok; border-radius: 1px; }

/* Ayuda por pagina («How it works», D-73): franja plegable debajo del encabezado. */
QFrame#howItWorks { background-color: transparent; border: 1px solid @divider; border-radius: 10px; }
QFrame#howItWorks[open="true"] { background-color: @card; border-color: @border; }
QLabel#howTitle { color: @textStrong; font-size: @fs13_5; font-weight: 600; }
QLabel#howCount { color: @textFaint; font-size: @fs12; }
QLabel#howNumber { color: @textBright; font-family: "@titleFamily"; font-size: @fs19; font-weight: @tw800; }
QLabel#howStep { color: @text; font-size: @fs13_5; }
QLabel#howNote { color: @textCaption; font-size: @fs12_5; }
QFrame#howRule { background-color: @border; border: none; min-height: 1px; max-height: 1px; }

/* Ventana: indice lateral sobre la mesa y la hoja de cada pagina. */
QLabel#sideLabel { color: @textFaint; font-size: @fs11; font-weight: 600; }
QLabel#navLetter { color: @navLetter; font-family: "@titleFamily"; font-size: @fs13; font-weight: @tw800; }
QLabel#sideName { color: #A3A2A9; font-family: "@titleFamily"; font-size: @fs14; font-weight: @tw600; }
QLabel#sideName[on="true"] { color: #DCDAD3; }
QLabel#sideName[sel="true"] { color: #F3F0E9; }
QLabel#sideStatus { color: #85848C; font-size: @fs12; }
QLabel#sideStatus[tone="ok"] { color: @statusOk; }
QLabel#sideStatus[tone="warn"] { color: @warn; }
QLabel#sideStatus[tone="err"] { color: @error; }
QScrollArea#pane, QWidget#paneContent { background: transparent; border: none; }
/* Scroll vertical fino (`.sbar` del diseno: 4 px, sin flechas). */
QScrollBar:vertical { background: transparent; width: 8px; margin: 6px 4px 6px 0px; }
QScrollBar::handle:vertical { background: @borderStrong; border-radius: 2px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #4A4A55; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; border: none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QLabel#pageTitle { color: @textBright; }
QLabel#platTag { color: #A9A8AF; font-size: @fs11_5; font-weight: 500; border: 1px solid @platBorder; border-radius: 4px; padding: 0px 6px; min-height: 18px; max-height: 18px; }
QLabel#pageDescription { color: @textMuted; font-size: @fs13_5; }
QLabel#offBullet { color: @text; font-size: @fs13; }
QLabel#welcomeTitle { color: @textBright; font-family: "@titleFamily"; font-size: @fs20; font-weight: @tw800; }
QFrame#miniCard { background-color: @miniCard; border: 1px solid @cardBorder; border-radius: 8px; }
QLabel#miniTitle { color: @textStrong; font-family: "@titleFamily"; font-size: @fs14; font-weight: @tw600; }
QLabel#runningText { color: @text; font-size: @fs13; }
QLabel#meta[tone="ok"] { color: @ok; }
QLabel#meta[tone="warn"] { color: @warn; }
QLabel#linkLabel { color: @link; font-size: @fs13; font-weight: 600; text-decoration: none; }
QLabel#linkLabel[hover="true"] { color: @linkHover; }
QLabel#traySection { color: @textFaint; font-size: @fs11; font-weight: 600; padding: 0px 16px 0px 14px; min-height: 22px; max-height: 22px; background: transparent; }

/* Colofon de General (`.colophon`): logo, version, autor, sitio y la linea de la bandeja. */
QLabel#colophonName { color: @textBright; font-family: "@titleFamily"; font-size: @fs16; font-weight: @tw700; }
QLabel#colophonText { color: @textCaption; font-size: @fs13; }
QFrame#colophonRule { background-color: @divider; border: none; min-height: 1px; max-height: 1px; }

/* Ventana de limpieza de Disk Space. */
QWidget#cleanupWindow { background-color: @window; }
QScrollArea#cleanScroll, QWidget#cleanContent { background-color: @window; border: none; }
QLabel#meterText, ElidedLabel#meterText { color: @textFaint; font-size: @fs12_5; }
QFrame#actionBar { background-color: @desk; border: none; border-top: 1px solid @divider; }
QLabel#actionText { color: @text; font-size: @fs13; }
QPushButton[variant="danger"] {
    background-color: @errBg; color: @error; border: 1px solid @errBorder;
    min-height: 26px; max-height: 26px; padding: 0px 11px;
}
QPushButton[variant="danger"][btnSize="sm"] { min-height: 22px; max-height: 22px; padding: 0px 9px; }
QPushButton[variant="danger"]:hover { background-color: #45222A; }
QPushButton[variant="danger"]:pressed { background-color: #2E1719; }
QPushButton[variant="danger"]:disabled { background-color: #1C1C21; color: @textPlaceholder; border: 1px solid #26262D; }
QPushButton#filterChip {
    background-color: transparent; border: 1px solid @borderStrong; border-radius: 12px; color: @textMuted;
    padding: 0px 9px; min-height: 22px; max-height: 22px; font-size: @fs12; font-weight: 500;
}
QPushButton#filterChip:hover { color: @textStrong; }
QPushButton#filterChip:checked { background-color: @chosenBg; border: 1px solid @chosenBorder; color: @chosenText; }
QLabel#cleanName { color: @textStrong; font-size: @fs13_5; font-weight: 500; }
QLabel#cleanCaption { color: @textFaint; font-size: @fs12; }
QLabel#cleanGroupCaption { color: @textFaint; font-size: @fs12_5; }
QLabel#cleanSize { color: @textStrong; font-size: @fs13; font-weight: 500; }
QLabel#cleanSize[dim="true"] { color: @textFaint; font-weight: 400; }
QLabel#itemSize { color: @textStrong; font-size: @fs13; }
QLabel#itemSize[dim="true"] { color: @textFaint; }
QLabel#itemName, ElidedLabel#itemName { color: @text; font-size: @fs13; }
QLabel#itemName[off="true"], ElidedLabel#itemName[off="true"] { color: @textFaint; }
ElidedLabel#itemPath { color: @textFaint; font-size: @fs12; }
QWidget#cleanItem { border-radius: 4px; background-color: transparent; }
QWidget#cleanItem:hover { background-color: @listHover; }
QFrame#okBanner { background-color: @okBg; border: 1px solid @okBorder; border-radius: 6px; }
QFrame#accessStrip { background-color: @warn; border: none; }
QLabel#accessText { color: @warnInk; font-size: @fs12_5; font-weight: 500; }
QLabel#bannerText { color: @ok; font-size: @fs13; font-weight: 500; }
QLabel#dialogLine, ElidedLabel#dialogLine { color: @text; font-size: @fs13; }
QLabel#dialogValue { color: @text; font-size: @fs13; }
ElidedLabel#dialogPath { color: @textStrong; font-family: "JetBrains Mono", Consolas, monospace; font-size: @fs12; }
QScrollArea#dialogScroll, QScrollArea#dialogScroll > QWidget > QWidget { background: transparent; border: none; }
QLineEdit#ruleField {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 6px; color: @textStrong;
    padding: 0px 8px; min-height: 26px; max-height: 26px; font-size: @fs13;
    selection-background-color: @selectionBg; selection-color: @textBright;
}
QLineEdit#ruleField:focus { border-color: @accent; }
QLineEdit#ruleField:disabled { color: @textPlaceholder; }
QPlainTextEdit#ruleArea {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 6px; color: @textStrong;
    padding: 2px 6px; font-size: @fs13;
    selection-background-color: @selectionBg; selection-color: @textBright;
}
QPlainTextEdit#ruleArea:focus { border-color: @accent; }
)QSS");

    const QList<QPair<const char *, QString>> tokens = {
        {"@titleFamily", titleFamily()},
        {"@tw600", QString::number(titleWeight(600))}, {"@tw700", QString::number(titleWeight(700))},
        {"@tw800", QString::number(titleWeight(800))},
        {"@statusOk", kStatusOk}, {"@listSelected", kListSelected}, {"@listHover", kListHover}, {"@navLetter", kNavLetter},
        {"@platBorder", kPlatBorder}, {"@miniCard", kMiniCard},
        {"@chosenBg", kChosenBg}, {"@chosenHover", kChosenHover}, {"@chosenBorder", kChosenBorder}, {"@chosenText", kChosenText},
        {"@okBg", kOkBg}, {"@okBorder", kOkBorder}, {"@errBg", kErrBg}, {"@errBorder", kErrBorder},
        {"@errCardBorder", kErrCardBorder}, {"@warnBorder", kWarnBorder}, {"@warnInk", kWarnInk},
        {"@selectionBg", kSelectionBg}, {"@keyBorder", kKeyBorder}, {"@keyBottom", kKeyBottom},
        {"@primaryBorder", kPrimaryBorder}, {"@primaryHover", kPrimaryHover}, {"@primary", kPrimary},
        {"@textOnAccent", kTextOnAccent}, {"@borderStrong", kBorderStrong}, {"@cardBorder", kCardBorder},
        {"@desk", kDesk}, {"@window", kWindow}, {"@card", kCard}, {"@tile", kTile}, {"@fieldBorder", kFieldBorder},
        {"@field", kField}, {"@border", kBorder}, {"@divider", kDivider}, {"@dialog", kDialog},
        {"@textStrong", kTextStrong}, {"@textBright", kTextBright}, {"@textMuted", kTextMuted},
        {"@textCaption", kTextCaption}, {"@textFaint", kTextFaint}, {"@textPlaceholder", kTextPlaceholder},
        {"@linkHover", kLinkHover}, {"@link", kLink}, {"@text", kText}, {"@ok", kOk}, {"@error", kError},
        {"@warn", kWarn}, {"@accent", kAccent}, {"@icon", kIcon},
        {"@fs13_5", fs(13.5)}, {"@fs12_5", fs(12.5)}, {"@fs11_5", fs(11.5)}, {"@fs11", fs(11)},
        {"@fs21", fs(21)}, {"@fs20", fs(20)}, {"@fs19", fs(19)}, {"@fs16", fs(16)}, {"@fs14", fs(14)},
        {"@fs13", fs(13)}, {"@fs12", fs(12)},
    };
    // Orden: los nombres largos primero ("@textStrong" antes que "@text", "@fs13_5" antes que "@fs13",
    // "@primaryBorder" antes que "@primary", "@linkHover" antes que "@link").
    for (const auto &token : tokens) {
        qss.replace(QLatin1String(token.first), token.second);
    }
    return qss;
}

QString fontSize(qreal px)
{
    return fs(px);
}

} // namespace Theme
