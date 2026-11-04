#ifndef MIGHTYTOOLS_HELPSECTION_H
#define MIGHTYTOOLS_HELPSECTION_H

#include "ui/Theme.h"

#include <QString>
#include <QStringList>

// La ayuda de una herramienta: la franja «How it works» de su pagina (D-73). Los pasos aceptan texto
// enriquecido; strong() resalta un termino (`.st b` del diseno) y la franja dibuja como teclas los que son
// atajos.
struct HelpSection
{
    QString title;
    QStringList steps;
    QString note;

    static QString strong(const QString &text)
    {
        return QStringLiteral("<span style=\"color:%1; font-weight:600;\">%2</span>").arg(QLatin1String(Theme::kTextBright), text);
    }
};

#endif // MIGHTYTOOLS_HELPSECTION_H
