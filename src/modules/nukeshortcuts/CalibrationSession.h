#ifndef MIGHTYTOOLS_CALIBRATIONSESSION_H
#define MIGHTYTOOLS_CALIBRATIONSESSION_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QWidget>

class InputInjector;
class NukeWatcher;
class QLabel;
class QTimer;

// La burbuja que sigue al puntero mientras el calibrador espera el click: "Click inside the Dope
// Sheet" y el punto en porcentaje de la ventana de Nuke, o "Move over the Nuke window" si el puntero
// no esta sobre Nuke. No toma el foco ni recibe el mouse (el click tiene que llegarle a Nuke).
class CalibrationBubble : public QWidget
{
    Q_OBJECT
public:
    explicit CalibrationBubble(QWidget *parent = nullptr);

    // Sobre Nuke, con el punto en fraccion de su ventana.
    void showOverNuke(const QPointF &spot);
    // Afuera de Nuke. `rejected` = true despues de un click que no cayo en Nuke.
    void showOutside(bool rejected);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_title = nullptr;
    QLabel *m_meta = nullptr;
};

// El paso "esperando el click" del calibrador. No captura el mouse: mira el estado FISICO del boton
// principal y de Esc cada 16 ms (como hacia la version AutoHotkey), asi el click le llega a Nuke y
// el Dope Sheet queda enfocado. Al primer click sobre una ventana de Nuke, calcula el punto en
// fraccion de su ventana principal y termina.
class CalibrationSession : public QObject
{
    Q_OBJECT
public:
    CalibrationSession(NukeWatcher *watcher, InputInjector *injector, QObject *parent = nullptr);
    ~CalibrationSession() override;

    void start();
    void cancel();

signals:
    void calibrated(const QPointF &spot);
    void cancelled();

private:
    void poll();
    void placeBubble();
    void resolveClick(const QPoint &nativePoint);

    NukeWatcher *m_watcher = nullptr;
    InputInjector *m_injector = nullptr;
    CalibrationBubble *m_bubble = nullptr;
    QTimer *m_timer = nullptr;
    bool m_buttonWasDown = true; // el click en "Start" puede seguir apretado al arrancar
    bool m_resolving = false;
    bool m_rejected = false;
};

#endif // MIGHTYTOOLS_CALIBRATIONSESSION_H
