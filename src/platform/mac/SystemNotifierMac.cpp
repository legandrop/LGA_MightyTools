#include "platform/SystemNotifier.h"

#include <QDebug>
#include <QProcess>

// macOS: `osascript display notification`, como la rama mac de LGA_PipeSync_2. El centro de
// notificaciones pone el icono de la app. Sin compilar todavia en un Mac.

struct SystemNotifier::Private
{
};

SystemNotifier::SystemNotifier(bool automatedRun, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
    , m_automated(automatedRun)
{
}

SystemNotifier::~SystemNotifier() = default;

bool SystemNotifier::workerRunning() const
{
    return false;
}

void SystemNotifier::show(const Notice &notice)
{
    // osascript no tiene acciones: salen el titulo y el texto.
    show(notice.title, notice.body);
    m_last.notice = notice;
}

void SystemNotifier::show(const QString &title, const QString &body)
{
    m_last = Last{title, body, IconFile{}, false, Notice{}};
    if (m_automated) {
        qInfo().noquote() << QStringLiteral("[SystemNotifier] (automatizada, sin mostrar) '%1' | '%2'").arg(title, body);
        return;
    }
    QString escapedTitle = title;
    QString escapedBody = body;
    escapedTitle.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escapedBody.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    const QString script = QStringLiteral("display notification \"%1\" with title \"%2\" sound name \"default\"")
                               .arg(escapedBody, escapedTitle);
    m_last.launched = true;
    // Un proceso corto por aviso; se borra solo al terminar.
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    process->start(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
}
