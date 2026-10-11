#include "modules/windowlayouts/LayoutGeometry.h"

#include <QtMath>
#include <cmath>

namespace LayoutGeometry {

bool validArea(const QRectF &area)
{
    return std::isfinite(area.x()) && std::isfinite(area.y()) && std::isfinite(area.width())
        && std::isfinite(area.height()) && area.x() >= 0.0 && area.y() >= 0.0
        && area.width() > 0.0 && area.height() > 0.0 && area.right() <= 1.0 + 1e-9
        && area.bottom() <= 1.0 + 1e-9;
}

QRectF grid(AxisSlice horizontal, AxisSlice vertical)
{
    if (!horizontal.valid() || !vertical.valid()) {
        return {};
    }
    return QRectF(double(horizontal.index) / horizontal.parts, double(vertical.index) / vertical.parts,
                  1.0 / horizontal.parts, 1.0 / vertical.parts);
}

QRectF capture(const QRect &selection, const QRect &workArea)
{
    if (workArea.isEmpty()) {
        return {};
    }
    const QRect clipped = selection.normalized().intersected(workArea);
    if (clipped.isEmpty()) {
        return {};
    }
    return QRectF(double(clipped.x() - workArea.x()) / workArea.width(),
                  double(clipped.y() - workArea.y()) / workArea.height(),
                  double(clipped.width()) / workArea.width(), double(clipped.height()) / workArea.height());
}

QRect resolve(const QRect &workArea, const QRectF &normalizedArea)
{
    if (workArea.isEmpty() || !validArea(normalizedArea)) {
        return {};
    }
    const int left = workArea.x() + qRound(normalizedArea.x() * workArea.width());
    const int top = workArea.y() + qRound(normalizedArea.y() * workArea.height());
    const int right = workArea.x() + qRound(normalizedArea.right() * workArea.width());
    const int bottom = workArea.y() + qRound(normalizedArea.bottom() * workArea.height());
    return QRect(left, top, qMax(1, right - left), qMax(1, bottom - top)).intersected(workArea);
}

} // namespace LayoutGeometry
