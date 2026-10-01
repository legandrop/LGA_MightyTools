#include "ui/Theme.h"

#include <QApplication>
#include <QEvent>
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

} // namespace

namespace Theme {

QFont uiFont(qreal pixelSize, int weight)
{
    QFont font(QStringLiteral("Inter"));
    font.setPointSizeF(pixelSize * pointsPerPixel());
    font.setWeight(static_cast<QFont::Weight>(weight));    return font;
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
    palette.setColor(QPalette::Button, QColor(0x2a, 0x2a, 0x2a));
    palette.setColor(QPalette::ButtonText, color(kText));
    palette.setColor(QPalette::Highlight, QColor(0x39, 0x34, 0x55));
    palette.setColor(QPalette::HighlightedText, color(kTextBright));
    palette.setColor(QPalette::ToolTipBase, color(kTile));
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
    QString qss = QStringLiteral(R"QSS(
QMainWindow, QWidget#central, QWidget#content, QWidget#helpPage { background-color: @window; }
QLabel { background: transparent; color: @text; }
/* El tooltip es propio (ui/CustomTooltip): su fondo, borde y flecha se pintan por codigo. */
QLabel#tooltipLabel { color: @text; background-color: transparent; font-size: @fs13; font-weight: 400; }

/* Barra de titulo propia */
QLabel#titleBarTitle { color: @textMuted; font-size: @fs13; font-weight: 500; }
QFrame#titleBarSeparator { background-color: @border; border: none; }

/* Tarjeta: 1 px de borde transparente como `.card` del canvas (el de color lo ponen warn y err). */
QFrame#card { background-color: @card; border: 1px solid transparent; border-radius: 8px; }
QLabel#cardTitle { color: @textStrong; font-size: @fs14; font-weight: 600; }
QLabel#caption { color: @textFaint; font-size: @fs13; }
QLabel#caption[tone="err"] { color: @error; }
QLabel#optionLabel { color: @text; font-size: @fs13_5; }
QLabel#meta { color: @textFaint; font-size: @fs12; }
QFrame#divider { background-color: @divider; border: none; min-height: 1px; max-height: 1px; }

/* Punto de estado: verde (activo), gris (en pausa), rojo (un atajo no se pudo registrar), ambar
   (falta un permiso). */
QLabel#statusDot { border-radius: 5px; min-width: 10px; max-width: 10px; min-height: 10px; max-height: 10px; background-color: @textFaint; }
QLabel#statusDot[state="on"] { background-color: @ok; }
QLabel#statusDot[state="paused"] { background-color: #555555; }
QLabel#statusDot[state="error"] { background-color: @error; }
QLabel#statusDot[state="warn"] { background-color: @warn; }
QFrame#card[tone="warn"] { border: 1px solid #4d4020; }
QFrame#card[tone="err"] { border: 1px solid #5a2e26; }

/* Fila de un atajo: nombre, teclas y el lapiz. Grabando, las teclas se reemplazan por el campo
   con borde violeta. */
QLabel#shortcutName { color: @text; font-size: @fs13_5; }
QFrame#recorder { background-color: @field; border: 1px solid @accent; border-radius: 4px; min-height: 24px; max-height: 24px; }
QLabel#recorderText { color: @textStrong; font-size: @fs12_5; }
QLabel#recorderDot { background-color: @accent; border-radius: 3px; min-width: 6px; max-width: 6px; min-height: 6px; max-height: 6px; }

/* Tarjeta del Dope Sheet */
QLabel#spotValue { color: @text; font-size: @fs13_5; }

/* Tarjeta de espacio en disco. Los controles con borde miden 24 de alto: 22 de contenido + borde. */
QLabel#caption[tone="warn"] { color: @warn; }
ElidedLabel#driveName { color: @text; font-size: @fs13_5; }
ElidedLabel#driveName[dim="true"] { color: @icon; }
QSpinBox#threshold {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; color: @textStrong;
    font-size: @fs12_5; padding: 0px 7px 0px 4px; selection-background-color: #393455; selection-color: @textBright;
}
QSpinBox#threshold:focus { border-color: @accent; }
QPushButton#segButton {
    background-color: #2b2b2b; border: 1px solid #383838; border-radius: 0px; color: @textMuted;
    padding: 0px 7px; min-height: 22px; max-height: 22px; font-size: @fs11_5; font-weight: 600;
}
QPushButton#segButton[pos="left"] { border-top-left-radius: 4px; border-bottom-left-radius: 4px; }
QPushButton#segButton[pos="right"] { border-top-right-radius: 4px; border-bottom-right-radius: 4px; border-left: none; }
QPushButton#segButton:hover { background-color: #333333; }
QPushButton#segButton:checked { background-color: #393455; border: 1px solid #4c4770; color: #DDDBEE; }
QPushButton#segButton[pos="right"]:checked { border-left: 1px solid #4c4770; }
/* Switch segmentado (SegmentedSwitch): el mismo campo que #fieldButton (fondo, borde, radio, 24 de alto,
   letra). Adentro, segmentos de 20 con radio 2; el elegido con los tokens de "Elegido". */
QWidget#segmentSwitch { background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; }
QPushButton#segment {
    background-color: transparent; color: @textMuted; border: 1px solid transparent; border-radius: 2px;
    padding: 0px 9px; min-height: 18px; max-height: 18px; font-size: @fs12_5; font-weight: 400;
}
QPushButton#segment:hover { color: @textStrong; }
QPushButton#segment:checked { background-color: @chosenBg; border: 1px solid @chosenBorder; color: @chosenText; }
QPushButton#fieldButton {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; color: @textStrong;
    padding: 0px 8px 0px 8px; min-height: 22px; max-height: 22px; font-size: @fs12_5; font-weight: 400;
}
QPushButton#fieldButton:hover { border-color: #3d3d3d; background-color: #1f1f1f; }
QPushButton#linkButton {
    background-color: transparent; border: none; color: @link; padding: 0px; min-height: 16px; max-height: 16px;
    font-size: @fs13; font-weight: 500; text-align: left;
}
QPushButton#linkButton:hover { color: #b9aef0; }

