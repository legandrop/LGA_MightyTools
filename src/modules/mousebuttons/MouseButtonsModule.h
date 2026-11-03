#ifndef MIGHTYTOOLS_MOUSEBUTTONSMODULE_H
#define MIGHTYTOOLS_MOUSEBUTTONSMODULE_H

#include "app/Module.h"
#include "modules/mousebuttons/MouseButtonsState.h"
#include "ui/HelpSection.h"

#include <memory>

class ForegroundWatcher;

// Reglas «app -> boton -> teclas (y clic izquierdo) mientras esta apretado» (solo Windows). El gancho de
// mouse existe solo mientras la ventana del frente es de una app con una regla activada: lo pide el cambio
// de frente (el observador compartido) y se va, soltando todo, al cambiar de app, al apagar una regla o la
// herramienta. En una corrida automatizada nunca se instala: solo se anota que se instalaria.
class MouseButtonsModule : public Module
{
    Q_OBJECT

public:
    explicit MouseButtonsModule(ModuleContext &context);
    ~MouseButtonsModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;

    const MouseButtonsState &rulesState() const { return m_state; }
    QString lastError() const { return m_lastError; }
    // El motivo por el que el editor no puede guardar la regla (texto para la UI), o vacio.
    QString validateRule(const MouseRule &rule) const;
    bool saveRule(MouseRule rule, QString *error = nullptr);
    bool setRuleEnabled(int id, bool enabled);
    bool removeRule(int id);

    // La app al frente cambio (lo llama el observador compartido; el self-test, a mano).
    void onForeground(quint32 pid, const QString &exeName);
    // El proceso para el que hay (o, en una corrida automatizada, habria) un gancho; 0 sin gancho.
    quint32 hookPid() const { return m_hookPid; }

    static QString buttonText(MouseButtonsSystem::Button button);
    // «Holds Space and the left button»: lo que hace la regla, para su fila.
    static QString summary(const MouseRule &rule);

signals:
    void rulesChanged();

private:
    void syncWatcher();
    void syncHook();
    void changed();

    MouseButtonsState m_state;
    bool m_started = false;
    ForegroundWatcher *m_watcher = nullptr;
    quint32 m_frontPid = 0;
    QString m_frontExe;
    quint32 m_hookPid = 0;
    QList<MouseButtonsSystem::Rule> m_hookRules;
    std::unique_ptr<MouseButtonsSystem::Hook> m_hook;
    QString m_lastError;
    QString m_captureState;
};

ModuleDescriptor mouseButtonsDescriptor();
HelpSection mouseButtonsHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_MOUSEBUTTONSMODULE_H
