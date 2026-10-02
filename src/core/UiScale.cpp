#include "core/UiScale.h"

#include "core/AppSettings.h"

#include <QByteArray>

namespace {

int g_sessionLevel = 0;
bool g_envSet = false;
// Un QT_SCALE_FACTOR que el usuario ya tuviera: se devuelve tal cual despues de crear la app.
bool g_hadPrevious = false;
QByteArray g_previous;
bool g_areaOverridden = false;
QSize g_overriddenArea;
constexpr char kEnvName[] = "QT_SCALE_FACTOR";

} // namespace

namespace UiScale {

QString settingsKey()
{
    return QStringLiteral("app/uiSize");
}

bool supported()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
#endif
}

int clampLevel(int level)
{
    return level >= 0 && level <= kMaxLevel ? level : kDefaultLevel;
}

qreal factor(int level)
{
    // Pasos chicos: el 2 es "un puntito mas" que el 1. Con la ventana mas grande (largestWindow, 960 x 676),
    // el 2 pide 1152 x 812 de area util.
    static constexpr qreal kFactors[kMaxLevel + 1] = {1.0, 1.1, 1.2};
    return kFactors[clampLevel(level)];
}

QSize largestWindow()
{
    return QSize(960, 676);
}

int maxFittingLevel(const QSize &unscaledArea)
{
    if (!unscaledArea.isValid() || unscaledArea.isEmpty()) {
        return kMaxLevel;
    }
    const QSize window = largestWindow();
    for (int level = kMaxLevel; level > 0; --level) {
        const qreal f = factor(level);
        if (window.width() * f <= unscaledArea.width() && window.height() * f <= unscaledArea.height()) {
            return level;
        }
    }
    return 0;
}

int fitLevel(int wanted, const QSize &unscaledArea)
{
    return qMin(clampLevel(wanted), maxFittingLevel(unscaledArea));
}

void overrideScreenArea(const QSize &unscaledArea)
{
    g_areaOverridden = true;
    g_overriddenArea = unscaledArea;
}

bool screenAreaOverridden()
{
    return g_areaOverridden;
}

QSize overriddenScreenArea()
{
    return g_overriddenArea;
}

int readSavedLevel()
{
    bool ok = false;
    const int level = AppSettings::open()->value(settingsKey(), kDefaultLevel).toInt(&ok);
    return ok ? clampLevel(level) : kDefaultLevel;
}

void applyBeforeApp(int level)
{
    g_sessionLevel = supported() ? clampLevel(level) : 0;
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
