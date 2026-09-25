#ifndef MIGHTYTOOLS_HELPSECTION_H
#define MIGHTYTOOLS_HELPSECTION_H

#include "ui/Theme.h"

#include <QString>
#include <QStringList>

// Una seccion de la ayuda unica (canvas, seccion 6): una por herramienta, prendida o apagada.
// Los pasos aceptan texto enriquecido; strong() resalta un termino como en el canvas.
struct HelpSection
{
    QString title;
    QStringList steps;
    QString note;

    static QString strong(const QString &text)
    {
        return QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(QLatin1String(Theme::kTextBright), text);
    }
};

#endif // MIGHTYTOOLS_HELPSECTION_H
