#ifndef MIGHTYTOOLS_APPTOGGLEMODULE_H
#define MIGHTYTOOLS_APPTOGGLEMODULE_H

#include "app/Module.h"
#include "modules/apptoggle/AppToggleState.h"
#include "ui/HelpSection.h"

#include <QHash>
#include <QSet>

class ModuleHotkeys;

// Un atajo por app: si no esta abierta la abre, si esta abierta sin el foco la trae al frente
// (restaurandola si estaba minimizada) y si ya tiene el foco la minimiza (mac: la oculta). Los atajos
// pasan por el mismo servicio que Window Layouts, con la misma prioridad en Windows.
class AppToggleModule : public Module
{
    Q_OBJECT

public:
    explicit AppToggleModule(ModuleContext &context);
    ~AppToggleModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;

    const AppToggleState &appsState() const { return m_state; }
    QString lastError() const { return m_lastError; }
    int registrationFailureCount() const { return m_registrationErrors.size(); }
    int externalConflictCount() const { return m_externalConflicts.size(); }
    QString registrationError(int id) const { return m_registrationErrors.value(id); }
    bool hasExternalConflict(int id) const { return m_externalConflicts.contains(id); }
    QString validateShortcut(int editingId, const Shortcut &shortcut, bool enabled = true) const;
    // El motivo por el que el editor no puede guardar la entrada (texto para la UI), o vacio.
    QString validateEntry(const AppEntry &entry) const;
    bool saveApp(AppEntry entry, QString *error = nullptr);
    bool setAppEnabled(int id, bool enabled);
    bool removeApp(int id);
    // La accion del atajo de la entrada `id`. En una corrida automatizada solo loguea lo que haria.
    void toggle(int id);

signals:
    void appsChanged();

private:
    void registerApps();

    AppToggleState m_state;
    bool m_started = false;
    ModuleHotkeys *m_hotkeys = nullptr;
    QSet<int> m_declaredIds;
    QHash<int, QString> m_registrationErrors;
    QSet<int> m_externalConflicts;
    QString m_lastError;
    QString m_captureState;
};

ModuleDescriptor appToggleDescriptor();
HelpSection appToggleHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_APPTOGGLEMODULE_H
