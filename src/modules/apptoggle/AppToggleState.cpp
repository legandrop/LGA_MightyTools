#include "modules/apptoggle/AppToggleState.h"

#include "app/ModuleContext.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
const QString kApps = QStringLiteral("apps");
constexpr int kMaxText = 2048;
} // namespace

AppToggleState::AppToggleState(ModuleContext *context)
    : m_context(context)
{
    if (context) {
        m_apps = fromJson(context->value(kApps).toString());
    }
}

bool AppToggleState::validPath(const QString &path)
{
    const QString clean = QDir::fromNativeSeparators(path.trimmed());
#ifdef Q_OS_MACOS
    const QString suffix = QStringLiteral(".app");
#else
    const QString suffix = QStringLiteral(".exe");
#endif
    return clean.size() <= kMaxText && QDir::isAbsolutePath(clean)
        && clean.endsWith(suffix, Qt::CaseInsensitive) && clean.size() > suffix.size() + 1;
}

bool AppToggleState::valid(const AppEntry &entry)
{
    return entry.id >= 1 && entry.id <= kMaxApps && !entry.name.trimmed().isEmpty() && entry.name.size() <= 80
        && validPath(entry.path) && entry.arguments.size() <= kMaxText && entry.workingDir.size() <= kMaxText
        && entry.shortcut.isValid();
}

bool AppToggleState::upsert(AppEntry &entry)
{
    int position = -1;
    if (entry.id != 0) {
        for (int i = 0; i < m_apps.size(); ++i) {
            if (m_apps.at(i).id == entry.id) position = i;
        }
        if (position < 0) return false;
    } else {
        if (m_apps.size() >= kMaxApps) return false;
        QSet<int> used;
        for (const AppEntry &stored : m_apps) used.insert(stored.id);
        for (int id = 1; id <= kMaxApps && !entry.id; ++id) {
            if (!used.contains(id)) entry.id = id;
        }
    }
    entry.name = entry.name.trimmed();
    entry.path = entry.path.trimmed();
    entry.arguments = entry.arguments.trimmed();
    entry.workingDir = entry.workingDir.trimmed();
    if (!valid(entry)) {
        if (position < 0) entry.id = 0;
        return false;
    }
    for (const AppEntry &stored : m_apps) {
        if (stored.id != entry.id && stored.shortcut == entry.shortcut) {
            if (position < 0) entry.id = 0;
            return false;
        }
    }
    if (position >= 0) {
        m_apps[position] = entry;
    } else {
        m_apps.append(entry);
    }
    save();
    return true;
}

bool AppToggleState::remove(int id)
{
    for (int i = 0; i < m_apps.size(); ++i) {
        if (m_apps.at(i).id == id) {
            m_apps.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

void AppToggleState::setCaptureApps(const QList<AppEntry> &apps)
{
    if (!m_context || m_context->captureMode()) {
        m_apps = apps;
    }
}

QList<AppEntry> AppToggleState::fromJson(const QString &text)
{
    QList<AppEntry> result;
    if (text.size() > 262144) {
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) {
        return result;
    }
    QSet<int> seen;
    QSet<QString> shortcuts;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        AppEntry entry;
        entry.id = object.value(QStringLiteral("id")).toInt();
        entry.name = object.value(QStringLiteral("name")).toString().trimmed();
        entry.path = object.value(QStringLiteral("path")).toString().trimmed();
        entry.arguments = object.value(QStringLiteral("arguments")).toString().trimmed();
        entry.workingDir = object.value(QStringLiteral("workingDir")).toString().trimmed();
        entry.shortcut = Shortcut::fromPortableString(object.value(QStringLiteral("shortcut")).toString());
        entry.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        const QString shortcut = entry.shortcut.toPortableString();
        if (!valid(entry) || seen.contains(entry.id) || shortcuts.contains(shortcut)) {
            continue;
        }
        result.append(entry);
        seen.insert(entry.id);
        shortcuts.insert(shortcut);
        if (result.size() == kMaxApps) break;
    }
    return result;
}

QString AppToggleState::toJson(const QList<AppEntry> &apps)
{
    QJsonArray array;
    for (const AppEntry &entry : apps) {
        if (!valid(entry)) continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), entry.id);
        object.insert(QStringLiteral("name"), entry.name);
        object.insert(QStringLiteral("path"), entry.path);
        if (!entry.arguments.isEmpty()) object.insert(QStringLiteral("arguments"), entry.arguments);
        if (!entry.workingDir.isEmpty()) object.insert(QStringLiteral("workingDir"), entry.workingDir);
        object.insert(QStringLiteral("shortcut"), entry.shortcut.toPortableString());
        if (!entry.enabled) object.insert(QStringLiteral("enabled"), false);
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void AppToggleState::save()
{
    if (m_context) {
        m_context->setValue(kApps, toJson(m_apps));
    }
}
