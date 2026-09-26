#ifndef MIGHTYTOOLS_MODULEHOST_H
#define MIGHTYTOOLS_MODULEHOST_H

#include "app/Module.h"

#include <map>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class ForegroundWatcher;
class HostServices;
class HotkeyHub;
class InputInjector;
class ModuleContextImpl;
class SettingsStore;

// Como corre el host. La app de verdad: todo en false. Las corridas automatizadas (self-test,
// medicion, captura) prenden automatedRun: atajos contados, inyector en solo loguear, nada escrito
// en el sistema.
struct HostOptions
{
    bool captureMode = false;  ///< --ui-shot / --ui-probe: los modulos se construyen sin start()
    bool automatedRun = false; ///< toda corrida automatizada (incluye captura)
    bool buildTree = false;    ///< el exe corre desde un arbol de build (LgaBuildTree::isBuildTree)
    bool dryRunInput = false;  ///< --dry-run-input o dryRunInput=true en debug_flags
    // El observador de ventana al frente se instala de verdad aunque la corrida sea automatizada: solo
    // observa, no actua. Lo usan la medicion de consumo y el conteo de hooks del self-test.
    bool observeForeground = false;
};

// Dueno de las herramientas (plan 4.2 y 4.3). Lee [modules]/<id>/enabled, construye cada modulo al
// prenderlo y lo destruye al apagarlo, con el orden del contrato (Module.h, "Vida"):
//   stop() -> panel con deleteLater -> release() del modulo y deleteLater -> el contexto se borra
//   al recibir destroyed() del modulo.
// Lo apagado no existe: solo su descriptor.
class ModuleHost : public QObject
{
    Q_OBJECT

public:
    ModuleHost(QList<ModuleDescriptor> descriptors, SettingsStore *store, const HostOptions &options,
               QObject *parent = nullptr);
    ~ModuleHost() override;

    void setHostServices(HostServices *services) { m_services = services; }
    HostServices *hostServices() const { return m_services; }
    const HostOptions &options() const { return m_options; }
    SettingsStore *store() const { return m_store; }

    const QList<ModuleDescriptor> &descriptors() const { return m_descriptors; }
    const ModuleDescriptor *descriptor(const QString &id) const;
    int indexOf(const QString &id) const;

    // Lo que dice settings.ini (lo que el usuario eligio).
    bool isEnabled(const QString &id) const;
    // Si el objeto del modulo existe ahora.
    bool isRunning(const QString &id) const;
    Module *module(const QString &id) const;

    // Prende o apaga: escribe [modules]/<id>/enabled y construye o destruye el modulo.
    void setEnabled(const QString &id, bool enabled);
    // Arranque de la app: prende los que estaban prendidos.
    void startEnabled();
    // Salida: apaga todos (sin tocar settings.ini) y borra en el acto.
    void shutdown();

    // El panel de la derecha, creado la primera vez que se pide. nullptr si esta apagado.
    QWidget *panel(const QString &id, QWidget *parent);
    QWidget *existingPanel(const QString &id) const;

    ModuleStatus status(const QString &id) const;
    int runningCount() const;
    // Ninguna prendida, o todas las prendidas en pausa: el icono de la bandeja se atenua.
    bool allPausedOrOff() const;
    QStringList trayTooltipLines() const;
    QStringList runningIds() const;

    // Lectura de la seccion [<id>] sin construir el modulo.
    SettingsReader reader(const QString &id) const;

    HotkeyHub *hotkeyHub() const { return m_hotkeys; }
    // Inyector compartido, perezoso: lo piden los contextos y se destruye cuando lo suelta el ultimo.
    InputInjector *acquireInjector();
    void releaseInjector();
    bool injectorAlive() const { return m_injector != nullptr; }

    // Observador de ventana al frente compartido (plan 4.4), mismo refcount que el inyector: hoy lo
    // usa Folder Switch; el dia que Nuke Shortcuts migre su propio hook (NukeWatcherWin) aca, sigue
    // sirviendo a los dos con una sola instancia.
    ForegroundWatcher *acquireForeground();
    void releaseForeground();
    bool foregroundAlive() const { return m_foreground != nullptr; }

    // Captura: construye el modulo en modo captura (sin start) y le fija el estado. False si el
    // modulo no existe o no conoce el estado (el arnes lo reporta como error).
    bool enableForCapture(const QString &id, const QString &state);

signals:
    // Se prendio o se apago (el objeto ya existe o ya se pidio borrar).
    void moduleToggled(const QString &id, bool running);
    // Cambio el status(), el tooltip o la pausa de un modulo prendido.
    void moduleStatusChanged(const QString &id);
    // Se borro el objeto del modulo: recien ahi el menu de la bandeja se puede rearmar.
    void moduleDestroyed(const QString &id);

private:
    struct Slot
    {
        std::unique_ptr<Module> module;
        ModuleContextImpl *context = nullptr;
        QPointer<QWidget> panel;
    };

    bool build(const QString &id, bool start);
    void destroy(const QString &id, bool immediate);

    QList<ModuleDescriptor> m_descriptors;
    SettingsStore *m_store = nullptr;
    HostOptions m_options;
    HostServices *m_services = nullptr;
    HotkeyHub *m_hotkeys = nullptr;
    std::map<QString, Slot> m_slots;
    // Modulos apagados cuyo deleteLater todavia no corrio: al cerrar el host se borran en el acto.
    QList<QPointer<Module>> m_pendingDelete;
    std::unique_ptr<InputInjector> m_injector;
    int m_injectorUsers = 0;
    std::unique_ptr<ForegroundWatcher> m_foreground;
    int m_foregroundUsers = 0;
};

#endif // MIGHTYTOOLS_MODULEHOST_H
