#ifndef MIGHTYTOOLS_OPENINNUKEX_APPLYISSUE_H
#define MIGHTYTOOLS_OPENINNUKEX_APPLYISSUE_H

#include <QList>
#include <QString>
#include <QStringList>

// Lo que puede salir mal al asociar los .nk. El hilo de trabajo solo devuelve ESTOS CODIGOS; el texto visible lo
// arma el hilo de la UI al recibir el resultado (OpenInNukeXMessages::applyIssueText), en el idioma de ese
// momento. Asi un cambio de idioma durante el Apply no deja un cartel en el idioma anterior y I18n::tr nunca se
// llama desde el hilo de trabajo.
enum class ApplyIssue {
    Unknown = 0,
    RegisterProgId,
    RegisterDefaultApps,
    RegisterExtension,
    CleanRegistry,
    WriteAssociation,
    OpenDefaultApps,
};

// Nombre estable para el LOG (nunca para la UI).
inline QString applyIssueCode(ApplyIssue issue)
{
    switch (issue) {
    case ApplyIssue::RegisterProgId:
        return QStringLiteral("register-progid");
    case ApplyIssue::RegisterDefaultApps:
        return QStringLiteral("register-default-apps");
    case ApplyIssue::RegisterExtension:
        return QStringLiteral("register-extension");
    case ApplyIssue::CleanRegistry:
        return QStringLiteral("clean-registry");
    case ApplyIssue::WriteAssociation:
        return QStringLiteral("write-association");
    case ApplyIssue::OpenDefaultApps:
        return QStringLiteral("open-default-apps");
    case ApplyIssue::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

inline QString applyIssuesForLog(const QList<ApplyIssue> &issues)
{
    QStringList codes;
    for (const ApplyIssue issue : issues) {
        codes << applyIssueCode(issue);
    }
    return codes.join(QLatin1Char(' '));
}

#endif // MIGHTYTOOLS_OPENINNUKEX_APPLYISSUE_H
