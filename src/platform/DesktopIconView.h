#ifndef MIGHTYTOOLS_DESKTOPICONVIEW_H
#define MIGHTYTOOLS_DESKTOPICONVIEW_H

#include <QList>
#include <QPoint>
#include <QSize>
#include <QString>

// La vista de iconos del escritorio del Explorador (Desktop Icons, solo Windows). Implementacion en
// platform/win/DesktopIconViewWin.cpp: IShellWindows::FindWindowSW(SWC_DESKTOP) -> IFolderView2.
// Coordenadas: las de la vista del escritorio, en pixeles fisicos, con el origen en la esquina de la
// pantalla virtual (todos los monitores).
namespace DesktopIconView {

struct Icon
{
    QString path;   ///< ruta de parseo (SIGDN_DESKTOPABSOLUTEPARSING): identifica al icono
    QString name;   ///< nombre visible, solo para mostrar
    QPoint position;
};

struct Layout
{
    QList<Icon> icons;
    QSize screen;   ///< pantalla virtual en pixeles fisicos
    int dpi = 96;
    QSize spacing;  ///< espaciado de iconos de la vista
    bool autoArrange = false;
    bool snapToGrid = false;
};

// Lee el escritorio (solo lectura). False con el motivo si el Explorador no lo da.
bool read(Layout *layout, QString *error);

// Tamano de la pantalla virtual en pixeles fisicos (barato, sin COM).
QSize screenSize();

// Milisegundos despierta desde el arranque: QueryUnbiasedInterruptTime, que NO cuenta el tiempo
// suspendido o hibernado (a diferencia del reloj de pared).
qint64 awakeClockMs();

// Milisegundos desde la ultima tecla o movimiento del mouse del usuario (GetLastInputInfo).
qint64 idleMs();

struct ApplyResult
{
    int moved = 0;     ///< iconos que quedaron EXACTO en su posicion, releida despues (o los que se moverian)
    int notFound = 0;  ///< pedidos que ya no estan en el escritorio
    int notPlaced = 0; ///< se pidieron pero quedaron en otro lado (Organizar automaticamente, por ejemplo)
    bool dryRun = false;
};

// Deja "Organizar automaticamente" y "Alinear a la cuadricula" como se pide y mueve todos los iconos en UNA
// llamada a SelectAndPositionItems (como la herramienta que recupero el escritorio de Lega); despues relee
// cada posicion. Nunca reinicia el Explorador. Con dryRun, o en una corrida automatizada
// (AutomatedRun::active()), no toca el escritorio: solo loguea y devuelve lo que haria.
bool apply(bool autoArrange, bool snapToGrid, const QList<Icon> &moves, bool dryRun, ApplyResult *result,
           QString *error);

} // namespace DesktopIconView

#endif // MIGHTYTOOLS_DESKTOPICONVIEW_H
