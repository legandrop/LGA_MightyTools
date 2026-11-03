#ifndef MIGHTYTOOLS_DESKTOPSNAPSHOTS_H
#define MIGHTYTOOLS_DESKTOPSNAPSHOTS_H

#include "platform/DesktopIconView.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// Snapshots de Desktop Icons: el dato, su JSON y las reglas (funciones puras, sin sistema ni disco: el
// --self-test las prueba con datos falsos). Decisiones de Lega en Docs/Roadmap_Desktop_Icons.md.

enum class SnapshotKind {
    Manual,        ///< lo guarda el usuario; nunca se borra solo
    Automatic,     ///< uno por dia; quedan los ultimos kKeepAutomatic
    BeforeRestore, ///< el escritorio justo antes de restaurar (deshacer); quedan los ultimos kKeepBeforeRestore
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
constexpr int kKeepBeforeRestore = 3;
constexpr int kAwakeMinutes = 60;
constexpr int kIdleMinutes = 10;
constexpr int kStreakDays = 3;

// Clave del tipo en el JSON y en el nombre del archivo ("manual", "automatic", "beforeRestore").
QString kindKey(SnapshotKind kind);

QByteArray toJson(const DesktopSnapshot &snapshot);
// False si el JSON no es un snapshot valido (archivo corrupto, version desconocida, icono sin ruta).
bool fromJson(const QByteArray &json, DesktopSnapshot *snapshot, QString *error = nullptr);

// Los mismos iconos (por ruta) en las mismas posiciones, en cualquier orden.
bool sameArrangement(const DesktopIconView::Layout &a, const DesktopIconView::Layout &b);

// Resolucion habitual: la que mas se repite entre los manuales y automaticos guardados (los "antes de
// restaurar" no cuentan: suelen ser un escritorio desordenado). Sin guardados, o empatada con otra en el
// primer lugar, cuenta como habitual.
bool isUsualScreen(const QList<DesktopSnapshot> &stored, const QSize &screen);
// La habitual para mostrar: la que mas se repite; en un empate, la del snapshot mas nuevo. QSize() sin datos.
QSize usualScreen(const QList<DesktopSnapshot> &stored);

// Ids que sobran: los automaticos mas alla de los kKeepAutomatic mas nuevos y los "antes de restaurar" mas
// alla de los kKeepBeforeRestore mas nuevos. Los manuales y `keep` (el destino de Undo) nunca.
QStringList toPrune(const QList<DesktopSnapshot> &stored, const QString &keep = QString());

// El automatico del dia todavia hace falta: no hay uno de hoy, el dia no se salteo, la computadora lleva
// kAwakeMinutes despierta y el usuario esta presente (alguna tecla o el mouse en los ultimos kIdleMinutes:
// evita la foto con el monitor dormido o desconectado).
bool needsAutomatic(const QList<DesktopSnapshot> &stored, const QDateTime &now, const QDateTime &awakeSince,
                    const QDate &skippedDay, qint64 idleMs);

// Racha de dias con una resolucion fuera de lo habitual. Despues de kStreakDays dias seguidos (dias con
// intento, sin uno de resolucion habitual en el medio) esa resolucion pasa a ser la habitual (`usual`).
struct ScreenStreak
{
    QSize screen;  ///< la resolucion salteada
    int days = 0;  ///< dias seguidos salteados con esa resolucion
    QSize usual;   ///< habitual fijada por una racha; vacia = la que mas se repite en los guardados
};

enum class AutomaticStep {
    Save,       ///< guardar uno nuevo
    Update,     ///< igual al automatico anterior: solo se actualiza su fecha
    SkipScreen, ///< resolucion distinta de la habitual: el dia se saltea
    Wait,       ///< el escritorio llego vacio (Explorador arrancando): se reintenta en el proximo turno
};
// Lo llama el automatico a lo sumo una vez por dia con resolucion salteada; actualiza la racha.
AutomaticStep automaticStep(const QList<DesktopSnapshot> &stored, const DesktopIconView::Layout &current,
                            ScreenStreak *streak, QString *sameId);

// Hubo suspension entre dos turnos si el reloj de pared avanzo bastante mas que el reloj despierto.
bool suspendedBetween(qint64 wallBeforeMs, qint64 awakeBeforeMs, qint64 wallNowMs, qint64 awakeNowMs);

struct RestorePlan
{
    QList<DesktopIconView::Icon> moves; ///< los del snapshot que siguen en el escritorio, a su posicion
    QStringList missing;                ///< nombres de los que ya no estan
};
RestorePlan plan(const DesktopIconView::Layout &target, const DesktopIconView::Layout &current);

// Una restauracion sobre la lista en memoria (el disco y el escritorio los pone quien llama):
//  - Si el escritorio ya esta como el destino (doble click en Restore), Undo sigue donde estaba.
//  - Si el escritorio de ahora es igual a un snapshot guardado, no se crea un "antes": Undo va a ese.
//  - Si no, se agrega un "antes de restaurar" (`newId`) y Undo va a el.
//  - `apply` restaura; solo si sale bien se poda (nunca el destino de Undo). Si falla no se borra nada.
struct RestoreRun
{
    bool found = false;
    bool ok = false;
    QString undoId;
    QStringList created; ///< ids agregados a la lista (a guardar en disco)
    QStringList pruned;  ///< ids sacados de la lista (a borrar del disco)
    RestorePlan plan;
};
RestoreRun restoreInto(QList<DesktopSnapshot> *stored, const QString &targetId, const DesktopIconView::Layout &current,
                       const QDateTime &now, const QString &previousUndo, const std::function<QString()> &newId,
                       const std::function<bool(const DesktopSnapshot &target, const RestorePlan &plan)> &apply);

// "Today 10:42", "Yesterday 22:15" o "2026-10-28 10:42" (la fecha como el resto de la app).
QString whenText(const QDateTime &when, const QDateTime &now);
// "1920 × 1080".
QString screenText(const QSize &size);

} // namespace DesktopSnapshots

#endif // MIGHTYTOOLS_DESKTOPSNAPSHOTS_H
