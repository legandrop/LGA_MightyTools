#ifndef MIGHTYTOOLS_DICTATIONOVERLAY_H
#define MIGHTYTOOLS_DICTATIONOVERLAY_H

#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <memory>

class QPropertyAnimation;
class QScreen;
class QTextLayout;

// La ventanita del dictado: muestra lo que se va entendiendo mientras se habla. Sin marco, siempre arriba, nunca
// toma el foco ni se activa (OverlayWindow) y los clicks la atraviesan: la app donde se escribe sigue al frente.
//
// Ubicación: abajo al centro del área útil del monitor de la ventana de destino, sobre la barra de tareas. Se
// eligió en vez de «junto al cursor de texto» porque Chrome, Electron y muchas apps no informan ese cursor
// (GetGUIThreadInfo da un rectángulo vacío): el lugar cambiaría de app en app. Abajo al centro siempre es el
// mismo y no tapa lo que se está escribiendo. Crece hacia arriba, hasta tres líneas.
//
// Una pastilla: punto rojo de grabando y medidor de nivel a la izquierda, el texto a la derecha. Lo confirmado
// en el color de texto claro y lo provisional en gris; si no entra en tres líneas, se ve el final (lo último
// dicho) con «…» adelante. Sin texto, el estado («Listening…»). Un error va en rojo.
class DictationOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class State {
        Listening,    ///< grabando (con o sin texto en vivo)
        Loading,      ///< grabando mientras el motor carga el modelo
        Transcribing, ///< ya no graba: la última pasada
        Error,        ///< `message` en rojo
    };

    // Sin padre: la ventanita flotante. Con padre: un hijo común (las capturas de QA).
    explicit DictationOverlay(QWidget *parent = nullptr);
    ~DictationOverlay() override;

    void setState(State state, const QString &message = QString());
    // 0..1 (AudioCapture::level). Sube enseguida y baja suave.
    void setLevel(float level);
    void setText(const QString &confirmed, const QString &provisional);
    // Se muestra (o se queda, si se estaba yendo) en el monitor de esa ventana nativa, con un fundido.
    void showFor(quintptr nativeWindow);
    // Se va con un fundido después de `delayMs`. showFor() antes de eso la deja.
    void hideSoon(int delayMs = 700);

    QSize sizeHint() const override;

    // Estados de --ui-shot ("overlay-listening", "overlay-live"...) y la ventanita armada en uno, como hijo de
    // `parent`; nullptr si el estado no existe.
    static QStringList captureStates();
    static DictationOverlay *createCaptureWidget(const QString &state, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void relayout();
    void place();
    void fadeTo(qreal opacity);
    bool recording() const { return m_state == State::Listening || m_state == State::Loading; }
    int indicatorWidth() const;

    State m_state = State::Listening;
    QString m_message;
    QString m_confirmed;
    QString m_provisional;
    float m_level = 0.0f;
    std::unique_ptr<QTextLayout> m_layout;
    QSize m_textSize;
    QPointer<QScreen> m_screen;
    QTimer m_hideTimer;
    QPropertyAnimation *m_fade = nullptr;
    bool m_hiding = false;
    bool m_nativeReady = false;
};

#endif // MIGHTYTOOLS_DICTATIONOVERLAY_H
