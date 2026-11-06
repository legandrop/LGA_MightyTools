#ifndef MIGHTYTOOLS_MEETFRAMEOVERLAY_H
#define MIGHTYTOOLS_MEETFRAMEOVERLAY_H

#include <QMargins>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QLabel;
class QMenu;
class QPushButton;
class StatusDot;

// El marco de Meet Frame: un borde fino alrededor de la region y una barrita arriba (estado, tamaño, Pause,
// Size y cerrar). Siempre arriba, no toma foco y no sale en ninguna captura (MeetFrameSystem::prepareFrame).
// Adentro del borde es transparente al mouse: se sigue trabajando en lo que se comparte. Se mueve tomando la
// barrita y se estira desde el borde.
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

    void setState(bool paused, int hiddenWindows, const QSize &pixels, const QString &error);
    void setShortcuts(const QString &toggle, const QString &pause);
    QMenu *sizeMenu() const { return m_sizeMenu; }

signals:
    void pauseClicked();
    void closeClicked();
    // Tamaño pedido en pixeles reales, o un tamaño invalido para centrar la region en su pantalla.
    void sizeRequested(const QSize &pixels);
    // La region se movio o cambio de tamaño (durante el arrastre).
    void regionChanged();
    // Termino el arrastre: se guarda.
    void regionSettled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Qt::Edges edgesAt(const QPoint &pos) const;
    void updateMask();
    void updateTips();

    bool m_capture = false;
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
    QString m_toggleKeys;
    QString m_pauseKeys;
    QTimer m_settle;
};

#endif // MIGHTYTOOLS_MEETFRAMEOVERLAY_H
