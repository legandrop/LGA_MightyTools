#ifndef MIGHTYTOOLS_WINDOWLAYOUTSSTATE_H
#define MIGHTYTOOLS_WINDOWLAYOUTSSTATE_H

#include "core/Shortcut.h"
#include "modules/windowlayouts/LayoutGeometry.h"

#include <QList>
#include <QRectF>
#include <QString>

class ModuleContext;

enum class LayoutDestination { Grid, Custom, Minimize };

struct WindowLayout
{
    int id = 0;
    QString name;
    Shortcut shortcut{Qt::NoModifier, 0};
    bool takePriority = false;
    LayoutDestination destination = LayoutDestination::Grid;
    LayoutGeometry::AxisSlice horizontal;
    LayoutGeometry::AxisSlice vertical;
    QRectF customArea;

    QRectF normalizedArea() const;
};

class WindowLayoutsState
{
public:
    static constexpr int kMaxLayouts = 99;
    explicit WindowLayoutsState(ModuleContext *context = nullptr);

    const QList<WindowLayout> &layouts() const { return m_layouts; }
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    bool upsert(WindowLayout &layout);
    bool remove(int id);
    void setCaptureLayouts(const QList<WindowLayout> &layouts);

    static QList<WindowLayout> fromJson(const QString &text);
    static QString toJson(const QList<WindowLayout> &layouts);

private:
    void save();
    ModuleContext *m_context = nullptr;
    QList<WindowLayout> m_layouts;
    bool m_paused = false;
};

#endif // MIGHTYTOOLS_WINDOWLAYOUTSSTATE_H
