#include "platform/InputInjector.h"

#include <QDebug>

InputInjector::InputInjector(bool dryRun)
    : m_dryRun(dryRun)
{
}

QString InputInjector::keyName(Key key)
{
    switch (key) {
    case Key::Down:
        return QStringLiteral("Down");
    case Key::Return:
        return QStringLiteral("Return");
    case Key::A:
        return QStringLiteral("A");
    case Key::F:
        return QStringLiteral("F");
    }
    return QString();
}

void InputInjector::note(const QString &step)
{
    m_steps << step;
    qDebug().noquote() << QStringLiteral("[InputInjector]%1 %2").arg(m_dryRun ? QStringLiteral(" (dry-run)") : QString(), step);
}
