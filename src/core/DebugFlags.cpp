#include "core/DebugFlags.h"
#include "core/AppPaths.h"

#include <QFile>
#include <QHash>
#include <QTextStream>

namespace {

QHash<QString, QString> load()
{
    QHash<QString, QString> flags;
    QFile file(AppPaths::configFile(QStringLiteral("debug_flags.txt")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return flags;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        flags.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return flags;
}

const QHash<QString, QString> &flags()
{
    static const QHash<QString, QString> loaded = load();
    return loaded;
}

} // namespace

namespace DebugFlags {

bool isOn(const QString &name)
{
    const QString value = flags().value(name).toLower();
    return value == QLatin1String("true") || value == QLatin1String("1") || value == QLatin1String("yes");
}

QString value(const QString &name)
{
    return flags().value(name);
}

} // namespace DebugFlags
