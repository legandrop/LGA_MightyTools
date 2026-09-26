#ifndef MIGHTYTOOLS_SHORTCUTROW_H
#define MIGHTYTOOLS_SHORTCUTROW_H

#include "core/Shortcut.h"

#include <QWidget>

#include <functional>

class Chip;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;

// Una fila de la tarjeta "Shortcuts": nombre de la accion, una caja por tecla, el lapiz para
// cambiarla y la descripcion debajo.
//
// Al tocar el lapiz la fila GRABA: las teclas se reemplazan por un campo con borde violeta, la fila
// toma el teclado (grabKeyboard) y guarda la primera combinacion completa que el usuario aprieta.
// Esc cancela. Antes de aceptarla le pregunta al validador; si la rechaza, la fila vuelve al atajo
// anterior y muestra el motivo en rojo. Es el unico lugar de la app que usa el teclado.
class ShortcutRow : public QWidget
{
    Q_OBJECT

public:
    // Devuelve el motivo del rechazo (texto para la UI, en ingles) o vacio si la combinacion sirve.
    using Validator = std::function<QString(const Shortcut &)>;

    ShortcutRow(const QString &name, const QString &description, QWidget *parent = nullptr);

    void setShortcut(const Shortcut &shortcut);
    Shortcut shortcut() const { return m_shortcut; }
    void setValidator(Validator validator) { m_validator = std::move(validator); }

    // Chip junto al nombre (Folder Switch: "In use" cuando otra app o herramienta ya tiene el
    // atajo). Tono vacio lo oculta. Aditivo: sin llamarlo, la fila se ve igual que antes.
    void setExtraChip(const QString &tone, const QString &text);

    bool isRecording() const { return m_recording; }
    void startRecording();
    void cancelRecording();

    // Motivo en rojo en lugar de la descripcion (vacio = vuelve la descripcion).
    void setError(const QString &error);

    // Solo para la captura de QA: dibuja el estado "grabando" con un texto dado, sin tomar el teclado.
    void showRecordingFixture(const QString &partial);
    // Solo para la captura de QA: el lapiz con el aspecto de mouse encima.
    QPushButton *editButton() const { return m_editButton; }

signals:
    void shortcutRecorded(const Shortcut &shortcut);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    void rebuildKeys();
    void setRecordingVisible(bool recording, const QString &partial);
    void finishRecording();
    static QString partialText(Qt::KeyboardModifiers modifiers);

    Shortcut m_shortcut;
    QString m_description;
    Validator m_validator;
    bool m_recording = false;

    QLabel *m_name = nullptr;
    Chip *m_extraChip = nullptr;
    QWidget *m_keys = nullptr;
    QHBoxLayout *m_keysLayout = nullptr;
    QFrame *m_recorder = nullptr;
    QLabel *m_recorderText = nullptr;
    QPushButton *m_editButton = nullptr;
    QLabel *m_caption = nullptr;
};

#endif // MIGHTYTOOLS_SHORTCUTROW_H
