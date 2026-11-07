#ifndef MIGHTYTOOLS_MEETFRAMEMODULE_H
#define MIGHTYTOOLS_MEETFRAMEMODULE_H

#include "app/Module.h"
#include "platform/MeetFrameSystem.h"
#include "ui/HelpSection.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#include <memory>

class ModuleHotkeys;
class MeetFrameOverlay;

// Meet Frame: compartir solo una parte de la pantalla en Teams, Meet o Zoom, sin los avisos, el escritorio ni
// las apps elegidas. Con la herramienta prendida y el marco cerrado solo hay atajos: la captura, la ventana
// compartida y el marco existen mientras el marco esta abierto. En una corrida automatizada nunca se crea una
// sesion: abrir el marco solo cambia el estado y lo anota en el log.
class MeetFrameModule : public Module
{
    Q_OBJECT

public:
    enum ShortcutId { ToggleShortcut = 1, PauseShortcut = 2 };

    struct CustomApp
    {
        QString id;   ///< exe (Windows) o bundle id (mac), en minusculas
        QString name; ///< lo que se muestra
        QString path; ///< para el icono; puede estar vacia
    };

    explicit MeetFrameModule(ModuleContext &context);
    ~MeetFrameModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    void fillTrayMenu(QMenu *menu) override;
    bool isPaused() const override;
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

    bool frameShown() const { return m_shown; }
    bool paused() const { return m_paused; }
    int hiddenWindows() const;
    QString error() const { return m_error; }
    bool needsPermission() const;

    void showFrame();
    void hideFrame();
    void toggleFrame();
    void togglePause();
    void requestPermission();

    // Ajustes
    bool groupEnabled(const QString &id) const;
    void setGroupEnabled(const QString &id, bool enabled);
    bool hideNotifications() const;
    void setHideNotifications(bool hide);
    bool hideDesktop() const;
    void setHideDesktop(bool hide);
    bool showCursor() const;
    void setShowCursor(bool show);
    int fps() const;
    void setFps(int fps);
    QList<CustomApp> customApps() const;
    bool addApp(const CustomApp &app);
    bool removeApp(const QString &id);

    Shortcut shortcut(int id) const;
    QString validateShortcut(int id, const Shortcut &keys) const;
    bool setShortcut(int id, const Shortcut &keys, QString *error);
    QString shortcutError() const { return m_shortcutError; }

    static QList<CustomApp> appsFromJson(const QString &json);
    static QString appsToJson(const QList<CustomApp> &apps);
    static Shortcut defaultShortcut(int id);

signals:
    void settingsChanged();

private:
    MeetFrameSystem::Options options() const;
    void registerShortcuts();
    void unregisterShortcuts();
    void syncPauseShortcut();
    void applyOptions();
    void syncOverlay();
    void onRegionChanged();
    void applyRegion();
    void watchTick();
    void logStats(const char *when);
    void saveRegion();
    QRect savedRegion() const;
    void resizeRegion(const QSize &pixels);
    void changed();

    ModuleHotkeys *m_hotkeys = nullptr;
    bool m_started = false;
    bool m_shown = false;
    bool m_paused = false;
    bool m_pauseRegistered = false;
    QString m_error;
    QString m_shortcutError;
    QPointer<MeetFrameOverlay> m_overlay;
    std::unique_ptr<MeetFrameSystem::Session> m_session;
    // Mientras se mueve o se estira el marco, la captura y la ventana compartida se actualizan como mucho cada
    // kRegionMs (antes: en cada pixel, dos veces). Corre solo con el marco a la vista.
    QTimer m_regionTimer;
    int m_regionRequests = 0;
    int m_regionApplied = 0;
    // Para el log, con el marco a la vista: cuanto se atrasa el hilo principal (un reloj cada 100 ms) y un
    // resumen cada kStatsMs.
    QTimer m_watch;
    QElapsedTimer m_watchClock;
    QElapsedTimer m_statsClock;
    qint64 m_watchMaxLateMs = 0;
    int m_watchLate = 0;
    QString m_captureState;
    int m_captureHidden = 0;
};

ModuleDescriptor meetFrameDescriptor();
HelpSection meetFrameHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_MEETFRAMEMODULE_H
