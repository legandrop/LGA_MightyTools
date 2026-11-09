#ifndef MIGHTYTOOLS_UIWIDGETS_H
#define MIGHTYTOOLS_UIWIDGETS_H

#include <QAbstractButton>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QStringList>
#include <QWidget>

#include <memory>

class QBoxLayout;
class QDateTime;
class QHBoxLayout;
class QMouseEvent;
class QTextDocument;
class QPainter;
class QPushButton;
class QVariantAnimation;

// Piezas chicas compartidas: iconos vectoriales, chips y label con elipsis. Copia recortada de
// uiwidgets.h de LGA_VideoDownloader (mismos trazos y medidas). Todo se pinta con QPainter a la
// escala real del dispositivo, sin mapas de bits reescalados ni SVG (el deploy no lleva qsvg).

enum class Icon {
    Help, Folder, X, Minimize, Close, Pencil, Plus, ChevronDown, General,
    // Franja «How it works» de cada pagina (D-73).
    HelpCircle, ChevronUp, ChevronDownRound,
    // Ventana de limpieza de Disk Space (trazos del canvas, caja de 16).
    TreeClosed, TreeOpen, File, Trash, Reveal, Refresh, Shield, List, Check, Dash,
};

namespace Icons {
// Pinta el icono dentro de rect (se escala desde su viewBox original).
void paint(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color);
// QIcon vectorial para botones; el color disabled se atenua solo.
QIcon icon(Icon icon, const QColor &color);
} // namespace Icons

// Icono suelto como widget (para poner al lado de un texto).
class IconWidget : public QWidget
{
    Q_OBJECT
public:
    IconWidget(Icon icon, const QColor &color, int size, QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    Icon m_icon;
    QColor m_color;
};

// Label de una linea que recorta con "..." en su propio paintEvent, con SU fuente y SU ancho.
// Reemplaza el elidedText calculado afuera, que media con una fuente, pintaba con otra y cortaba
// el path contra el borde de la ventana.
// El logo de la app (sus planchas C, M y Y) recortado al dibujo: `width` de ancho y el alto en proporcion
// (892 x 716 del PNG de 1024). Lo usan la barra de titulo y el colofon de General.
class AppLogo : public QWidget
{
public:
    explicit AppLogo(int width, QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QIcon m_icon;
};

class ElidedLabel : public QWidget
{
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    void setText(const QString &text);
    QString text() const { return m_text; }
    void setElideMode(Qt::TextElideMode mode);
    // Horizontal: a la izquierda (de fabrica) o a la derecha. Vertical, siempre centrado.
    void setAlignment(Qt::Alignment alignment);
    // Texto que se ve con el ancho actual (para la evidencia de las capturas).
    QString shownText() const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    // El tooltip es el texto entero, y solo cuando no entra en el ancho que tiene.
    void updateTip();
    QString m_text;
    Qt::TextElideMode m_mode = Qt::ElideRight;
    Qt::Alignment m_alignment = Qt::AlignLeft;
};

// Texto gris de una o mas lineas (`.cap` del canvas) con el interlineado del diseno: 17 px por
// linea, con la media diferencia arriba como CSS. QLabel usa el de la fuente (16 px a 13 px) y en
// un panel con varias descripciones la diferencia se acumula. Texto plano; el tono (warn, err) y el
// color siguen saliendo de la hoja de estilo (QLabel#caption).
class CaptionLabel : public QLabel
{
    Q_OBJECT
public:
    explicit CaptionLabel(const QString &text, QWidget *parent = nullptr, int lineHeight = 17);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    int heightForWidth(int width) const override;
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    int lineCount(int width) const;
    QString laidOutText() const;
    int m_lineHeight = 17;
};

// Texto enriquecido (spans de color) con interlineado fijo, como `line-height` de CSS: cada linea
// mide `lineHeight` y el texto queda centrado en ella. Lo usa la ayuda (pasos a 22 px por linea).
// Fuente y color salen de la hoja de estilo (el objectName que se le ponga). Las teclas que arma
// Ui::keycapsHtml() se dibujan como teclas (`.k` del diseno) adentro del texto.
class RichLineLabel : public QLabel
{
    Q_OBJECT
public:
    RichLineLabel(const QString &html, int lineHeight, QWidget *parent = nullptr);
    ~RichLineLabel() override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QTextDocument &document(int width) const;
    int m_lineHeight = 18;
    mutable std::unique_ptr<QTextDocument> m_document;
    mutable QString m_documentKey;
    mutable int m_documentWidth = -1;
    mutable QString m_idealKey;
    mutable int m_idealWidth = -1;
};

// Chip de estado: tono (ok, err, src, key o neutro) + texto.
class Chip : public QFrame
{
    Q_OBJECT
public:
    explicit Chip(QWidget *parent = nullptr);
    void set(const QString &tone, const QString &text);
    QString text() const;
private:
    QLabel *m_label;
    QString m_tone;
};

// Link de texto subrayado que cambia de color con el mouse encima y abre `url` con un click (el
// GitHubLinkLabel del Help de FileManager S3). Un <a> dentro de un QLabel no tiene hover.
class LinkLabel : public QLabel
{
    Q_OBJECT
public:
    LinkLabel(const QString &text, const QString &url, QWidget *parent = nullptr);
protected:
    bool event(QEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    QString m_url;
};

// Interruptor de encendido de una herramienta (`.sw` del Pliego, D-73): 30 x 16 con la perilla circular;
// apagado gris, prendido cian. setLarge() da el del encabezado de la hoja (42 x 22).
// Checkable: toggled() avisa el cambio. No toma foco de teclado (regla de la app).
// Animacion «Suave» (D-64): con un click del usuario la perilla desliza 160 ms con frenado al final y
// el color se funde parejo en el mismo tiempo. Un setChecked() del codigo (reflejar un estado) salta
// al final sin animar.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    void setLarge(bool large);
    QSize sizeHint() const override;
    // Posicion de la perilla que se esta dibujando (0 apagado, 1 prendido). Para QA.
    qreal knobPosition() const { return m_knob; }
protected:
    void paintEvent(QPaintEvent *event) override;
    void nextCheckState() override;
    void checkStateSet() override;
private:
    void animateTo(qreal target);

