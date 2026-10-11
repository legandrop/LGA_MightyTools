#include "platform/WindowPlacement.h"

#include <QtGlobal>

#include <cmath>

namespace WindowPlacement {

QRect resolve(const QRect &workArea, const QRectF &normalizedArea)
{
    if (workArea.isEmpty() || !std::isfinite(normalizedArea.x()) || !std::isfinite(normalizedArea.y())
        || !std::isfinite(normalizedArea.width()) || !std::isfinite(normalizedArea.height())
        || normalizedArea.width() <= 0 || normalizedArea.height() <= 0) {
        return {};
    }

    const auto edge = [](double fraction, int extent) {
        return qRound(qBound(0.0, fraction, 1.0) * extent);
    };
    const int left = edge(normalizedArea.x(), workArea.width());
    const int right = edge(normalizedArea.x() + normalizedArea.width(), workArea.width());
    const int top = edge(normalizedArea.y(), workArea.height());
    const int bottom = edge(normalizedArea.y() + normalizedArea.height(), workArea.height());
    if (right <= left || bottom <= top) {
        return {};
    }
    return QRect(workArea.x() + left, workArea.y() + top, right - left, bottom - top);
}

} // namespace WindowPlacement
