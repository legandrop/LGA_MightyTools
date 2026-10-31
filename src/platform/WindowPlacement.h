#ifndef MIGHTYTOOLS_WINDOWPLACEMENT_H
#define MIGHTYTOOLS_WINDOWPLACEMENT_H

#include <QList>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QString>

// Geometria en coordenadas globales logicas de Qt para el selector; el movimiento
// se resuelve en la escala nativa de la pantalla de la ventana activa.
namespace WindowPlacement {

struct WindowInfo
{
    QString title;
    QRect geometry;
};

QList<WindowInfo> selectableWindows(QString *error = nullptr);

// Area util del monitor que contiene el punto global logico de Qt.
// Devuelve un rectangulo vacio si el punto queda fuera de las pantallas.
QRect pickerWorkAreaAt(const QPoint &globalLogical);

// normalizedArea expresa una fraccion del area util del monitor, no de toda la pantalla.
// Los bordes se redondean por separado para que regiones contiguas compartan el mismo borde.
QRect resolve(const QRect &workArea, const QRectF &normalizedArea);

// En dryRun solo valida el rectangulo; no consulta ni modifica ventanas del sistema.
bool moveForeground(const QRectF &normalizedArea, QString *error = nullptr, bool dryRun = false);

// En dryRun no consulta ni modifica ventanas del sistema.
bool minimizeForeground(QString *error = nullptr, bool dryRun = false);

#ifdef Q_OS_WIN
// La ventana del frente es una ventana comun de una app abierta como administrador. Desde un
// proceso sin elevar, un proceso cuya elevacion no se puede leer cuenta como elevado.
bool foregroundIsElevated();
// Este proceso corre como administrador.
bool currentProcessElevated();
#endif

} // namespace WindowPlacement

#endif // MIGHTYTOOLS_WINDOWPLACEMENT_H
