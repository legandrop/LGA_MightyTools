#include "modules/folderswitch/FolderSwitchState.h"

#include "app/ModuleContext.h"

#include <utility>

namespace {
const QString kKeyEnabled = QStringLiteral("enabled");
const QString kKeyAutoSwitch = QStringLiteral("autoSwitch");
const QString kKeyManualShortcut = QStringLiteral("shortcuts/manual");
const QString kKeyRecentShortcut = QStringLiteral("shortcuts/recent");
const QString kKeyRecentFolders = QStringLiteral("recentFolders");
} // namespace

Shortcut FolderSwitchState::defaultManualShortcut()
{
    Shortcut s;
    s.modifiers = Qt::ControlModifier | Qt::AltModifier;
    s.key = Qt::Key_O;
    return s;
}

Shortcut FolderSwitchState::defaultRecentShortcut()
{
    Shortcut s;
    s.modifiers = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier;
    s.key = Qt::Key_O;
    return s;
}

FolderSwitchState::FolderSwitchState(ModuleContext *context)
    : m_context(context)
{
    m_manualShortcut = defaultManualShortcut();
    m_recentShortcut = defaultRecentShortcut();

    if (!m_context) {
        return;
    }

    m_enabled = m_context->value(kKeyEnabled, true).toBool();
    m_autoSwitch = m_context->value(kKeyAutoSwitch, true).toBool();

    const Shortcut manual = Shortcut::fromPortableString(
        m_context->value(kKeyManualShortcut, defaultManualShortcut().toPortableString()).toString());
    if (manual.isValid()) {
        m_manualShortcut = manual;
    }
    const Shortcut recent = Shortcut::fromPortableString(
        m_context->value(kKeyRecentShortcut, defaultRecentShortcut().toPortableString()).toString());
    if (recent.isValid()) {
        m_recentShortcut = recent;
    }

    m_recentFolders = m_context->value(kKeyRecentFolders).toStringList().mid(0, kMaxRecentFolders);
}

void FolderSwitchState::setEnabled(bool enabled)
{
    if (enabled == m_enabled) {
        return;
    }
    m_enabled = enabled;
    if (m_context) {
        m_context->setValue(kKeyEnabled, enabled);
    }
}

void FolderSwitchState::setAutoSwitch(bool autoSwitch)
{
    if (autoSwitch == m_autoSwitch) {
        return;
    }
    m_autoSwitch = autoSwitch;
    if (m_context) {
        m_context->setValue(kKeyAutoSwitch, autoSwitch);
    }
}

void FolderSwitchState::setManualShortcut(const Shortcut &shortcut)
{
    m_manualShortcut = shortcut;
    if (m_context) {
        m_context->setValue(kKeyManualShortcut, shortcut.toPortableString());
    }
}

void FolderSwitchState::setRecentShortcut(const Shortcut &shortcut)
{
    m_recentShortcut = shortcut;
    if (m_context) {
        m_context->setValue(kKeyRecentShortcut, shortcut.toPortableString());
    }
}

bool FolderSwitchState::sameFolder(const QString &a, const QString &b)
{
    const auto strip = [](QString p) {
        while (p.size() > 3 && (p.endsWith(QLatin1Char('\\')) || p.endsWith(QLatin1Char('/')))) {
            p.chop(1);
        }
        return p;
    };
    return strip(a).compare(strip(b), Qt::CaseInsensitive) == 0;
}

void FolderSwitchState::addRecentFolder(const QString &path)
{
    const QString folder = path.trimmed();
    if (folder.isEmpty()) {
        return;
    }
    QStringList updated{folder};
    for (const QString &existing : std::as_const(m_recentFolders)) {
        if (!sameFolder(existing, folder) && updated.size() < kMaxRecentFolders) {
            updated.append(existing);
        }
    }
    if (updated == m_recentFolders) {
        return;
    }
    m_recentFolders = updated;
    if (m_context) {
        m_context->setValue(kKeyRecentFolders, m_recentFolders);
    }
}
