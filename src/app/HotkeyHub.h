#ifndef MIGHTYTOOLS_HOTKEYHUB_H
#define MIGHTYTOOLS_HOTKEYHUB_H

#include "app/ModuleContext.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

class HotkeyService;
class ModuleHotkeysImpl;

// El unico dueno de los atajos globales del proceso (plan 4.4). Cada modulo prendido que pide
// atajos recibe un ModuleHotkeysImpl con su rango de ids; el HotkeyService del sistema se crea con
// el primero que lo pide y se destruye cuando se suelta el ultimo (refcount).
//
// En una corrida automatizada NO se crea HotkeyService: se cuentan los registros y se responde como
// si el sistema los aceptara (contrato de ModuleHotkeys).
class HotkeyHub : public QObject
{
    Q_OBJECT

public:
    // Atajos configurados de las herramientas APAGADAS: devuelve el titulo de la que tiene
    // `shortcut` (sin contar `exceptModuleId`), o vacio. La arma ModuleHost con los descriptores.
    using OffModuleLookup = std::function<QString(const Shortcut &shortcut, const QString &exceptModuleId)>;

    HotkeyHub(bool automatedRun, OffModuleLookup offLookup, QObject *parent = nullptr);
    ~HotkeyHub() override;

    // `index` es la posicion del modulo en el registro: fija su rango de ids.
    ModuleHotkeysImpl *acquire(const QString &moduleId, const QString &moduleTitle, int index);
    void release(ModuleHotkeysImpl *hotkeys);

    // Para el self-test y la medicion.
    int registeredCount() const;
    int declaredCount() const;
    int clientCount() const { return m_clients.size(); }
    bool systemServiceAlive() const { return m_service != nullptr; }
    bool automatedRun() const { return m_automated; }

    // Titulo del modulo PRENDIDO que declaro `shortcut`, sin contar `exceptModuleId`.
    QString declaredByRunningModule(const Shortcut &shortcut, const QString &exceptModuleId) const;
    QString declaredByOffModule(const Shortcut &shortcut, const QString &exceptModuleId) const;

    // Lo que usa ModuleHotkeysImpl.
    bool systemRegister(int globalId, const Shortcut &shortcut);
    void systemUnregister(int globalId);
    bool systemProbe(const Shortcut &shortcut);
    void systemPassThrough(const Shortcut &shortcut);

private:
    void onActivated(int globalId);

    bool m_automated = false;
    OffModuleLookup m_offLookup;
    HotkeyService *m_service = nullptr;
    QList<ModuleHotkeysImpl *> m_clients;
};

// Los atajos de UN modulo: ids locales (1..99) sobre el rango global del modulo.
class ModuleHotkeysImpl : public ModuleHotkeys
{
    Q_OBJECT

public:
    static constexpr int kRangeSize = 100;

    ModuleHotkeysImpl(HotkeyHub *hub, const QString &moduleId, const QString &moduleTitle, int index);
    ~ModuleHotkeysImpl() override;

    bool registerHotkey(int localId, const Shortcut &shortcut) override;
    void unregisterHotkey(int localId) override;
    void unregisterAll() override;
    bool isRegistered(int localId) const override;
    bool probe(const Shortcut &shortcut) override;
    void passThrough(const Shortcut &shortcut) override;
    QString declare(int localId, const Shortcut &shortcut) override;
    QString declaredByOtherModule(const Shortcut &shortcut) const override;

    QString moduleId() const { return m_moduleId; }
    QString moduleTitle() const { return m_moduleTitle; }
    int registeredCount() const { return m_registered.size(); }
    int declaredCount() const { return m_declared.size(); }
    bool hasDeclared(const Shortcut &shortcut) const;
    // Soltar todo sin avisar al hub (el hub se esta cerrando).
    void detachFromHub() { m_hub = nullptr; }
    bool ownsGlobalId(int globalId) const;
    void fire(int globalId);

private:
    int globalId(int localId) const { return (m_index + 1) * kRangeSize + localId; }

    HotkeyHub *m_hub = nullptr;
    QString m_moduleId;
    QString m_moduleTitle;
    int m_index = 0;
    QHash<int, Shortcut> m_registered;
    QHash<int, Shortcut> m_declared;
};

#endif // MIGHTYTOOLS_HOTKEYHUB_H
