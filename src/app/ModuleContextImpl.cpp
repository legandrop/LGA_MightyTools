#include "app/ModuleContextImpl.h"

#include "app/HostServices.h"
#include "app/HotkeyHub.h"
#include "app/ModuleHost.h"
#include "app/SettingsStore.h"

#include <QDebug>

ModuleContextImpl::ModuleContextImpl(ModuleHost *host, const QString &moduleId, const QString &moduleTitle, int index)
    : m_host(host)
    , m_id(moduleId)
    , m_title(moduleTitle)
    , m_index(index)
{
}

ModuleContextImpl::~ModuleContextImpl()
{
    // Red de seguridad: lo que el modulo no solto en stop() o en su destructor.
    if (m_hotkeys) {
        m_hotkeys->unregisterAll();
        delete m_hotkeys;
        m_hotkeys = nullptr;
    }
    if (m_injector) {
        m_injector = nullptr;
        m_host->releaseInjector();
    }
    if (m_windowHidden) {
        restoreWindow();
    }
}

QString ModuleContextImpl::key(const QString &key) const
{
    return m_id + QLatin1Char('/') + key;
}

QVariant ModuleContextImpl::value(const QString &k, const QVariant &defaultValue) const
{
    return m_host->store()->value(key(k), defaultValue);
}

void ModuleContextImpl::setValue(const QString &k, const QVariant &value)
{
    m_host->store()->setValue(key(k), value);
}

void ModuleContextImpl::removeValue(const QString &k)
{
    m_host->store()->remove(key(k));
}

bool ModuleContextImpl::captureMode() const
{
    return m_host->options().captureMode;
}

bool ModuleContextImpl::dryRunInput() const
{
    return m_host->options().dryRunInput || m_host->options().automatedRun;
}

bool ModuleContextImpl::automatedRun() const
{
    return m_host->options().automatedRun || m_host->options().captureMode;
}

bool ModuleContextImpl::persistentRegistrationAllowed() const
{
    return persistentAllowed(automatedRun(), m_host->options().buildTree);
}

ModuleHotkeys *ModuleContextImpl::hotkeys()
{
    if (!m_hotkeys) {
        m_hotkeys = m_host->hotkeyHub()->acquire(m_id, m_title, m_index);
    }
    return m_hotkeys;
}

InputInjector *ModuleContextImpl::injector()
{
    if (!m_injector) {
        m_injector = m_host->acquireInjector();
    }
    return m_injector;
}

void ModuleContextImpl::notify(const QString &title, const QString &body, NoticeIcon icon, int msecs)
{
    HostServices *services = m_host->hostServices();
    if (automatedRun() || !services) {
        qInfo().noquote() << QStringLiteral("[Notifier] (sin mostrar) %1: %2 | %3").arg(m_id, title, body);
        return;
    }
    services->notify(m_id, title, body, icon, msecs);
}

void ModuleContextImpl::showPanel()
{
    if (HostServices *services = m_host->hostServices()) {
        services->showPanel(m_id);
    }
}

void ModuleContextImpl::hideWindowTemporarily()
{
    HostServices *services = m_host->hostServices();
    if (!services || m_windowHidden) {
        return;
    }
    m_windowWasVisible = services->hideWindow();
    m_windowHidden = true;
}

void ModuleContextImpl::restoreWindow()
{
    HostServices *services = m_host->hostServices();
    if (!m_windowHidden) {
        return;
    }
    m_windowHidden = false;
    if (services && m_windowWasVisible) {
        services->showWindow();
    }
}

QWidget *ModuleContextImpl::window() const
{
    HostServices *services = m_host->hostServices();
    return services ? services->window() : nullptr;
}
