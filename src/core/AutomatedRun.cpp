#include "core/AutomatedRun.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>

#include <atomic>

namespace {

std::atomic<bool> g_active{false};
QMutex g_mutex;
QString g_sandbox; // nativa, sin separador final

// Windows y macOS (APFS y HFS+ por defecto) no distinguen mayusculas en las rutas. En un volumen de mac
// que si las distingue, comparar sin distinguir solo hace las guardas mas amplias.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

QString normalized(const QString &path)
{
    QString text = QDir::toNativeSeparators(QDir::cleanPath(path));
    if (text.startsWith(QLatin1String("\\\\?\\"))) {
        text.remove(0, 4);
    }
    while (text.size() > 1 && text.endsWith(QDir::separator())) {
        text.chop(1);
    }
    return text;
}

} // namespace

namespace AutomatedRun {

void enable()
{
    g_active.store(true);
}

bool active()
{
    return g_active.load();
}

void setSandbox(const QString &canonicalDir)
{
    QMutexLocker locker(&g_mutex);
    g_sandbox = canonicalDir.isEmpty() ? QString() : normalized(canonicalDir);
}

QString sandbox()
{
    QMutexLocker locker(&g_mutex);
    return g_sandbox;
}

bool mayModify(const QString &path)
{
    if (!g_active.load()) {
        return true;
    }
    const QString root = sandbox();
    if (root.isEmpty()) {
        return false;
    }
    const QString target = normalized(path);
    // Estrictamente adentro: con el separador, para que "sandbox2" no cuente como "sandbox".
    return target.size() > root.size() + 1 && target.startsWith(root, kPathCase) && target.at(root.size()) == QDir::separator();
}

} // namespace AutomatedRun
