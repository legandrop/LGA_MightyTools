#include "app/HotkeyHub.h"

#include "platform/HotkeyService.h"

#include <QDebug>

// ------------------------------------------------------------------ HotkeyHub

HotkeyHub::HotkeyHub(bool automatedRun, OffModuleLookup offLookup, QObject *parent)
    : QObject(parent)
    , m_automated(automatedRun)
    , m_offLookup(std::move(offLookup))
{
}

HotkeyHub::~HotkeyHub()
{
    // Los clientes los borran sus contextos; si alguno sigue vivo, que no vuelva a llamar aca.
    for (ModuleHotkeysImpl *client : m_clients) {
        client->detachFromHub();
    }
    delete m_service;
}

ModuleHotkeysImpl *HotkeyHub::acquire(const QString &moduleId, const QString &moduleTitle, int index)
{
    if (!m_automated && !m_service) {
        m_service = new HotkeyService(this);
        connect(m_service, &HotkeyService::activated, this, &HotkeyHub::onActivated);
        qInfo() << "[HotkeyHub] Servicio de atajos creado por" << moduleId;
    }
    auto *client = new ModuleHotkeysImpl(this, moduleId, moduleTitle, index);
    m_clients.append(client);
    return client;
}

void HotkeyHub::release(ModuleHotkeysImpl *hotkeys)
{
    m_clients.removeAll(hotkeys);
    if (m_clients.isEmpty() && m_service) {
        m_service->unregisterAll();
        delete m_service;
        m_service = nullptr;
        qInfo() << "[HotkeyHub] Ningun modulo usa atajos: servicio destruido";
    }
}

int HotkeyHub::registeredCount() const
{
    int count = 0;
    for (const ModuleHotkeysImpl *client : m_clients) {
        count += client->registeredCount();
    }
    return count;
}

int HotkeyHub::declaredCount() const
{
    int count = 0;
    for (const ModuleHotkeysImpl *client : m_clients) {
        count += client->declaredCount();
    }
    return count;
}

QString HotkeyHub::declaredByRunningModule(const Shortcut &shortcut, const QString &exceptModuleId) const
{
    for (const ModuleHotkeysImpl *client : m_clients) {
        if (client->moduleId() != exceptModuleId && client->hasDeclared(shortcut)) {
            return client->moduleTitle();
        }
    }
    return QString();
}

QString HotkeyHub::declaredByOffModule(const Shortcut &shortcut, const QString &exceptModuleId) const
{
    return m_offLookup ? m_offLookup(shortcut, exceptModuleId) : QString();
}

bool HotkeyHub::systemRegister(int globalId, const Shortcut &shortcut)
{
    if (m_automated) {
        qDebug() << "[HotkeyHub] (automatizada) registro contado sin llamar al sistema:" << shortcut.toPortableString()
                 << "id" << globalId;
        return true;
    }
    return m_service && m_service->registerHotkey(globalId, shortcut);
}

void HotkeyHub::systemUnregister(int globalId)
{
    if (!m_automated && m_service) {
        m_service->unregisterHotkey(globalId);
    }
}

bool HotkeyHub::systemProbe(const Shortcut &shortcut)
{
    if (m_automated) {
        return true;
    }
    return m_service && m_service->probe(shortcut);
}

void HotkeyHub::systemPassThrough(const Shortcut &shortcut)
{
    if (m_automated) {
        qInfo() << "[HotkeyHub] (automatizada) se devolveria" << shortcut.toPortableString() << "a la app del frente";
        return;
    }
    if (m_service) {
        m_service->passThrough(shortcut);
    }
}

void HotkeyHub::onActivated(int globalId)
{
    for (ModuleHotkeysImpl *client : m_clients) {
        if (client->ownsGlobalId(globalId)) {
            client->fire(globalId);
            return;
        }
    }
}

// ------------------------------------------------------------------ ModuleHotkeysImpl

ModuleHotkeysImpl::ModuleHotkeysImpl(HotkeyHub *hub, const QString &moduleId, const QString &moduleTitle, int index)
    : m_hub(hub)
    , m_moduleId(moduleId)
    , m_moduleTitle(moduleTitle)
    , m_index(index)
{
}

ModuleHotkeysImpl::~ModuleHotkeysImpl()
{
    unregisterAll();
    m_declared.clear();
    if (m_hub) {
        m_hub->release(this);
    }
}

bool ModuleHotkeysImpl::registerHotkey(int localId, const Shortcut &shortcut)
{
    if (localId <= 0 || localId >= kRangeSize || !m_hub) {
        return false;
    }
    unregisterHotkey(localId);
    // Una combinacion declarada por otra herramienta prendida no se registra: gana la que declaro
    // primero, sin preguntarle al sistema.
    const QString other = m_hub->declaredByRunningModule(shortcut, m_moduleId);
    if (!other.isEmpty()) {
        qInfo() << "[HotkeyHub]" << m_moduleId << ":" << shortcut.toPortableString() << "ya es de" << other;
        return false;
    }
    if (!m_hub->systemRegister(globalId(localId), shortcut)) {
        return false;
    }
    m_registered.insert(localId, shortcut);
    return true;
}

void ModuleHotkeysImpl::unregisterHotkey(int localId)
{
    if (!m_registered.contains(localId)) {
        return;
    }
    if (m_hub) {
        m_hub->systemUnregister(globalId(localId));
    }
    m_registered.remove(localId);
}

void ModuleHotkeysImpl::unregisterAll()
{
    const QList<int> ids = m_registered.keys();
    for (const int id : ids) {
        unregisterHotkey(id);
    }
}

bool ModuleHotkeysImpl::isRegistered(int localId) const
{
    return m_registered.contains(localId);
}

bool ModuleHotkeysImpl::probe(const Shortcut &shortcut)
{
    return m_hub && m_hub->systemProbe(shortcut);
}

void ModuleHotkeysImpl::passThrough(const Shortcut &shortcut)
{
    if (m_hub) {
        m_hub->systemPassThrough(shortcut);
    }
}

QString ModuleHotkeysImpl::declare(int localId, const Shortcut &shortcut)
{
    m_declared.remove(localId);
    if (!shortcut.isValid() || !m_hub) {
        return QString();
    }
    // Compite solo contra las prendidas: prender una herramienta nunca la deja sin atajo por una
    // apagada.
    const QString other = m_hub->declaredByRunningModule(shortcut, m_moduleId);
    if (!other.isEmpty()) {
        return other;
    }
    m_declared.insert(localId, shortcut);
    return QString();
}

QString ModuleHotkeysImpl::declaredByOtherModule(const Shortcut &shortcut) const
{
    if (!m_hub) {
        return QString();
    }
    const QString running = m_hub->declaredByRunningModule(shortcut, m_moduleId);
    return running.isEmpty() ? m_hub->declaredByOffModule(shortcut, m_moduleId) : running;
}

bool ModuleHotkeysImpl::hasDeclared(const Shortcut &shortcut) const
{
    for (auto it = m_declared.constBegin(); it != m_declared.constEnd(); ++it) {
        if (it.value() == shortcut) {
            return true;
        }
    }
    return false;
}

bool ModuleHotkeysImpl::ownsGlobalId(int id) const
{
    return id > (m_index + 1) * kRangeSize && id < (m_index + 2) * kRangeSize;
}

void ModuleHotkeysImpl::fire(int id)
{
    const int localId = id - (m_index + 1) * kRangeSize;
    if (m_registered.contains(localId)) {
        emit activated(localId);
    }
}
