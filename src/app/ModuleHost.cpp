#include "app/ModuleHost.h"

#include "app/HotkeyHub.h"
#include "app/ModuleContextImpl.h"
#include "app/SettingsStore.h"
#include "platform/ForegroundWatcher.h"
#include "platform/InputInjector.h"

#include <QDebug>
#include <QWidget>

namespace {

QString enabledKey(const QString &id)
{
    return QStringLiteral("modules/%1/enabled").arg(id);
}

} // namespace

ModuleHost::ModuleHost(QList<ModuleDescriptor> descriptors, SettingsStore *store, const HostOptions &options,
                       QObject *parent)
    : QObject(parent)
    , m_descriptors(std::move(descriptors))
    , m_store(store)
    , m_options(options)
{
    if (m_options.captureMode) {
        m_options.automatedRun = true;
    }
    // Los atajos configurados de las herramientas apagadas cuentan para el grabador de las demas
    // (ModuleHotkeys::declaredByOtherModule).
    m_hotkeys = new HotkeyHub(
        m_options.automatedRun,
        [this](const Shortcut &shortcut, const QString &exceptModuleId) -> QString {
            for (const ModuleDescriptor &d : m_descriptors) {
                if (d.id == exceptModuleId || isRunning(d.id) || !d.configuredShortcuts) {
                    continue;
                }
                if (d.configuredShortcuts(reader(d.id)).contains(shortcut)) {
                    return d.title;
                }
            }
            return QString();
        },
        this);
}

ModuleHost::~ModuleHost()
{
    shutdown();
    // Los apagados que esperaban su deleteLater: con el host cerrandose, en el acto (sus contextos
    // se borran por destroyed(), que todavia llega a este objeto).
    for (const QPointer<Module> &pending : m_pendingDelete) {
        delete pending.data();
    }
    m_pendingDelete.clear();
}

