#ifndef MIGHTYTOOLS_ACTIONRUNNER_H
#define MIGHTYTOOLS_ACTIONRUNNER_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>

#include <functional>
#include <memory>

class InputInjector;

// Las dos acciones sobre Nuke, como secuencias de pasos con pausas entre medio. Las pausas van por
// QTimer y no con sleep: la app nunca bloquea su hilo mientras manda la secuencia.
//
// Tiempos tomados de la version AutoHotkey, que andaban en uso real: 50 ms para que abra el menu
// contextual, 30 ms antes de Enter, 10 ms entre Ctrl+A y F. Se suma una pausa corta despues de
// soltar los modificadores para que Nuke los procese antes del click.
class ActionRunner : public QObject
{
    Q_OBJECT

public:
    explicit ActionRunner(InputInjector *injector, QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }

    // Click derecho donde esta el puntero, flecha abajo y Enter: el primer item del menu contextual
    // de un knob es "Set key". False si ya habia una secuencia corriendo.
    bool runAddKeyframe();

    // Click en el punto calibrado del Dope Sheet, Ctrl+A (Cmd+A en mac), F, y el puntero vuelve a
    // donde estaba. `nukeFrame` es el marco nativo de la ventana principal de Nuke y `spot` el
    // punto guardado en fraccion de ese marco. False si ya habia una secuencia corriendo o si el
    // marco no es valido.
    bool runFrameDopeSheet(const QRect &nukeFrame, const QPointF &spot);

    // Conversion entre el punto guardado (fraccion 0..1 del marco de Nuke) y un punto nativo.
    static QPoint spotToNative(const QRect &frame, const QPointF &spot);
    static QPointF nativeToSpot(const QRect &frame, const QPoint &nativePoint);

signals:
    void finished();

private:
    struct Step
    {
        int delayBeforeMs = 0;
        std::function<void()> run;
    };

    void start(const QVector<Step> &steps);
    void runNext();

    InputInjector *m_injector = nullptr;
    QVector<Step> m_steps;
    int m_index = 0;
    bool m_busy = false;
};

#endif // MIGHTYTOOLS_ACTIONRUNNER_H