    QVariantAnimation *m_anim = nullptr; // se crea con el primer click
    bool m_large = false;
    bool m_byUser = false;
    qreal m_target = 0; // hacia donde va la animacion en marcha
    qreal m_knob = 0;  // posicion de la perilla: 0 apagado, 1 prendido (con la curva)
    qreal m_tint = 0;  // mezcla de colores: 0 apagado, 1 prendido (lineal)
    qreal m_knobFrom = 0;
    qreal m_tintFrom = 0;
};

// Una opcion de encendido y apagado: texto (y descripcion opcional debajo) a la izquierda y el switch a
// la derecha. Reemplaza a las casillas de verificacion (Lega, 2026-10-31: todo con switch). Un click en
// el texto tambien cambia el switch. clicked() sale solo por accion del usuario; setChecked() no avisa.
class SwitchRow : public QWidget
{
    Q_OBJECT
public:
    SwitchRow(const QString &text, const QString &caption, QWidget *parent = nullptr);
    bool isChecked() const;
    void setChecked(bool checked);
    ToggleSwitch *toggle() const { return m_switch; }
    // nullptr si se creo sin descripcion.
    QLabel *captionLabel() const { return m_caption; }
    // Controles que van entre el texto y el switch (un resultado, un boton).
    void addTrailingWidget(QWidget *widget);
signals:
    void clicked(bool checked);
protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    QHBoxLayout *m_row = nullptr;
    QLabel *m_caption = nullptr;
    ToggleSwitch *m_switch = nullptr;
    QList<QWidget *> m_trailing;
};

// Switch segmentado: varias opciones, una sola elegida. Es un campo como el desplegable (#fieldButton):
// mismo fondo, mismo borde de 1 px, radio 3 y 24 de alto, para que conviva con el resto de la ventana. El
// segmento elegido usa los tokens de "Elegido" (kChosen*), no el violeta de la accion principal. La idea
// viene del switch Studio/Client de HieroTools. QSS #segmentSwitch / #segment en Theme.cpp. No toma foco
// de teclado.
class SegmentedSwitch : public QWidget
{
    Q_OBJECT
public:
    SegmentedSwitch(const QStringList &labels, int current, QWidget *parent = nullptr);
    int current() const { return m_current; }
    void setCurrent(int index);
    // Una opcion que no se puede elegir queda apagada (sin hover ni mano).
    void setSegmentEnabled(int index, bool enabled);
    QPushButton *segment(int index) const { return m_segments.value(index); }

signals:
    // El usuario eligio otra opcion (un click en la elegida no avisa nada).
    void currentChanged(int index);

private:
    QList<QPushButton *> m_segments;
    int m_current = -1;
};

// Marca de estado dibujada: un circulo lleno con el tono (ok, paused, warn, err, accent) o hueco
// con borde (off). `diameter` es el de la caja.
class StatusDot : public QWidget
{
    Q_OBJECT
public:
    explicit StatusDot(int diameter, QWidget *parent = nullptr);
    void setTone(const QString &tone);
    QString tone() const { return m_tone; }
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QString m_tone = QStringLiteral("ok");
};

// La miniatura de una app en las filas de App Toggle y Mouse Buttons: su icono real en una baldosa con los
// materiales de la de Window Layouts. Sin icono (la app no esta en el disco), una ventana dibujada.
class AppIconTile : public QWidget
{
    Q_OBJECT
public:
    explicit AppIconTile(QWidget *parent = nullptr);
    void setIcon(const QIcon &icon);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    void paintIcon(QPainter &painter, const QRect &target) const;
    QIcon m_icon;
    // El cuadro ya achicado a los pixeles de la pantalla: se arma una vez, no en cada pintada.
    mutable QPixmap m_scaled;
    mutable qreal m_scaledDpr = 0;
    mutable int m_scaledDevice = 0;
};

// Tarjeta de estado con punto, titulo, texto y boton (`.card.status` del canvas): la de Nuke
// Shortcuts y el aviso del panel de apagado.
class StatusCard : public QFrame
{
    Q_OBJECT
public:
    explicit StatusCard(QWidget *parent = nullptr);
    // dot: "on", "paused", "warn", "error"; tone de la tarjeta: "", "warn", "err"; variant del boton:
    // "", "primary"; size: "" (30 px) o "sm". Boton vacio = sin boton.
    void set(const QString &dot, const QString &title, const QString &text, const QString &button,
             const QString &buttonVariant, const QString &cardTone, const QString &buttonSize = QString());
    QPushButton *button() const { return m_button; }
    QString title() const;
    // El estado vivo de la herramienta (`.live` del Pliego): sin caja, y la ventana lo sube al encabezado
    // de la hoja, arriba de «How it works». Lo marca el panel al crearla.
    void setLive(bool live);
    bool isLive() const { return m_live; }

private:
    void applyLiveMargins();

