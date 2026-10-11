#ifndef MIGHTYTOOLS_LAYOUTGEOMETRY_H
#define MIGHTYTOOLS_LAYOUTGEOMETRY_H

#include <QRect>
#include <QRectF>

namespace LayoutGeometry {

struct AxisSlice
{
    int parts = 1;
    int index = 0;
    bool valid() const { return (parts >= 1 && parts <= 4 && (parts != 1 || index == 0)) && index >= 0 && index < parts; }
};

bool validArea(const QRectF &area);
QRectF grid(AxisSlice horizontal, AxisSlice vertical);
QRectF capture(const QRect &selection, const QRect &workArea);
QRect resolve(const QRect &workArea, const QRectF &normalizedArea);

} // namespace LayoutGeometry

#endif // MIGHTYTOOLS_LAYOUTGEOMETRY_H
