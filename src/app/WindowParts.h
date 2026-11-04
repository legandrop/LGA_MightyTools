#ifndef MIGHTYTOOLS_WINDOWPARTS_H
#define MIGHTYTOOLS_WINDOWPARTS_H

#include "app/Module.h"
#include "ui/HelpSection.h"

#include <QAbstractButton>
#include <QFrame>
#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

class Chip;
class QLabel;
class QPushButton;
class QVBoxLayout;
class StatusDot;
class StripGlyph;
class ToggleSwitch;

// Piezas de la ventana que dibuja el host, en el lenguaje «Pliego» (D-73). Ninguna conoce a un modulo en
// particular: todo sale del descriptor, de ModuleStatus y de la ayuda de cada herramienta.

// Pinta el icono de una herramienta (del descriptor) o el de General.
using IconPainter = std::function<void(QPainter &painter, const QRectF &rect, const QColor &color)>;

class ToolIcon : public QWidget
{
    Q_OBJECT
public:
    ToolIcon(IconPainter painter, int size, QWidget *parent = nullptr);
    void setColor(const QColor &color);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    IconPainter m_painter;
    QColor m_color;
    int m_glyph = 16;
};

// Una fila del indice lateral (`.nrow`): la inicial al margen (solo la primera herramienta de cada letra),
// y la ficha con el icono, el nombre, la linea de estado y, en las herramientas, el interruptor. Un click en
// la fila la elige; el interruptor prende o apaga. La ficha elegida no se pinta aca: la dibuja NavShell,
// unida a la hoja.
class SidebarItem : public QWidget
{
    Q_OBJECT
public:
    // La columna de la inicial; la ficha empieza ahi.
    static constexpr int kLetterColumn = 30;

    SidebarItem(const QString &id, const QString &title, IconPainter icon, bool withSwitch, QWidget *parent = nullptr);

    QString id() const { return m_id; }
    void setLetter(const QString &letter);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    // Estado de la fila: prendida o no, texto y tono ("ok", "warn", "err" o vacio = gris).
    void setState(bool on, const QString &statusText, const QString &tone);
    ToggleSwitch *toggle() const { return m_switch; }
    // QA (--ui-probe nav-fit): el estado entero y cuanto aire le queda a `text` hasta el switch (o hasta el
    // borde de la ficha en General). Negativo = no entra.
    QString statusText() const { return m_statusText; }
    int statusSlack(const QString &text) const;
    // La ficha (sin la columna de la inicial), en coordenadas de la fila.
    QRect tabRect() const;

signals:
    void picked(const QString &id);
    void toggleRequested(const QString &id, bool on);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshColors();
    // El estado se corta con «…» si no entra en la fila; el texto entero va al tooltip.
    void elideStatus();

    QString m_id;
    bool m_selected = false;
    bool m_on = true;
    bool m_withSwitch = false;
    QLabel *m_letter = nullptr;
    ToolIcon *m_icon = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_status = nullptr;
    QString m_statusText;
    ToggleSwitch *m_switch = nullptr;
};

// La mesa debajo de la barra de titulo: el indice a la izquierda y la hoja a la derecha. Pinta la hoja
// (radio 12) y la ficha elegida del indice unida a ella, con las esquinas concavas del diseno.
class NavShell : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kNavWidth = 244;
    explicit NavShell(QWidget *parent = nullptr);
    // La hoja: lo que queda a la derecha del indice, con 4 arriba y 10 a la derecha y abajo.
    QRect sheetRect() const;
    void setSelectedItem(SidebarItem *item);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPointer<SidebarItem> m_selected;
};

// Encabezado de la hoja (`.ph`): titulo grande, etiqueta de plataformas, descripcion y (en las
// herramientas) el interruptor grande. Debajo, el lugar del estado vivo (`.live`) que el panel sube.
class ModuleHeader : public QWidget
{
    Q_OBJECT
public:
    ModuleHeader(const QString &title, const QString &platforms, const QString &description, bool withSwitch,
                 QWidget *parent = nullptr);
    void setOn(bool on);
    ToggleSwitch *toggle() const { return m_switch; }
    // Pone `status` (la tarjeta de estado del panel) en el encabezado. Vive mientras viva `owner` (el panel).
    void adoptLiveStatus(QWidget *status, QObject *owner);

signals:
    void toggleRequested(bool on);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ToggleSwitch *m_switch = nullptr;
    QWidget *m_liveSlot = nullptr;
    QVBoxLayout *m_liveLayout = nullptr;
    QPointer<QWidget> m_liveHome;
};

// "Win · mac", "Windows" o "mac" segun las plataformas del descriptor.
QString platformsText(int platforms);

// La ayuda de la pagina (`.how`, opcion B de la ronda 1): una franja plegable debajo del encabezado.
// Cerrada: icono, «How it works», «N steps» y la flecha. Abierta: los pasos numerados (los atajos como
// teclas) y la nota.
class HowItWorksStrip : public QFrame
{
    Q_OBJECT
public:
    explicit HowItWorksStrip(QWidget *parent = nullptr);
    // Los pasos y la nota de la herramienta (de su helpProvider). Se puede volver a llamar: rearma el cuerpo.
    void setSection(const HelpSection &section);
    void setOpen(bool open);
    bool isOpen() const { return m_open; }
    int stepCount() const { return m_steps; }

signals:
    // Lo abrio o cerro el usuario (click en la franja).
    void toggledByUser(bool open);

private:
    QAbstractButton *m_header = nullptr;
    StripGlyph *m_icon = nullptr;
    QLabel *m_count = nullptr;
    StripGlyph *m_chevron = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    bool m_open = false;
    int m_steps = 0;
};

// Panel de una herramienta apagada: "X is off", lo que no se carga, lo que hace al prenderse y "Turn on";
// debajo, el aviso del sistema (offNotice) si viene.
class OffPanel : public QWidget
{
    Q_OBJECT
public:
    OffPanel(const ModuleDescriptor &descriptor, const ModuleOffNotice &notice, QWidget *parent = nullptr);

signals:
    void turnOnRequested();
    void releaseRequested();
};

#endif // MIGHTYTOOLS_WINDOWPARTS_H
