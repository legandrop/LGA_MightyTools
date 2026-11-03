#ifndef MIGHTYTOOLS_DESKTOPSNAPSHOTS_H
#define MIGHTYTOOLS_DESKTOPSNAPSHOTS_H

#include "platform/DesktopIconView.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Snapshots de Desktop Icons: el dato, su JSON y las reglas (funciones puras, sin sistema ni disco: el
// --self-test las prueba con datos falsos). Decisiones de Lega en Docs/Roadmap_Desktop_Icons.md.

enum class SnapshotKind {
    Manual,        ///< lo guarda el usuario; nunca se borra solo
    Automatic,     ///< uno por dia; quedan los ultimos kKeepAutomatic
    BeforeRestore, ///< el escritorio justo antes de restaurar (deshacer); queda solo el ultimo
};

struct DesktopSnapshot
{
    QString id; ///< nombre del archivo sin .json; estable aunque cambie la fecha
    SnapshotKind kind = SnapshotKind::Manual;
    QString name;
    QDateTime when; ///< hora local; un automatico igual al anterior la actualiza
    DesktopIconView::Layout layout;
};

namespace DesktopSnapshots {

constexpr int kKeepAutomatic = 5;
constexpr int kAwakeMinutes = 60;

QByteArray toJson(const DesktopSnapshot &snapshot);
// False si el JSON no es un snapshot valido (archivo corrupto, version desconocida, icono sin ruta).
bool fromJson(const QByteArray &json, DesktopSnapshot *snapshot, QString *error = nullptr);

// Los mismos iconos (por ruta) en las mismas posiciones, en cualquier orden.
bool sameArrangement(const DesktopIconView::Layout &a, const DesktopIconView::Layout &b);

// Resolucion habitual: la que mas se repite entre los manuales y automaticos guardados (los "antes de
// restaurar" no cuentan: suelen ser un escritorio desordenado). Sin guardados, o empatada con otra en el
// primer lugar, cuenta como habitual.
bool isUsualScreen(const QList<DesktopSnapshot> &stored, const QSize &screen);

// Ids que sobran: los automaticos mas alla de los kKeepAutomatic mas nuevos y los "antes de restaurar"
// salvo el mas nuevo. Los manuales nunca.
QStringList toPrune(const QList<DesktopSnapshot> &stored);

// El automatico del dia todavia hace falta: no hay uno de hoy, el dia no se salteo y la computadora lleva
// kAwakeMinutes despierta.
bool needsAutomatic(const QList<DesktopSnapshot> &stored, const QDateTime &now, const QDateTime &awakeSince,
                    const QDate &skippedDay);

enum class AutomaticStep {
    Save,       ///< guardar uno nuevo
    Update,     ///< igual al automatico anterior: solo se actualiza su fecha
    SkipScreen, ///< resolucion distinta de la habitual: el dia se saltea
    Wait,       ///< el escritorio llego vacio (Explorador arrancando): se reintenta en el proximo turno
};
AutomaticStep automaticStep(const QList<DesktopSnapshot> &stored, const DesktopIconView::Layout &current,
                            QString *sameId);

// Hubo suspension entre dos turnos si el reloj de pared avanzo bastante mas que el reloj despierto.
bool suspendedBetween(qint64 wallBeforeMs, qint64 awakeBeforeMs, qint64 wallNowMs, qint64 awakeNowMs);

struct RestorePlan
{
    QList<DesktopIconView::Icon> moves; ///< los del snapshot que siguen en el escritorio, a su posicion
    QStringList missing;                ///< nombres de los que ya no estan
};
RestorePlan plan(const DesktopIconView::Layout &target, const DesktopIconView::Layout &current);

// "Today 10:42", "Yesterday 22:15" o "2026-10-28 10:42" (la fecha como el resto de la app).
QString whenText(const QDateTime &when, const QDateTime &now);

} // namespace DesktopSnapshots

#endif // MIGHTYTOOLS_DESKTOPSNAPSHOTS_H
