#ifndef MIGHTYTOOLS_DESKTOPICONSMODULE_H
#define MIGHTYTOOLS_DESKTOPICONSMODULE_H

#include "app/Module.h"
#include "modules/desktopicons/DesktopSnapshots.h"
#include "ui/HelpSection.h"

#include <QDateTime>

class QTimer;

// Desktop Icons (solo Windows): guarda y restaura la posicion de los iconos del escritorio. Un JSON por
// snapshot en <datos de la app>/desktop_icons/. Prendida: un timer cada 15 min solo si "Daily snapshot"
// esta prendido. En una corrida automatizada no lee ni escribe ese disco ni mueve iconos.
class DesktopIconsModule : public Module
{
    Q_OBJECT

public:
    // Resultado de la ultima restauracion (o deshacer). Guardar un snapshot no lo toca: su error va aparte.
    struct Result
    {
        bool visible = false;
        bool ok = false;
        int restored = 0;  ///< los que quedaron exacto en su lugar (releidos despues de mover)
        int notPlaced = 0; ///< los que el Explorador dejo en otro lado
        QStringList missing;
        QString error;
        QString undoId; ///< el snapshot que deshace esta restauracion
    };

    explicit DesktopIconsModule(ModuleContext &context);
    ~DesktopIconsModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;

    // Del mas nuevo al mas viejo.
    QList<DesktopSnapshot> snapshots() const;
    // Pantalla de ahora, para avisar en las filas de otra resolucion.
    QSize currentScreen() const;
    const Result &lastResult() const { return m_result; }
    // El error del ultimo "Save snapshot" (vacio si salio bien).
    QString saveError() const { return m_saveError; }
    // "Skipped today: the screen is ... (usually ...)" si hoy el automatico se salteo por la resolucion.
    QString skipNotice() const;
    QString captureState() const { return m_captureState; }

    bool dailyEnabled() const;
    void setDailyEnabled(bool enabled);
    bool saveManual(const QString &name);
    bool restore(const QString &id);
    bool remove(const QString &id);

    // Carpeta de los snapshots (vacia en una corrida automatizada o en captura: todo queda en memoria).
    static QString storageFolder();

signals:
    void snapshotsChanged();

private:
    void tick();
    void startDailyTimer();
    void loadFromDisk();
    bool persist(const DesktopSnapshot &snapshot);
    void erase(const QString &id);
    void prune();
    QString newId(SnapshotKind kind, const QDateTime &when) const;
    DesktopSnapshot makeSnapshot(SnapshotKind kind, const QString &name, const DesktopIconView::Layout &layout) const;
    void announce();

    bool m_started = false;
    QString m_folder;
    QList<DesktopSnapshot> m_snapshots;
    QTimer *m_timer = nullptr;
    QDateTime m_awakeSince;
    qint64 m_lastWallMs = 0;
    qint64 m_lastAwakeMs = 0;
    Result m_result;
    QString m_saveError;
    QString m_captureState;
    QSize m_captureScreen;
};

ModuleDescriptor desktopIconsDescriptor();
HelpSection desktopIconsHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_DESKTOPICONSMODULE_H