    StatusDot *m_dot = nullptr;
    bool m_live = false;
    QString m_alert; ///< "warn", "err" o vacio: el marco de color del aviso, tambien en el encabezado
    QLabel *m_title = nullptr;
    QLabel *m_text = nullptr;
    QPushButton *m_button = nullptr;
};

// Paginas apiladas que miden lo que mide la VISIBLE (QStackedWidget toma el alto de la mas alta y deja
// el editor con el scroll de la lista): la lista y el editor de Window Layouts y App Toggle.
class CurrentPageStack : public QWidget
{
public:
    explicit CurrentPageStack(QWidget *parent = nullptr);
    void addWidget(QWidget *page);
    QWidget *currentWidget() const { return m_current; }
    void setCurrentWidget(QWidget *page);

private:
    QWidget *m_current = nullptr;
};

// Ayudas para construir botones con las variantes de la hoja de estilo.
namespace Ui {
QPushButton *button(const QString &text, const QString &variant = QString(), const QString &size = QString(),
                    QWidget *parent = nullptr);
void setIcon(QPushButton *button, Icon icon, const QColor &color, int size = 14);
// La flecha de un desplegable (`fieldButton`): a la derecha del texto y con aire entre los dos. El espacio
// que QPushButton deja entre icono y texto es fijo y la dejaba pegada.
void setDropdownArrow(QPushButton *button);
void repolish(QWidget *widget);
// Cambia una propiedad de estilo y vuelve a pulir solo si cambio.
void setStyleProperty(QWidget *widget, const char *name, const QVariant &value);

// Piezas de las tarjetas del diseno (las usan el host y los paneles de las herramientas).
QLabel *label(const QString &text, const char *objectName, QWidget *parent);
// Texto gris de dos lineas o mas (`.cap` del canvas).
QLabel *caption(const QString &text, QWidget *parent);
// La nota gris de un atajo que se apreto y no pudo hacer nada: «Ultimo atajo, 14:32: motivo» (hoy solo la
// hora; otro dia, tambien la fecha). Texto vacio si no hay nota.
QString attemptNoteText(const QString &note, const QDateTime &when);
// Tarjeta vacia (`.card`): fondo, radio 10, sin layout.
QFrame *card(QWidget *parent);
// Un atajo ("Ctrl+Shift+D", o "⌃⌥⇧⌘D" en mac) como teclas para un RichLineLabel; texto vacio si `text` no
// es un atajo.
QString keycapsHtml(const QString &text);
// Linea divisoria con 8 px de aire arriba y abajo (`.divider`).
void addDivider(QBoxLayout *layout, QWidget *parent);
} // namespace Ui

#endif // MIGHTYTOOLS_UIWIDGETS_H
