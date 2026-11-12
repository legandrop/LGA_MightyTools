#ifndef MIGHTYTOOLS_DICTATIONPANEL_H
#define MIGHTYTOOLS_DICTATIONPANEL_H

#include "app/PanelState.h"
#include "platform/AudioCapture.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QWidget>

#include <functional>

class DictationModule;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedLayout;
class QVBoxLayout;
class ShortcutRow;
class StatusCard;
class SwitchRow;
class UsageBar;

// La página de Dictation, con los materiales de App Toggle y Mouse Buttons: tarjeta de estado, el atajo, idioma
// y micrófono, palabras propias y reemplazos, el motor y el modelo (descarga con botón) y los últimos dictados.
class DictationPanel : public QWidget, public PanelState
{
public:
    // PanelState: algo a medio hacer que se perderia si la ventana se suelta (D-77).
    bool hasUnsavedWork() const override;
    DictationPanel(DictationModule *module, bool interactive, QWidget *parent = nullptr);
    void refresh();
    // Captura de QA: el editor de reemplazos con el segundo par cargado para editar, o el tacho de una pieza
    // armado («Confirm delete»).
    void showReplacementEditorForCapture();
    void armPieceDeleteForCapture(const QString &id) { armDelete(QStringLiteral("p") + id); }

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildShortcutCard();
    QWidget *buildSpeechCard();
    QWidget *buildWordsCard();
    QWidget *buildReplacementsCard();
    QWidget *buildModelCard();
    QWidget *buildRecentCard();
    void refreshStatus();
    void refreshReplacements();
    void refreshPieces();
    void refreshRecent();
    void showMenu(QPushButton *button, const QList<QPair<QString, QString>> &options, const QString &current,
                  const std::function<void(const QString &)> &choose);
    QString microphoneText() const;
    void saveWords();
    void saveRoot();
    void browseRoot();
    void submitReplacement();
    void finishReplacementField(QLineEdit *field);
    void editReplacement(int index);
    void resetReplacementEditor();
    void armDelete(const QString &key);
    void runLater(const std::function<void()> &action);

    DictationModule *m_module = nullptr;
    bool m_interactive = false;
    StatusCard *m_status = nullptr;
    ShortcutRow *m_shortcut = nullptr;
    QPushButton *m_language = nullptr;
    QPushButton *m_microphone = nullptr;
    SwitchRow *m_closingMarks = nullptr;
    QPlainTextEdit *m_words = nullptr;
    QWidget *m_replacementRows = nullptr;
    QLineEdit *m_from = nullptr;
    QLineEdit *m_to = nullptr;
    QPushButton *m_addReplacement = nullptr;
    QPushButton *m_cancelReplacement = nullptr;
    QLabel *m_replacementError = nullptr;
    int m_editing = -1;
    QLineEdit *m_root = nullptr;
    QLabel *m_rootError = nullptr;
    QWidget *m_pieceRows = nullptr;
    QPushButton *m_unload = nullptr;
    QWidget *m_recentRows = nullptr;
    QList<AudioCapture::Device> m_devices;
    QHash<QString, QStackedLayout *> m_deleteSlots; // "r<indice>" reemplazos, "p<id>" piezas
    QString m_armed;
    // Lo que muestran las listas: se rearman solo si cambio (una descarga avisa en cada 1 %, y rearmar
    // haria desaparecer «Cancel» o «Confirm delete» bajo el click).
    QString m_shownReplacements;
    QString m_shownRecent;
    QString m_shownPieces;
    struct PieceProgress
    {
        UsageBar *bar = nullptr;
        QLabel *percent = nullptr;
    };
    QHash<QString, PieceProgress> m_progress;
};

#endif // MIGHTYTOOLS_DICTATIONPANEL_H
