#ifndef MIGHTYTOOLS_MEETFRAMEOVERLAY_H
#define MIGHTYTOOLS_MEETFRAMEOVERLAY_H

#include <QElapsedTimer>
#include <QMargins>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QScreen;
class QMenu;
class QPushButton;
class StatusDot;

// El marco de Meet Frame: un borde fino alrededor de la region y una barrita arriba (estado, tamaño, Pause,
// Size y cerrar). Siempre arriba, no toma foco y no sale en ninguna captura (MeetFrameSystem::prepareFrame).
// Adentro del borde es transparente al mouse: se sigue trabajando en lo que se comparte. Se mueve tomando la
// barrita y se estira desde el borde, a mano y sobre un lienzo (MeetFrameOverlay.cpp explica por que), con
// la proporcion fija si se eligio una.
class MeetFrameOverlay : public QWidget
{
    Q_OBJECT

public:
    // `capture`: solo para la captura de QA (sin ventana nativa ni nada del sistema).
    explicit MeetFrameOverlay(bool capture, QWidget *parent = nullptr);

    static QMargins frameMargins();
    static QSize minimumRegion();

    // La region en coordenadas logicas globales de Qt (sin los margenes).
    QRect region() const;
    void setRegion(const QRect &region);
    // El marco (region con sus margenes) en coordenadas de la ventana: la ventana es un lienzo mas grande que el
    // marco y el marco va adentro (en la captura de QA, sin lienzo, son lo mismo).
    QRect frameRect() const;
    // Al estirar se mantiene esta proporcion de la region (16:9, 4:3...); invalida, libre.
    void setAspectRatio(const QSize &ratio);
    // La forma elegida ("free", "16:9", "4:3", "1:1", "current") y su proporcion (invalida si es libre): la
    // marca en el menu Size y la muestra junto al tamaño.
    void setAspectChoice(const QString &aspect, const QSize &ratio);

    void setState(bool paused, int hiddenWindows, const QSize &pixels, const QString &error);
    void setShortcuts(const QString &toggle, const QString &pause);
    // El mouse sin botones en `pos` (coordenadas del marco; fuera, cualquier punto afuera): elige el cursor y
    // contesta si el marco quiere el mouse ahi (borde o barrita); adentro de la region, no.
    // Lo llama mouseMoveEvent y, en mac, MeetFrameSystem::trackFrameMouse (con la app inactiva Qt no avisa).
    bool hoverAt(const QPoint &pos);
    Qt::CursorShape cursorShape() const { return m_cursor; }
    bool dragging() const { return m_dragging; }
    QMenu *sizeMenu() const { return m_sizeMenu; }

signals:
    void pauseClicked();
    void closeClicked();
    // Tamaño pedido en pixeles reales, o un tamaño invalido para centrar la region en su pantalla.
    void sizeRequested(const QSize &pixels);
    // Se eligio una forma en el menu Size ("free", "16:9", "4:3", "1:1" o "current").
    void aspectRequested(const QString &aspect);
    // La region se movio o cambio de tamaño (durante el arrastre).
    void regionChanged();
    // Termino el arrastre: se guarda.
    void regionSettled();
    // Se solto el mouse despues de mover o estirar (antes de regionSettled, que espera 400 ms).
    void dragFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Qt::Edges edgesAt(const QPoint &pos) const;
    Qt::CursorShape cursorAt(const QPoint &pos) const;
    void applyCursor(Qt::CursorShape shape);
    void hoverChild(const QPoint &pos);
    void dragTo(const QPoint &global, bool measureGap = true);
    // Cierra un arrastre propio: log y dragFinished (la captura se aplica ahi). Tambien si no llego el release.
    void finishDrag();
    void setFrameGlobal(const QRect &frame);
    QRegion frameArea() const;
    QRect keepAspect(const QRect &frame);
    void watchScreen(QScreen *screen);
    void replaceCanvas();
    void updateMask();
    void layoutBar();
    void updateTips();

    bool m_capture = false;
    bool m_canvas = false;
    QRect m_frame;   // con lienzo: el marco en coordenadas de la ventana
    QSize m_aspect;  // proporcion al estirar; invalida, libre
    int m_aspectAxis = 0; // en una esquina con forma fija: 0 sin decidir, 1 manda el ancho, 2 el alto
    bool m_placing = false; // el lienzo se esta acomodando (sus moveEvent/resizeEvent no son del sistema)
    QPointer<QScreen> m_screen;
    bool m_paused = false;
    QString m_error;
    QWidget *m_bar = nullptr;
    StatusDot *m_dot = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_hidden = nullptr;
    QPushButton *m_pause = nullptr;
    QPushButton *m_sizeButton = nullptr;
    QPushButton *m_close = nullptr;
    QMenu *m_sizeMenu = nullptr;
    QList<QAction *> m_shapeActions;
    QString m_shapeText; // la forma fija junto al tamaño; vacio si es libre
    QString m_toggleKeys;
    QString m_pauseKeys;
    QTimer m_settle;
    // Arrastre a mano: la barrita (sin bordes) mueve; un borde estira.
    bool m_dragging = false;
    Qt::Edges m_dragEdges;
    QPoint m_pressGlobal;
    QRect m_pressGeometry;
    Qt::CursorShape m_cursor = Qt::ArrowCursor;
    // Para el log de cada arrastre.
    QElapsedTimer m_dragClock;
    int m_dragSteps = 0;
    double m_dragStepMs = 0;
    double m_dragStepMaxMs = 0;
    QElapsedTimer m_dragGapClock; // la pausa mas larga entre dos pasos (si es grande, el sistema no entrego eventos)
    qint64 m_dragGapMaxMs = 0;
    int m_dragPaints = 0;
    double m_dragPaintMs = 0;
    // Lo que muestra la barrita: si no cambia, no se rearma.
    bool m_stateSet = false;
    int m_hiddenCount = 0;
    QSize m_pixels;
    QPointer<QWidget> m_hoverChild; // el control de la barrita bajo el mouse con la app inactiva
};

#endif // MIGHTYTOOLS_MEETFRAMEOVERLAY_H
