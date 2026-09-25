#ifndef MIGHTYTOOLS_APPSTATE_H
#define MIGHTYTOOLS_APPSTATE_H

#include "core/DiskSpace.h"
#include "core/Shortcut.h"

#include <QDateTime>
#include <QHash>
#include <QList>

#include <QObject>
#include <QPointF>

// Las dos acciones que la app sabe hacer sobre Nuke.
enum class ShortcutAction { AddKeyframe, FrameDopeSheet };

// Estado de la app que ve el usuario: la UNICA fuente de verdad. La tarjeta de estado, el menu del
// tray, su encabezado y el icono leen de aca y escriben aca, asi no pueden quedar dos controles
// diciendo cosas distintas.
//
// Persistence::Settings lee y escribe settings.ini (AppSettings). Persistence::None no toca el
// disco nunca: es el de la captura --ui-shot y el de los modos de prueba.
class AppState : public QObject
{
    Q_OBJECT

public:
    enum class Persistence { Settings, None };

    // Que paso la ultima vez que se intento registrar el atajo de una accion.
    enum class Registration {
        Idle,       ///< no esta registrado porque no hace falta (Nuke no esta al frente, o en pausa)
        Registered, ///< registrado: la combinacion le llega a la app
        Failed,     ///< el sistema lo rechazo: otra app ya tiene esa combinacion
    };

    explicit AppState(Persistence persistence, QObject *parent = nullptr);

    // ---- Persistente
    bool enabled() const { return m_enabled; }
    bool checkUpdatesAtStartup() const { return m_checkUpdatesAtStartup; }
    Shortcut shortcut(ShortcutAction action) const;
    // Punto guardado del Dope Sheet, en fraccion de la ventana principal de Nuke (0..1 en x e y).
    bool hasDopeSheetSpot() const { return m_hasDopeSheetSpot; }
    QPointF dopeSheetSpot() const { return m_dopeSheetSpot; }
    // Chequeo de espacio: cada cuantos minutos (uno solo para todos) y que discos, en el orden en
    // que se agregaron.
    int diskCheckMinutes() const { return m_diskCheckMinutes; }
    QList<DiskWatch> diskWatches() const { return m_diskWatches; }
    bool isWatched(const QString &root) const;

    // ---- De esta sesion
    bool nukeInFront() const { return m_nukeInFront; }
    Registration registration(ShortcutAction action) const;
    // Permiso de Accesibilidad de macOS. En Windows siempre true.
    bool accessibilityGranted() const { return m_accessibilityGranted; }
    // Ultima lectura de cada disco local, por raiz. Un disco vigilado que no esta aca no esta
    // enchufado. La llena DiskMonitor (o el fixture de la captura).
    bool driveReading(const QString &root, DriveInfo *drive) const;
    QList<DriveInfo> drives() const;
    QDateTime lastDiskCheck() const { return m_lastDiskCheck; }
    // Los vigilados que estan enchufados y por debajo de su umbral, en el orden de la lista.
    QList<DiskWatch> lowWatches() const;

    // Cada setter escribe (si corresponde) y avisa SOLO si el valor cambio.
    void setEnabled(bool enabled);
    void setCheckUpdatesAtStartup(bool check);
    void setShortcut(ShortcutAction action, const Shortcut &shortcut);
    void setDopeSheetSpot(const QPointF &fraction);
    void setNukeInFront(bool inFront);
    void setRegistration(ShortcutAction action, Registration registration);
    void setAccessibilityGranted(bool granted);

    void setDiskCheckMinutes(int minutes);
    // Un disco nuevo toma la unidad y el valor del ultimo de la lista (D-07: "vigilar varios por el
    // mismo peso" es agregarlos y listo); sin ninguno, 50 GB.
    void addDiskWatch(const QString &root, const QString &name);
    void removeDiskWatch(const QString &root);
    void setDiskThreshold(const QString &root, int value, DiskWatch::Unit unit);
    // Lecturas: `listedAll` reemplaza todo (listado completo de discos locales); si no, solo toca las
    // raices de `queried` (un chequeo que lee nada mas los vigilados, para no despertar otros discos).
    void setDriveReadings(const QList<DriveInfo> &readings, const QStringList &queried, bool listedAll,
                          const QDateTime &checkedAt);

    static QString actionTitle(ShortcutAction action);

signals:
    void changed();

private:
    void writeValue(const QString &key, const QVariant &value);
    void writeDiskWatches();

    Persistence m_persistence;
    bool m_enabled = true;
    bool m_checkUpdatesAtStartup = true;
    Shortcut m_addKeyframe = Shortcut::defaultAddKeyframe();
    Shortcut m_frameDopeSheet = Shortcut::defaultFrameDopeSheet();
    bool m_hasDopeSheetSpot = false;
    QPointF m_dopeSheetSpot;
    int m_diskCheckMinutes = DiskSpace::kDefaultIntervalMinutes;
    QList<DiskWatch> m_diskWatches;

    bool m_nukeInFront = false;
    Registration m_addKeyframeRegistration = Registration::Idle;
    Registration m_frameRegistration = Registration::Idle;
    bool m_accessibilityGranted = true;
    QHash<QString, DriveInfo> m_drives;
    QDateTime m_lastDiskCheck;
};

#endif // MIGHTYTOOLS_APPSTATE_H
