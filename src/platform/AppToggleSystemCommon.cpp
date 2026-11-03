#include "platform/AppToggleSystem.h"

#include <QDir>

namespace AppToggleSystem {

QString normalizedPath(const QString &path)
{
    QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    while (clean.size() > 1 && clean.endsWith(QLatin1Char('/'))) clean.chop(1);
#ifdef Q_OS_WIN
    return clean.toLower();
#else
    return clean;
#endif
}

bool samePath(const QString &a, const QString &b)
{
    const QString left = normalizedPath(a);
    return !left.isEmpty() && left == normalizedPath(b);
}

bool eligible(const Window &window)
{
    return (window.visible || window.minimized) && !window.cloaked
        && (window.appWindow || (!window.owned && !window.toolWindow));
}

Decision decide(const QString &appPath, const Snapshot &snapshot)
{
    Decision decision;
    bool running = false;
    for (const QString &process : snapshot.processPaths) running = running || samePath(process, appPath);
    if (!running) {
        decision.action = Action::Launch;
        decision.reason = QStringLiteral("no hay un proceso con esa ruta");
        return decision;
    }
    const bool inFront = samePath(snapshot.foregroundPath, appPath);
    if (snapshot.wholeApp) {
        decision.action = inFront ? Action::Minimize : Action::Activate;
        decision.reason = inFront ? QStringLiteral("la app esta al frente") : QStringLiteral("la app corre sin el foco");
        return decision;
    }
    if (inFront && snapshot.foreground) {
        decision.action = Action::Minimize;
        decision.window = snapshot.foreground;
        decision.reason = QStringLiteral("la ventana del frente es de la app");
        return decision;
    }
    for (const Window &window : snapshot.windows) {
        if (!samePath(window.processPath, appPath) || !eligible(window)) {
            continue;
        }
        decision.action = Action::Activate;
        decision.window = window.id;
        decision.restore = window.minimized;
        decision.reason = window.minimized ? QStringLiteral("su ultima ventana esta minimizada")
                                           : QStringLiteral("su ultima ventana esta detras");
        return decision;
    }
    decision.action = Action::Nothing;
    decision.reason = QStringLiteral("corre sin una ventana que se pueda mostrar (solo en la bandeja?)");
    return decision;
}

QString describe(const Decision &decision)
{
    const char *action = decision.action == Action::Launch     ? "abrir"
                       : decision.action == Action::Minimize   ? "minimizar"
                       : decision.action == Action::Activate   ? (decision.restore ? "restaurar y traer al frente"
                                                                                   : "traer al frente")
                                                               : "nada";
    QString text = QStringLiteral("%1 (%2)").arg(QLatin1String(action), decision.reason);
    if (decision.window) {
        text += QStringLiteral(" ventana=0x%1").arg(QString::number(quint64(decision.window), 16));
    }
    return text;
}

} // namespace AppToggleSystem
