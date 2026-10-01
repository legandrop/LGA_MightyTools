#ifndef MIGHTYTOOLS_CLEANUPJOB_H
#define MIGHTYTOOLS_CLEANUPJOB_H

#include "modules/diskspace/cleanup/CleanupModel.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// El borrado: un hilo propio que ejecuta una lista de pedidos, uno por uno, y deja el resultado de
// cada uno. Como ScanEngine, no manda senales (la ventana mira progress() con su timer) y nadie lo
// espera: cancelar o destruirlo levanta la bandera y el hilo termina solo apenas vuelve la llamada
// del sistema en curso (vaciar la Papelera o mover una carpeta grande no se pueden cortar).
//
// Reglas que valen pase lo que pase (auditadas 2026-09-30):
//  - Cada pedido se vuelve a validar al ejecutarlo: ruta REAL (sin enlaces ni nombres cortos),
//    DeleteGuard, programas que bloquean, y la guarda de corrida automatizada.
//  - Nunca se sigue un enlace: un junction o un symlink se borra como enlace.
//  - Lo que esta en uso, sin permiso o es de la nube sin bajar se saltea y se cuenta; nunca se fuerza
//    ni se deja para el reinicio.
//  - Si la Papelera falla, el pedido falla: nunca se pasa a un borrado definitivo.
class CleanupJob
{
public:
    struct Request
    {
        int id = 0; ///< lo elige quien pide; vuelve en el resultado
        QString label;
        QString path;
        Cleanup::Action action = Cleanup::Action::Entire;
        int minAgeDays = 0;
        bool toTrash = false; ///< solo con Action::Entire
        QStringList blockers;
        QString blockerLabel;
    };

    struct Outcome
    {
        int id = 0;
        QString label;
        QString path; ///< la ruta real sobre la que se trabajo
        qint64 freedBytes = 0;
        qint64 deletedFiles = 0;
        qint64 skippedFiles = 0; ///< en uso, sin permiso o de la nube
        qint64 skippedBytes = 0;
        QStringList skippedSample; ///< primeras rutas salteadas
        QString blockedBy;         ///< un programa que bloquea corria: no se toco nada
        DeleteGuard::Verdict refused = DeleteGuard::Verdict::Ok; ///< distinto de Ok: no se toco nada
        QString error;             ///< la Papelera no lo acepto
        bool trashed = false;      ///< fue a la Papelera
        bool removedEntirely = false; ///< Action::Entire y no quedo nada
        bool touched() const { return deletedFiles > 0 || removedEntirely || trashed; }
    };

    struct Progress
    {
        bool running = false;
        int done = 0;
        int total = 0;
        qint64 files = 0;
        qint64 bytes = 0;
        QString current;
    };

    CleanupJob();
    ~CleanupJob();
    CleanupJob(const CleanupJob &) = delete;
    CleanupJob &operator=(const CleanupJob &) = delete;

    // false si ya hay un trabajo en curso.
    bool start(const QList<Request> &requests, const DeleteGuard &guard);
    void cancel();
    bool isRunning() const;
    Progress progress() const;
    QList<Outcome> outcomes() const;

    // ---- Medicion, con el mismo criterio que el borrado. Solo lectura.
    struct Measure
    {
        qint64 bytes = 0;
        qint64 files = 0;
        qint64 newest = 0;
        qint64 keptBytes = 0; ///< OldChildren: lo que no se borraria por ser nuevo
    };
    // Todo lo que cuelga de una carpeta (sin seguir enlaces ni entrar en carpetas de nube sin bajar).
    static Measure measureTree(const QString &dir);
    // Los hijos directos con `minAgeDays` o mas de antiguedad (lo mas nuevo entre creacion y
    // modificacion, de todo lo que tienen adentro).
    static Measure measureOldChildren(const QString &dir, int minAgeDays, qint64 nowSecs);

private:
    struct Shared;
    std::shared_ptr<Shared> m_shared;
};

#endif // MIGHTYTOOLS_CLEANUPJOB_H
