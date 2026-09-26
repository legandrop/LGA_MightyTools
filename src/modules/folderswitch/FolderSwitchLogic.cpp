#include "modules/folderswitch/FolderSwitchLogic.h"

#include <QDebug>

namespace FolderSwitchLogic {

bool isManagerFresh(qint64 lastSeenMs, qint64 nowMs)
{
    return (nowMs - lastSeenMs) < kManagerFreshnessMs;
}

bool shouldAutoSwitch(bool autoSwitchOn, bool masterOn, bool managerFresh, bool isPendingReturn,
                      bool alreadySwitchedThisDialog)
{
    if (alreadySwitchedThisDialog) {
        return false;
    }
    return autoSwitchOn && masterOn && managerFresh && isPendingReturn;
}

void logCallTiming(const char *operation, qint64 elapsedMs)
{
    qDebug().noquote() << QStringLiteral("[folderSwitch] %1 %2 ms").arg(QLatin1String(operation)).arg(elapsedMs);
    if (elapsedMs > kSlowCallMs) {
        qWarning().noquote() << QStringLiteral("[folderSwitch] %1 tardo %2 ms (> %3 ms): puede trabar toda la app "
                                               "mientras corre, comparte proceso con las demas herramientas")
                                     .arg(QLatin1String(operation))
                                     .arg(elapsedMs)
                                     .arg(kSlowCallMs);
    }
}

QtDialogCache::QtDialogCache(qint64 ttlMs) : m_ttlMs(ttlMs)
{
}

bool QtDialogCache::lookup(quintptr key, qint64 now, bool *outValue) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.constEnd()) {
        return false;
    }
    if (now - it.value().timestampMs >= m_ttlMs) {
        return false; // vencio: quien llama tiene que recalcular y volver a guardar con store()
    }
    if (outValue) {
        *outValue = it.value().value;
    }
    return true;
}

void QtDialogCache::store(quintptr key, bool value, qint64 now)
{
    // Cota de tamano, igual de simple que el cache anterior (QHash<HWND,bool> con limite de 64): con
    // el TTL de por medio esto casi nunca se alcanza, pero evita crecer sin fin en una sesion larga
    // con muchas ventanas distintas.
    if (m_entries.size() > 128) {
        m_entries.clear();
    }
    m_entries.insert(key, Entry{value, now});
}

void QtDialogCache::invalidate(quintptr key)
{
    m_entries.remove(key);
}

void QtDialogCache::clear()
{
    m_entries.clear();
}

} // namespace FolderSwitchLogic
