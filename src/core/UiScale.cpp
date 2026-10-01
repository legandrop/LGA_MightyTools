#include "core/UiScale.h"

#include "core/AppSettings.h"

#include <QByteArray>

namespace {

int g_sessionLevel = 0;
bool g_envSet = false;
// Un QT_SCALE_FACTOR que el usuario ya tuviera: se devuelve tal cual despues de crear la app.
bool g_hadPrevious = false;
QByteArray g_previous;
constexpr char kEnvName[] = "QT_SCALE_FACTOR";

} // namespace

namespace UiScale {

QString settingsKey()
{
    return QStringLiteral("app/uiSize");
}

int clampLevel(int level)
{
    return level >= 0 && level <= kMaxLevel ? level : kDefaultLevel;
}

qreal factor(int level)
{
    // Pasos chicos: el 2 es "un puntito mas" que el 1. Con la ventana de 780 x 676, el 2 la deja en
    // 936 x 811 sobre una pantalla al 100 %.
    static constexpr qreal kFactors[kMaxLevel + 1] = {1.0, 1.1, 1.2};
    return kFactors[clampLevel(level)];
}

int readSavedLevel()
{
    bool ok = false;
    const int level = AppSettings::open()->value(settingsKey(), kDefaultLevel).toInt(&ok);
    return ok ? clampLevel(level) : kDefaultLevel;
}

void applyBeforeApp(int level)
{
    g_sessionLevel = clampLevel(level);
    if (g_sessionLevel == 0) {
        return;
    }
    g_hadPrevious = qEnvironmentVariableIsSet(kEnvName);
    g_previous = qgetenv(kEnvName);
    qputenv(kEnvName, QByteArray::number(factor(g_sessionLevel), 'f', 2));
    g_envSet = true;
}

void clearEnvironmentAfterApp()
{
    if (g_envSet) {
        if (g_hadPrevious) {
            qputenv(kEnvName, g_previous);
        } else {
            qunsetenv(kEnvName);
        }
        g_envSet = false;
    }
}

int sessionLevel()
{
    return g_sessionLevel;
}

} // namespace UiScale
