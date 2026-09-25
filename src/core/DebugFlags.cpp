#include "core/DebugFlags.h"
#include "core/AppPaths.h"

#include <QFile>
#include <QHash>
#include <QTextStream>

namespace {

QHash<QString, bool> load()
{
    QHash<QString, bool> flags;
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
        const QString value = line.mid(eq + 1).trimmed().toLower();
        flags.insert(line.left(eq).trimmed(),
                     value == QLatin1String("true") || value == QLatin1String("1") || value == QLatin1String("yes"));
    }
    return flags;
}

} // namespace

namespace DebugFlags {

bool isOn(const QString &name)
{
    static const QHash<QString, bool> flags = load();
    return flags.value(name, false);
}

} // namespace DebugFlags