const ModuleDescriptor *ModuleHost::descriptor(const QString &id) const
{
    for (const ModuleDescriptor &d : m_descriptors) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

int ModuleHost::indexOf(const QString &id) const
{
    for (int i = 0; i < m_descriptors.size(); ++i) {
        if (m_descriptors.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

bool ModuleHost::isEnabled(const QString &id) const
{
    if (m_options.captureMode) {
        return isRunning(id);
    }
    return m_store->value(enabledKey(id), false).toBool();
}

bool ModuleHost::isRunning(const QString &id) const
{
    const auto it = m_slots.find(id);
    return it != m_slots.end() && it->second.module;
}

Module *ModuleHost::module(const QString &id) const
{
    const auto it = m_slots.find(id);
    return it != m_slots.end() ? it->second.module.get() : nullptr;
}

SettingsReader ModuleHost::reader(const QString &id) const
{
    SettingsStore *store = m_store;
    return [store, id](const QString &key, const QVariant &defaultValue) {
        return store->value(id + QLatin1Char('/') + key, defaultValue);
    };
}

void ModuleHost::setEnabled(const QString &id, bool enabled)
{
    if (!descriptor(id)) {
        qWarning() << "[ModuleHost] Herramienta desconocida:" << id;
        return;
    }
    if (!m_options.captureMode) {
        m_store->setValue(enabledKey(id), enabled);
    }
    if (enabled && !isRunning(id)) {
        build(id, !m_options.captureMode);
    } else if (!enabled && isRunning(id)) {
        destroy(id, false);
    }
}

void ModuleHost::startEnabled()
{
    for (const ModuleDescriptor &d : m_descriptors) {
        if (isEnabled(d.id) && !isRunning(d.id)) {
            build(d.id, true);
        }
    }
    qInfo() << "[ModuleHost] Herramientas prendidas al arrancar:" << runningIds();
}

void ModuleHost::shutdown()
{
    const QStringList ids = runningIds();
    for (const QString &id : ids) {
        destroy(id, true);
    }
}

bool ModuleHost::build(const QString &id, bool start)
{
    const ModuleDescriptor *d = descriptor(id);
    if (!d || !d->create) {
        return false;
    }
    Slot &slot = m_slots[id];
    slot.context = new ModuleContextImpl(this, d->id, d->title, indexOf(id));
    slot.module = d->create(*slot.context);
    if (!slot.module) {
        qWarning() << "[ModuleHost] La fabrica de" << id << "no devolvio un modulo";
        delete slot.context;
        m_slots.erase(id);
        return false;
    }
    Module *module = slot.module.get();
    ModuleContextImpl *context = slot.context;
    // El contexto sigue vivo mientras corre el destructor del modulo: se borra recien con destroyed().
    connect(module, &QObject::destroyed, this, [this, context, id]() {
        delete context;
        m_pendingDelete.removeAll(QPointer<Module>());
        emit moduleDestroyed(id);
    });
    connect(module, &Module::statusChanged, this, [this, id]() { emit moduleStatusChanged(id); });
    if (start) {
        module->start();
    }
    qInfo() << "[ModuleHost]" << id << (start ? "prendida" : "construida para captura");
    emit moduleToggled(id, true);
    return true;
}

void ModuleHost::destroy(const QString &id, bool immediate)
{
    auto it = m_slots.find(id);
    if (it == m_slots.end() || !it->second.module) {
        return;
    }
    Module *module = it->second.module.release();
    QPointer<QWidget> panel = it->second.panel;
    m_slots.erase(it);

    if (!m_options.captureMode) {
        module->stop();
    }
    if (panel) {
        panel->hide();
        if (immediate) {
            delete panel.data();
        } else {
            panel->deleteLater();
        }
    }
    if (immediate) {
        delete module;
    } else {
        m_pendingDelete.append(QPointer<Module>(module));
        module->deleteLater();
    }
    qInfo() << "[ModuleHost]" << id << "apagada";
    emit moduleToggled(id, false);
}

QWidget *ModuleHost::panel(const QString &id, QWidget *parent)
{
    auto it = m_slots.find(id);
    if (it == m_slots.end() || !it->second.module) {
        return nullptr;
    }
    if (!it->second.panel) {
        it->second.panel = it->second.module->createPanel(parent);
    }
    return it->second.panel.data();
}

QWidget *ModuleHost::existingPanel(const QString &id) const
{
    const auto it = m_slots.find(id);
    return it != m_slots.end() ? it->second.panel.data() : nullptr;
}

ModuleStatus ModuleHost::status(const QString &id) const
{
    if (Module *m = module(id)) {
        return m->status();
    }
    ModuleStatus off;
    off.tone = ModuleTone::Off;
    off.text = QStringLiteral("Off");
    return off;
}

int ModuleHost::runningCount() const
{
    int count = 0;
    for (const ModuleDescriptor &d : m_descriptors) {
        if (isRunning(d.id)) {
            ++count;
        }
    }
    return count;
}

QStringList ModuleHost::runningIds() const
{
    QStringList ids;
    for (const ModuleDescriptor &d : m_descriptors) {
        if (isRunning(d.id)) {
            ids.append(d.id);
        }
    }
    return ids;
}

bool ModuleHost::allPausedOrOff() const
{
    for (const QString &id : runningIds()) {
        if (!module(id)->isPaused()) {
            return false;
        }
    }
    return true;
}

QStringList ModuleHost::trayTooltipLines() const
{
    QStringList lines;
    for (const QString &id : runningIds()) {
        lines += module(id)->trayTooltipLines();
    }
    return lines;
}

InputInjector *ModuleHost::acquireInjector()
{
    if (!m_injector) {
        const bool dryRun = m_options.automatedRun || m_options.dryRunInput;
        m_injector = std::make_unique<InputInjector>(dryRun);
        if (dryRun) {
            qInfo() << "[ModuleHost] Inyector en solo loguear: no toca el mouse ni el teclado";
        }
    }
    ++m_injectorUsers;
    return m_injector.get();
}

void ModuleHost::releaseInjector()
{
    if (m_injectorUsers > 0 && --m_injectorUsers == 0) {
        m_injector.reset();
    }
}

ForegroundWatcher *ModuleHost::acquireForeground()
{
    if (!m_foreground) {
        // Inerte (sin hook real) en corrida automatizada, igual que el inyector en dry-run; salvo que
        // la corrida pida observar (medicion, conteo de hooks): observar no actua sobre nada.
        m_foreground = std::make_unique<ForegroundWatcher>(!m_options.automatedRun || m_options.observeForeground);
    }
    ++m_foregroundUsers;
    return m_foreground.get();
}

void ModuleHost::releaseForeground()
{
    if (m_foregroundUsers > 0 && --m_foregroundUsers == 0) {
        m_foreground.reset();
    }
}

bool ModuleHost::enableForCapture(const QString &id, const QString &state)
{
    if (!m_options.captureMode || !descriptor(id)) {
        return false;
    }
    if (!isRunning(id) && !build(id, false)) {
        return false;
    }
    if (state.isEmpty()) {
        return true;
    }
    return module(id)->applyCaptureState(state);
}
