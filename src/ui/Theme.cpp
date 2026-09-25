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
// escribe texto (WA_InputMethodEnabled): hoy, el umbral de cada disco en la tarjeta "Disk space".
class NoKeyboardFocus : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Polish && watched->isWidgetType()) {
            auto *widget = static_cast<QWidget *>(watched);
            if (!widget->testAttribute(Qt::WA_InputMethodEnabled)) {
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
    font.setWeight(static_cast<QFont::Weight>(weight));
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
QToolTip { background-color: @tile; color: @text; border: 1px solid #333333; padding: 4px 6px; }

/* Barra de titulo propia */
QLabel#titleBarTitle { color: @textMuted; font-size: @fs13; font-weight: 500; }
QFrame#titleBarSeparator { background-color: @border; border: none; }

QFrame#card { background-color: @card; border: none; border-radius: 8px; }
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
QPushButton#fieldButton {
    background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; color: @textStrong;
    padding: 0px 6px 0px 8px; min-height: 22px; max-height: 22px; font-size: @fs12_5; font-weight: 400;
}
QPushButton#fieldButton:hover { border-color: #3d3d3d; background-color: #1f1f1f; }
QPushButton#linkButton {
    background-color: transparent; border: none; color: @link; padding: 0px; min-height: 24px; max-height: 24px;
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
QPushButton[variant="ghost"] { background-color: transparent; color: @textMuted; padding: 0px 8px; }
QPushButton[variant="ghost"]:hover { background-color: #2a2a2a; color: @textStrong; }
QPushButton[btnSize="sm"] { min-height: 26px; max-height: 26px; font-size: @fs12_5; padding: 0px 10px; }
QPushButton[btnSize="icon"] { min-height: 26px; max-height: 26px; min-width: 28px; max-width: 28px; padding: 0px; }
QPushButton#closeButton { border: 1px solid #3B316A; }

/* Campo de solo lectura */
QFrame#field { background-color: @field; border: 1px solid @fieldBorder; border-radius: 3px; min-height: 28px; max-height: 28px; }
ElidedLabel#fieldValue { color: @textCaption; font-size: @fs13_5; }
ElidedLabel#fieldValue[empty="true"] { color: @textPlaceholder; }

/* Chips */
QFrame#chip { background-color: #2b2b2b; border: 1px solid #383838; border-radius: 4px; min-height: 18px; max-height: 18px; }
QFrame#chip QLabel { color: #c5c8c7; font-size: @fs11_5; font-weight: 600; }
QFrame#chip[tone="ok"] { background-color: #1f2a17; border-color: #3a4d27; }
QFrame#chip[tone="ok"] QLabel { color: @ok; }
QFrame#chip[tone="err"] { background-color: #35211f; border-color: #5c3330; }
QFrame#chip[tone="err"] QLabel { color: @error; }
QFrame#chip[tone="src"] { background-color: transparent; border-color: #333333; }
QFrame#chip[tone="src"] QLabel { color: @textMuted; font-weight: 500; }
QFrame#chip[tone="key"] { min-height: 20px; max-height: 20px; }
QFrame#chip[tone="warn"] { background-color: #2d2614; border-color: #4d4020; }
QFrame#chip[tone="warn"] QLabel { color: @warn; }

/* Checkbox: tokens LGA (LGA_LinkRedirector/docs/UI_STYLE_LGA_APPS.md). El fondo del widget se
   declara transparente en todos los estados porque el hover del indicador se filtra al fondo del
   QCheckBox. La tilde es PNG y no SVG a proposito: el exe no carga Qt6Svg y el deploy no lleva el
   plugin qsvg, asi que un SVG desapareceria en la copia instalada sin ningun error. */
QCheckBox, QCheckBox:hover, QCheckBox:checked, QCheckBox:unchecked { background: transparent; color: @text; spacing: 10px; font-size: @fs13_5; }
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
QFrame#helpRule { background-color: @border; border: none; min-height: 1px; max-height: 1px; }
)QSS");

    const QList<QPair<const char *, QString>> tokens = {
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

} // namespace Theme