/* Calibrador: pasos numerados con la caja violeta (badges del Shot Player) y la burbuja que sigue
   al puntero. */
QLabel#stepBadge { background-color: #443a91; color: #ffffff; border-radius: 4px; min-width: 22px; max-width: 22px; min-height: 22px; max-height: 22px; font-size: @fs11; font-weight: 600; }
QLabel#stepText { color: #a9a9ae; font-size: @fs13; }
QLabel#bubbleTitle { color: @textStrong; font-size: @fs13; font-weight: 600; }
QLabel#bubbleTitle[tone="err"] { color: @error; }
QLabel#bubbleMeta { color: @textFaint; font-size: @fs12; }

/* Botones (misma caja en todos los estados: si solo uno define borde, cambia el fondo) */
QPushButton {
    background-color: #2a2a2a; color: @text; border: none; border-radius: 5px;
    padding: 0px 12px; min-height: 30px; max-height: 30px; font-size: @fs13; font-weight: 500;
}
QPushButton:hover { background-color: #383838; }
QPushButton:pressed { background-color: #242424; }
QPushButton:disabled { background-color: #232323; color: #5a5a5a; }
QPushButton[variant="primary"] { background-color: #443a91; color: #DDDBEE; font-weight: 600; }
QPushButton[variant="primary"]:hover { background-color: #5243a8; }
QPushButton[variant="primary"]:pressed { background-color: #3b3280; }
QPushButton[variant="primary"]:disabled { background-color: #262245; color: #74728a; }
QPushButton[variant="ghost"] { background-color: transparent; color: @textMuted; padding: 0px 8px; }
QPushButton[variant="ghost"]:hover { background-color: #2a2a2a; color: @textStrong; }
QPushButton[btnSize="sm"] { min-height: 26px; max-height: 26px; font-size: @fs12_5; padding: 0px 10px; }
QPushButton[btnSize="icon"] { min-height: 26px; max-height: 26px; min-width: 26px; max-width: 26px; padding: 0px; }
QPushButton#closeButton { border: 1px solid #3B316A; }
/* "Elegido" entre varios botones del mismo tipo (canvas: --chk-bg/--chk-border), sin ser la accion
   principal: el keycap de version de Nuke elegida en Open in NukeX. */
QPushButton[chosen="true"] { background-color: @chosenBg; border: 1px solid @chosenBorder; color: @chosenText; }
QPushButton[chosen="true"]:hover { background-color: @chosenBorder; }

/* Campo de solo lectura */
QFrame#field { background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; min-height: 28px; max-height: 28px; }
ElidedLabel#fieldValue { color: @textCaption; font-size: @fs13_5; }
ElidedLabel#fieldValue[empty="true"] { color: @textPlaceholder; }

/* Chips */
/* En Qt min/max-height no cuentan el borde: 16 + 2 = los 18 de `.chip` y 18 + 2 = los 20 de `.kc`. */
QFrame#chip { background-color: #2b2b2b; border: 1px solid #383838; border-radius: 4px; min-height: 16px; max-height: 16px; }
QFrame#chip QLabel { color: #c5c8c7; font-size: @fs11_5; font-weight: 600; }
QFrame#chip[tone="ok"] { background-color: #1f2a17; border-color: #3a4d27; }
QFrame#chip[tone="ok"] QLabel { color: @ok; }
QFrame#chip[tone="err"] { background-color: #35211f; border-color: #5c3330; }
QFrame#chip[tone="err"] QLabel { color: @error; }
QFrame#chip[tone="src"] { background-color: transparent; border-color: #333333; }
QFrame#chip[tone="src"] QLabel { color: @textMuted; font-weight: 500; }
QFrame#chip[tone="key"] { min-height: 18px; max-height: 18px; }
QFrame#chip[tone="keyDim"] { min-height: 18px; max-height: 18px; border: 1px dashed #383838; }
QFrame#chip[tone="keyDim"] QLabel { color: @icon; }
QFrame#chip[tone="warn"] { background-color: #2d2614; border-color: #4d4020; }
QFrame#chip[tone="warn"] QLabel { color: @warn; }

/* Checkbox: tokens LGA (LGA_LinkRedirector/docs/UI_STYLE_LGA_APPS.md). El fondo del widget se
   declara transparente en todos los estados porque el hover del indicador se filtra al fondo del
   QCheckBox. La tilde es PNG y no SVG a proposito: el exe no carga Qt6Svg y el deploy no lleva el
   plugin qsvg, asi que un SVG desapareceria en la copia instalada sin ningun error. */
QCheckBox, QCheckBox:hover, QCheckBox:checked, QCheckBox:unchecked { background: transparent; color: @text; spacing: 10px; font-size: @fs13_5; min-height: 20px; }
QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; border: 1px solid #3a3744; background-color: #2a2832; }
QCheckBox::indicator:unchecked:hover { background-color: #3a3744; }
QCheckBox::indicator:checked { border: 1px solid #4c4770; background-color: #393455; image: url(:/icons/check.png); }
QCheckBox::indicator:checked:hover { background-color: #4c4770; }

/* Menu del tray: paleta y medidas de LGA_Base_QT_C_Py/docs/Doc_MenuContextual.md. Sin radio: un
   QMenu de nivel superior sin translucidez pinta las esquinas de negro. */
QMenu { background-color: #262626; border: 1px solid #3a3a3a; padding: 5px 0px; }
QMenu::item { color: #cccccc; padding: 5px 16px 5px 14px; background: transparent; font-size: @fs14; }
QMenu::item:selected { background-color: #443a91; color: #ffffff; }
QMenu::item:disabled { color: #6a6a6a; }
QMenu::separator { height: 1px; background: #3a3a3a; margin: 5px 8px; }
/* Item marcable (el combo de Link Redirector, el intervalo de Disk Space): la marca es un tilde
   violeta (canvas ".dd div.cur::before", color @link), no el cuadrado/tilde nativo de Fusion. Sin
   marca en el desmarcado: ni caja ni circulo, como el diseno. */
QMenu::indicator { width: 14px; height: 14px; }
QMenu::indicator:unchecked { image: none; }
QMenu::indicator:checked { image: url(:/icons/check_link.png); }

/* Dialogos: el de update, los QMessageBox y el progreso de descarga */
QDialog#updateDialog, QMessageBox, QProgressDialog { background-color: @dialog; }
QDialog#helpDialog { background: transparent; }
QLabel#dialogTitle { color: @textBright; font-size: @fs14; font-weight: 600; }
QProgressBar { background-color: #393959; border: 1px solid #444444; border-radius: 4px; min-height: 8px; max-height: 8px; color: transparent; }
QProgressBar::chunk { background-color: #6a55c9; border-radius: 3px; }

/* Ayuda */
QLabel#helpTitle { color: rgb(127, 98, 170); font-size: @fs20; font-weight: 600; }
QLabel#helpVersion { color: @textStrong; font-size: @fs16; font-weight: 600; }
QLabel#helpDeveloped { color: #9D9D9D; font-size: @fs14; }
QLabel#helpLink { color: @link; font-size: @fs14; text-decoration: underline; }
QLabel#helpLink[hover="true"] { color: #C9C0F5; }
QLabel#helpSection { color: @textStrong; font-size: @fs13_5; font-weight: 600; }
QLabel#helpBody { color: #a9a9ae; font-size: @fs13; }
QLabel#helpNote { color: @textCaption; font-size: @fs12; }
QFrame#helpRule { background-color: @divider; border: none; min-height: 1px; max-height: 1px; }
QScrollArea#helpScroll, QWidget#helpViewport, QWidget#helpContent { background: transparent; border: none; }
QLabel#helpToolTitle { color: @textStrong; font-size: @fs13_5; font-weight: 600; }

/* Ventana de herramientas (forma A): barra lateral, encabezado del panel y paginas del host. */
QFrame#sidebar { background-color: @side; border: none; border-right: 1px solid @divider; }
QLabel#sideLabel { color: @textFaint; font-size: @fs11; }
QLabel#sideName { color: @text; font-size: @fs13_5; }
QLabel#sideName[off="true"] { color: @textCaption; }
QLabel#sideName[sel="true"] { color: @textBright; }
QLabel#sideStatus { color: @textFaint; font-size: @fs11_5; }
QLabel#sideStatus[tone="ok"] { color: @statusOk; }
QLabel#sideStatus[tone="warn"] { color: @warn; }
QLabel#sideStatus[tone="err"] { color: @error; }
QScrollArea#pane, QWidget#paneContent { background-color: @window; border: none; }
/* Scroll vertical fino y oscuro (el canvas no lo dibuja; el de Fusion es claro y con flechas). */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 2px 2px 0px; }
QScrollBar::handle:vertical { background: #333333; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #444444; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; border: none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QFrame#modIcon { background-color: @modIconBg; border: 1px solid @modIconBorder; border-radius: 8px; }
QLabel#modTitle { color: @textBright; font-size: @fs16; font-weight: 600; }
QLabel#platTag { color: @textFaint; font-size: @fs11; font-weight: 500; border: 1px solid @platBorder; border-radius: 4px; padding: 1px 5px; }
QLabel#offBullet { color: @text; font-size: @fs13; }
QLabel#offBulletMark { color: @dotPaused; font-size: @fs13; }
QLabel#welcomeTitle { color: @textStrong; font-size: @fs16; font-weight: 600; }
QFrame#miniCard { background-color: @miniCard; border: 1px solid transparent; border-radius: 8px; }
QLabel#miniTitle { color: @textStrong; font-size: @fs13; font-weight: 600; }
QLabel#runningText { color: @text; font-size: @fs13; }
QLabel#aboutName { color: @aboutName; font-size: @fs13_5; font-weight: 600; }
QLabel#meta[tone="ok"] { color: @ok; }
QLabel#meta[tone="warn"] { color: @warn; }
QLabel#linkLabel { color: @link; font-size: @fs13; font-weight: 500; text-decoration: underline; }
QLabel#linkLabel[hover="true"] { color: #C9C0F5; }
QLabel#traySection { color: @textFaint; font-size: @fs11; padding: 0px 16px 0px 14px; min-height: 22px; max-height: 22px; background: transparent; }

/* Ventana de limpieza de Disk Space (canvas "Mighty Tools Disk Cleanup"). */
QWidget#cleanupWindow { background-color: @window; }
QScrollArea#cleanScroll, QWidget#cleanContent { background-color: @window; border: none; }
QLabel#meterText { color: @textFaint; font-size: @fs12_5; }
QFrame#actionBar { background-color: @side; border: none; border-top: 1px solid @divider; }
QLabel#actionText { color: @text; font-size: @fs13; }
QPushButton[variant="danger"] {
    background-color: @errBg; color: @error; border: 1px solid @errBorder;
    min-height: 28px; max-height: 28px; padding: 0px 11px;
}
QPushButton[variant="danger"][btnSize="sm"] { min-height: 24px; max-height: 24px; padding: 0px 9px; }
QPushButton[variant="danger"]:hover { background-color: #45292a; }
QPushButton[variant="danger"]:pressed { background-color: #2e1c1b; }
QPushButton[variant="danger"]:disabled { background-color: #232323; color: #5a5a5a; border: 1px solid #232323; }
QPushButton#filterChip {
    background-color: transparent; border: 1px solid #383838; border-radius: 12px; color: @textMuted;
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
QLabel#itemName { color: @text; font-size: @fs13; }
QLabel#itemName[off="true"] { color: @textFaint; }
ElidedLabel#itemPath { color: @textFaint; font-size: @fs12; }
QWidget#cleanItem { border-radius: 4px; background-color: transparent; }
QWidget#cleanItem:hover { background-color: #1b1b1b; }
QFrame#okBanner { background-color: @okBg; border: 1px solid @okBorder; border-radius: 6px; }
QLabel#bannerText { color: @ok; font-size: @fs13; font-weight: 500; }
QLabel#dialogLine { color: @text; font-size: @fs13; }
QLabel#dialogValue { color: @text; font-size: @fs13; }
ElidedLabel#dialogPath { color: @textStrong; font-family: "JetBrains Mono", Consolas, monospace; font-size: @fs12; }
QScrollArea#dialogScroll, QScrollArea#dialogScroll > QWidget > QWidget { background: transparent; border: none; }
QLineEdit#ruleField {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; color: @textStrong;
    padding: 0px 8px; min-height: 26px; max-height: 26px; font-size: @fs13;
    selection-background-color: #393455; selection-color: @textBright;
}
QLineEdit#ruleField:focus { border-color: @accent; }
QLineEdit#ruleField:disabled { color: @textPlaceholder; }
)QSS");

    const QList<QPair<const char *, QString>> tokens = {
        {"@statusOk", kStatusOk}, {"@sideSelected", kSideSelected}, {"@side", kSide},
        {"@modIconBg", kModIconBg}, {"@modIconBorder", kModIconBorder}, {"@platBorder", kPlatBorder},
        {"@miniCard", kMiniCard}, {"@aboutName", kAboutName}, {"@dotPaused", kDotPaused},
        {"@chosenBg", kChosenBg}, {"@chosenBorder", kChosenBorder}, {"@chosenText", kChosenText},
        {"@okBg", kOkBg}, {"@okBorder", kOkBorder}, {"@errBg", kErrBg}, {"@errBorder", kErrBorder},
        {"@window", kWindow}, {"@card", kCard}, {"@tile", kTile}, {"@fieldBorder", kFieldBorder},
        {"@field", kField}, {"@border", kBorder}, {"@divider", kDivider}, {"@dialog", kDialog},
        {"@textStrong", kTextStrong}, {"@textBright", kTextBright}, {"@textMuted", kTextMuted},
        {"@textCaption", kTextCaption}, {"@textFaint", kTextFaint}, {"@textPlaceholder", kTextPlaceholder},
        {"@link", kLink}, {"@text", kText}, {"@ok", kOk}, {"@error", kError}, {"@warn", kWarn},
        {"@accent", kAccent}, {"@icon", kIcon},
        {"@fs13_5", fs(13.5)}, {"@fs12_5", fs(12.5)}, {"@fs11_5", fs(11.5)}, {"@fs11", fs(11)},
        {"@fs20", fs(20)}, {"@fs16", fs(16)}, {"@fs14", fs(14)}, {"@fs13", fs(13)}, {"@fs12", fs(12)},
    };
    // Orden: los nombres largos primero ("@textStrong" antes que "@text", "@fs13_5" antes que "@fs13").
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
