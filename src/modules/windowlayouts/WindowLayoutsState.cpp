#include "modules/windowlayouts/WindowLayoutsState.h"

#include "app/ModuleContext.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {
const QString kLayouts = QStringLiteral("layouts");
const QString kPaused = QStringLiteral("paused");

bool validLayout(const WindowLayout &layout)
{
    return layout.id >= 1 && layout.id <= WindowLayoutsState::kMaxLayouts
        && !layout.name.trimmed().isEmpty() && layout.name.size() <= 80
        && layout.horizontal.valid() && layout.vertical.valid()
        && layout.shortcut.isValid() && LayoutGeometry::validArea(layout.normalizedArea());
}
} // namespace

QRectF WindowLayout::normalizedArea() const
{
    return destination == LayoutDestination::Custom ? customArea : LayoutGeometry::grid(horizontal, vertical);
}

WindowLayoutsState::WindowLayoutsState(ModuleContext *context)
    : m_context(context)
{
    if (context) {
        m_layouts = fromJson(context->value(kLayouts).toString());
        m_paused = context->value(kPaused, false).toBool();
    }
}

void WindowLayoutsState::setPaused(bool paused)
{
    if (m_paused == paused) {
        return;
    }
    m_paused = paused;
    if (m_context) {
        m_context->setValue(kPaused, paused);
    }
}

bool WindowLayoutsState::upsert(WindowLayout &layout)
{
    int position = -1;
    if (layout.id != 0) {
        for (int i = 0; i < m_layouts.size(); ++i) {
            if (m_layouts.at(i).id == layout.id) {
                position = i;
                break;
            }
        }
        if (position < 0) {
            return false;
        }
    } else {
        if (m_layouts.size() >= kMaxLayouts) {
            return false;
        }
        QSet<int> used;
        for (const WindowLayout &stored : m_layouts) {
            used.insert(stored.id);
        }
        for (int id = 1; id <= kMaxLayouts; ++id) {
            if (!used.contains(id)) {
                layout.id = id;
                break;
            }
        }
    }
    layout.name = layout.name.trimmed();
    if (!validLayout(layout)) {
        return false;
    }
    if (layout.shortcut.isValid()) {
        for (const WindowLayout &stored : m_layouts) {
            if (stored.id != layout.id && stored.shortcut == layout.shortcut) {
                return false;
            }
        }
    }
    if (position >= 0) {
        m_layouts[position] = layout;
    } else {
        m_layouts.append(layout);
    }
    save();
    return true;
}

bool WindowLayoutsState::remove(int id)
{
    for (int i = 0; i < m_layouts.size(); ++i) {
        if (m_layouts.at(i).id == id) {
            m_layouts.removeAt(i);
            save();
            return true;
        }
    }
    return false;
}

void WindowLayoutsState::setCaptureLayouts(const QList<WindowLayout> &layouts)
{
    if (!m_context || m_context->captureMode()) {
        m_layouts = layouts;
    }
}

QList<WindowLayout> WindowLayoutsState::fromJson(const QString &text)
{
    QList<WindowLayout> result;
    if (text.size() > 65536) {
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) {
        return result;
    }
    QSet<int> seen;
    QSet<QString> shortcuts;
    for (const QJsonValue &value : document.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        WindowLayout layout;
        layout.id = object.value(QStringLiteral("id")).toInt();
        layout.name = object.value(QStringLiteral("name")).toString();
        layout.shortcut = Shortcut::fromPortableString(object.value(QStringLiteral("shortcut")).toString());
        layout.destination = object.value(QStringLiteral("destination")).toString() == QLatin1String("custom")
            ? LayoutDestination::Custom : LayoutDestination::Grid;
        layout.horizontal = {object.value(QStringLiteral("horizontalParts")).toInt(1),
                             object.value(QStringLiteral("horizontalIndex")).toInt()};
        layout.vertical = {object.value(QStringLiteral("verticalParts")).toInt(1),
                           object.value(QStringLiteral("verticalIndex")).toInt()};
        layout.customArea = QRectF(object.value(QStringLiteral("x")).toDouble(),
                                   object.value(QStringLiteral("y")).toDouble(),
                                   object.value(QStringLiteral("width")).toDouble(),
                                   object.value(QStringLiteral("height")).toDouble());
        const QString shortcut = layout.shortcut.toPortableString();
        if (validLayout(layout) && !seen.contains(layout.id)
            && (shortcut.isEmpty() || !shortcuts.contains(shortcut))) {
            result.append(layout);
            seen.insert(layout.id);
            if (!shortcut.isEmpty()) shortcuts.insert(shortcut);
            if (result.size() == kMaxLayouts) {
                break;
            }
        }
    }
    return result;
}

QString WindowLayoutsState::toJson(const QList<WindowLayout> &layouts)
{
    QJsonArray array;
    for (const WindowLayout &layout : layouts) {
        if (!validLayout(layout)) {
            continue;
        }
        QJsonObject object;
        object.insert(QStringLiteral("id"), layout.id);
        object.insert(QStringLiteral("name"), layout.name);
        object.insert(QStringLiteral("shortcut"), layout.shortcut.toPortableString());
        object.insert(QStringLiteral("destination"),
                      layout.destination == LayoutDestination::Custom ? QStringLiteral("custom") : QStringLiteral("grid"));
        object.insert(QStringLiteral("horizontalParts"), layout.horizontal.parts);
        object.insert(QStringLiteral("horizontalIndex"), layout.horizontal.index);
        object.insert(QStringLiteral("verticalParts"), layout.vertical.parts);
        object.insert(QStringLiteral("verticalIndex"), layout.vertical.index);
        object.insert(QStringLiteral("x"), layout.customArea.x());
        object.insert(QStringLiteral("y"), layout.customArea.y());
        object.insert(QStringLiteral("width"), layout.customArea.width());
        object.insert(QStringLiteral("height"), layout.customArea.height());
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void WindowLayoutsState::save()
{
    if (m_context) {
        m_context->setValue(kLayouts, toJson(m_layouts));
    }
}
