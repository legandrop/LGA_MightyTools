#ifndef MIGHTYTOOLS_DICTATIONMODULE_H
#define MIGHTYTOOLS_DICTATIONMODULE_H

#include "app/Module.h"
#include "modules/dictation/DictationFlow.h"
#include "modules/dictation/DictationModels.h"
#include "platform/AudioCapture.h"
#include "ui/HelpSection.h"

#include <QElapsedTimer>
#include <QList>
#include <QPointer>

class DictationEngine;
class ModuleHotkeys;
class QTimer;

// Dictado por voz en cualquier campo de texto (solo Windows por ahora). Un atajo (Win+H por defecto, con
// prioridad) graba mientras se mantiene o entre dos toques; el texto se ve en vivo en una ventanita y al
// terminar se escribe en la ventana que estaba al frente, tecla por tecla y sin el portapapeles.
//
// Prendida: el atajo registrado; el motor (otro proceso) se carga cuando el usuario la prende (o termina de
// bajar las piezas) o con el primer dictado, nunca al arrancar la app, y se descarga solo después de N minutos
// sin dictar. El micrófono se abre solo mientras graba, y Esc se registra solo mientras hay un dictado en curso
// (también escribiendo: corta lo que falta). Apagada no existe nada: ni proceso, ni gancho, ni timers.
// En una corrida automatizada no se abre el micrófono, no se escribe, no se muestra la ventanita, no se baja
// nada y el motor no se precarga.
class DictationModule : public Module, protected DictationFlow::Io
{
    Q_OBJECT

public:
    enum class EngineState { Off, Loading, Ready, Unloaded, Error };

    struct PieceInfo
    {
        QString id;
        QString title;
        QString license;
        qint64 size = 0;
        DictationModels::State state = DictationModels::State::Missing;
        int progress = 0;
        QString error;
    };

    static constexpr int kHistoryMax = 10;

    explicit DictationModule(ModuleContext &context);
    ~DictationModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;

    // ---- Ajustes ([dictation] de settings.ini)
    Shortcut shortcut() const;
    QString validateShortcut(const Shortcut &shortcut) const;
    bool setShortcut(const Shortcut &shortcut, QString *error = nullptr);
    QString shortcutError() const { return m_shortcutError; }
    QString language() const; ///< "es", "en"... o "auto"
    void setLanguage(const QString &code);
    QString microphone() const; ///< id del sistema; vacío = el de por defecto de Windows
    void setMicrophone(const QString &id);
    QString customWords() const;
    void setCustomWords(const QString &words);
    DictationText::Replacements replacements() const;
    void setReplacements(const DictationText::Replacements &replacements);
    QString modelsRoot() const; ///< la carpeta elegida, o la de por defecto
    void setModelsRoot(const QString &root); ///< solo una ruta completa (fullPath); si no, no cambia nada
    static bool fullPath(const QString &path); ///< unidad + barra, o UNC
    int unloadMinutes() const; ///< 0 = nunca
    void setUnloadMinutes(int minutes);

    // ---- Motor y modelo
    QList<PieceInfo> pieces() const;
    bool piecesReady() const;
    void download(const QString &id);
    void downloadMissing();
    void cancelDownload(const QString &id);
    bool removePiece(const QString &id, QString *error = nullptr);
    void refreshModels();
    EngineState engineState() const { return m_engineState; }
    QString engineError() const { return m_engineError; }

    // ---- Lo que muestra la página
    QList<DictationFlow::Entry> history() const { return m_history; }
    QList<AudioCapture::Device> devices() const;
    DictationFlow::Phase phase() const { return m_flow.phase(); }

    // ---- Texto: puros, para la página y el self-test
    static QStringList languageCodes();
    static QString languageName(const QString &code);
    static QList<int> unloadChoices();
    static QString unloadText(int minutes);
    static QStringList vocabularyFrom(const QString &words);
    static QString contextPhrase(const QString &language);
    static QString replacementsToJson(const DictationText::Replacements &replacements);
    static DictationText::Replacements replacementsFromJson(const QString &json);

    // QA
    qint64 enginePidForQa() const;
    bool escapeRegisteredForQa() const;
    bool warmUpPendingForQa() const { return m_warmUp; }

signals:
    // Cambió algo que muestra la página (ajustes, piezas, motor, historial).
    void changed();

protected:
    // DictationFlow::Io con el sistema de verdad (la QA de --simulate-action reemplaza el micrófono y la escritura).
    quintptr foregroundWindow() override;
    QString engineProblem() override;
    bool engineReady() override;
    void engineBegin() override;
    void engineFeed(const QVector<float> &samples) override;
    void engineFinish() override;
    void engineCancel() override;
    bool startCapture() override;
    void stopCapture() override;
    bool type(quintptr target, const QString &text) override;
    void stopTyping() override;
    void showOverlay(quintptr target) override;
    void overlayState(DictationOverlay::State state, const QString &message) override;
    void overlayText(const QString &confirmed, const QString &provisional) override;
    void overlayLevel(float level) override;
    void hideOverlay(int delayMs) override;
    void setActive(bool active) override;

    // Precargar el motor al prender (y al quedar listas las piezas). En una corrida automatizada no.
    virtual bool preloadAllowed() const;
    DictationFlow &flow() { return m_flow; }

private:
    void registerShortcut();
    void unregisterAll();
    void openModels();
    void closeModels();
    void ensureEngine();
    void stopEngine();
    void applyFlowSettings();
    void onPieceChanged();
    void addHistory(const DictationFlow::Entry &entry);
    void notifyChanged();

    DictationFlow m_flow;
    bool m_started = false;
    bool m_warmUp = false; ///< cargar el motor apenas esten las piezas (lo prendio o lo bajo el usuario)
    ModuleHotkeys *m_hotkeys = nullptr;
    QString m_shortcutError;
    DictationModels *m_models = nullptr;
    DictationEngine *m_engine = nullptr;
    EngineState m_engineState = EngineState::Off;
    QString m_engineError;
    AudioCapture *m_capture = nullptr;
    TextTyper *m_typer = nullptr;
    QPointer<DictationOverlay> m_overlay;
    QTimer *m_tick = nullptr;
    QElapsedTimer m_clock;
    bool m_escRegistered = false;     ///< Esc sola
    bool m_escHeldRegistered = false; ///< Esc con los modificadores del atajo (mantener Win+H)
    QList<DictationFlow::Entry> m_history;

    // Captura (--ui-shot): el estado de prueba, sin leer nada del sistema.
    QString m_captureState;
    QList<PieceInfo> m_capturePieces;
};

ModuleDescriptor dictationDescriptor();
HelpSection dictationHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_DICTATIONMODULE_H
