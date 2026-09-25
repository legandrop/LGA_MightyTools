#ifndef MIGHTYTOOLS_UIWIDGETS_H
#define MIGHTYTOOLS_UIWIDGETS_H

#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QWidget>

class QPainter;
class QPushButton;

// Piezas chicas compartidas: iconos vectoriales, chips y label con elipsis. Copia recortada de
// uiwidgets.h de LGA_VideoDownloader (mismos trazos y medidas). Todo se pinta con QPainter a la
// escala real del dispositivo, sin mapas de bits reescalados ni SVG (el deploy no lleva qsvg).

enum class Icon { Help, Folder, X, Minimize, Close, Pencil, Plus, ChevronDown };

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
private:
    QString m_text;
    Qt::TextElideMode m_mode = Qt::ElideRight;
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

// Ayudas para construir botones con las variantes de la hoja de estilo.
namespace Ui {
QPushButton *button(const QString &text, const QString &variant = QString(), const QString &size = QString(),
                    QWidget *parent = nullptr);
void setIcon(QPushButton *button, Icon icon, const QColor &color, int size = 14);
void repolish(QWidget *widget);
// Cambia una propiedad de estilo y vuelve a pulir solo si cambio.
void setStyleProperty(QWidget *widget, const char *name, const QVariant &value);
} // namespace Ui

#endif // MIGHTYTOOLS_UIWIDGETS_H
