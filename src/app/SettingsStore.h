#ifndef MIGHTYTOOLS_SETTINGSSTORE_H
#define MIGHTYTOOLS_SETTINGSSTORE_H

#include <QHash>
#include <QString>
#include <QVariant>

#include <memory>

// Donde leen y escriben el host y los modulos. Claves con barra: "modules/nukeShortcuts/enabled",
// "nukeShortcuts/shortcuts/addKeyframe" (la primera parte es la seccion [<id>] del .ini).
//
// Dos implementaciones:
//  - FileSettingsStore: settings.ini de AppSettings (la app de verdad).
//  - MemorySettingsStore: un mapa en memoria. Captura, self-test y medicion: nunca tocan el disco.
class SettingsStore
{
public:
    virtual ~SettingsStore() = default;

    virtual QVariant value(const QString &key, const QVariant &defaultValue = {}) const = 0;
    virtual void setValue(const QString &key, const QVariant &value) = 0;
    // Borra la clave y todo lo que cuelga de ella ("diskSpace/watched" borra el array entero).
    virtual void remove(const QString &key) = 0;
    virtual bool persistent() const = 0;
};

class FileSettingsStore : public SettingsStore
{
public:
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const override;
    void setValue(const QString &key, const QVariant &value) override;
    void remove(const QString &key) override;
    bool persistent() const override { return true; }
};

class MemorySettingsStore : public SettingsStore
{
public:
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const override;
    void setValue(const QString &key, const QVariant &value) override;
    void remove(const QString &key) override;
    bool persistent() const override { return false; }

private:
    QHash<QString, QVariant> m_values;
};

#endif // MIGHTYTOOLS_SETTINGSSTORE_H
