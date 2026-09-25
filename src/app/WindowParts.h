#ifndef MIGHTYTOOLS_WINDOWPARTS_H
#define MIGHTYTOOLS_WINDOWPARTS_H

#include "app/Module.h"

#include <QFrame>
#include <QString>
#include <QWidget>

#include <functional>

class Chip;
class QCheckBox;
class QLabel;
class QPushButton;
class StatusDot;
class ToggleSwitch;

// Piezas de la ventana forma A que dibuja el host (canvas, secciones 1 A, 3 y 7). Ninguna conoce a
// un modulo en particular: todo sale del descriptor y de ModuleStatus.

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

// Una fila de la barra lateral (`.item`): icono, nombre, linea de estado y, en las herramientas,
// el interruptor. Un click en la fila la elige; el interruptor prende o apaga.
class SidebarItem : public QWidget
{
    Q_OBJECT
public:
    SidebarItem(const QString &id, const QString &title, IconPainter icon, bool withSwitch, QWidget *parent = nullptr);

    QString id() const { return m_id; }
    void setSelected(bool selected);
    // Estado de la fila: prendida o no, texto y tono ("ok", "warn", "err" o vacio = gris).
    void setState(bool on, const QString &statusText, const QString &tone);
    ToggleSwitch *toggle() const { return m_switch; }

signals:
    void picked(const QString &id);
    void toggleRequested(const QString &id, bool on);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QString m_id;
    bool m_selected = false;
    bool m_on = true;
    bool m_withSwitch = false;
    ToolIcon *m_icon = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_status = nullptr;
    ToggleSwitch *m_switch = nullptr;
};

// Encabezado del panel (`.modHead`): icono en su caja, titulo, etiqueta de plataformas,
// descripcion y (en las herramientas) el interruptor.
class ModuleHeader : public QWidget
{
    Q_OBJECT
public:
    ModuleHeader(const QString &title, const QString &platforms, const QString &description, IconPainter icon,
                 bool withSwitch, QWidget *parent = nullptr);
    void setOn(bool on);
    ToggleSwitch *toggle() const { return m_switch; }

signals:
    void toggleRequested(bool on);

private:
    ToolIcon *m_icon = nullptr;
    ToggleSwitch *m_switch = nullptr;
};

// "Win · mac", "Windows" o "mac" segun las plataformas del descriptor.
QString platformsText(int platforms);

// Panel de una herramienta apagada (canvas, seccion 3): "X is off", lo que no se carga, lo que hace
// al prenderse y "Turn on"; debajo, el aviso del sistema (offNotice) si viene.
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
