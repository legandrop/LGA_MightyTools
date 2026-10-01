#ifndef MIGHTYTOOLS_UIWIDGETS_H
#define MIGHTYTOOLS_UIWIDGETS_H

#include <QAbstractButton>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QStringList>
#include <QWidget>

class QBoxLayout;
class QTextDocument;
class QPainter;
class QPushButton;

// Piezas chicas compartidas: iconos vectoriales, chips y label con elipsis. Copia recortada de
// uiwidgets.h de LGA_VideoDownloader (mismos trazos y medidas). Todo se pinta con QPainter a la
// escala real del dispositivo, sin mapas de bits reescalados ni SVG (el deploy no lleva qsvg).

enum class Icon {
    Help, Folder, X, Minimize, Close, Pencil, Plus, ChevronDown, General,
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
class ElidedLabel : public QWidget
{
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    void setText(const QString &text);
    QString text() const { return m_text; }
    void setElideMode(Qt::TextElideMode mode);
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
    int m_lineHeight = 17;
};

// Texto enriquecido (spans de color) con interlineado fijo, como `line-height` de CSS: cada linea
// mide `lineHeight` y el texto queda centrado en ella. Lo usa la ayuda (pasos a 18 px por linea).
// Fuente y color salen de la hoja de estilo (el objectName que se le ponga).
class RichLineLabel : public QLabel
{
    Q_OBJECT
public:
    RichLineLabel(const QString &html, int lineHeight, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    void layoutDocument(QTextDocument &document, int width) const;
    int m_lineHeight = 18;
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

// Interruptor de encendido de una herramienta (30 x 17, el `.sw` del canvas). Checkable: toggled()
// avisa el cambio. No toma foco de teclado (regla de la app).
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
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

// Punto de estado dibujado: lleno con el tono (ok, paused, warn, err) o hueco con borde (off).
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

private:
    QLabel *m_dot = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_text = nullptr;
    QPushButton *m_button = nullptr;
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
// Tarjeta vacia (`.card`): fondo, radio 8, sin layout.
QFrame *card(QWidget *parent);
// Linea divisoria con 8 px de aire arriba y abajo (`.divider`).
void addDivider(QBoxLayout *layout, QWidget *parent);
} // namespace Ui

#endif // MIGHTYTOOLS_UIWIDGETS_H
