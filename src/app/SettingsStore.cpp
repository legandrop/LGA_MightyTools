#include "app/SettingsStore.h"

#include "core/AppSettings.h"

#include <QDebug>

QVariant FileSettingsStore::value(const QString &key, const QVariant &defaultValue) const
{
    return AppSettings::open()->value(key, defaultValue);
}

void FileSettingsStore::setValue(const QString &key, const QVariant &value)
{
    const auto settings = AppSettings::openForWrite();
    settings->setValue(key, value);
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        qWarning() << "[Settings] No se pudo guardar" << key << "en" << AppSettings::filePath();
    }
}

void FileSettingsStore::remove(const QString &key)
{
    const auto settings = AppSettings::openForWrite();
    settings->remove(key);
    settings->sync();
}

QVariant MemorySettingsStore::value(const QString &key, const QVariant &defaultValue) const
{
    return m_values.value(key, defaultValue);
}

void MemorySettingsStore::setValue(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
}

void MemorySettingsStore::remove(const QString &key)
{
    const QString prefix = key + QLatin1Char('/');
    for (auto it = m_values.begin(); it != m_values.end();) {
        if (it.key() == key || it.key().startsWith(prefix)) {
            it = m_values.erase(it);
        } else {
            ++it;
        }
    }
}
