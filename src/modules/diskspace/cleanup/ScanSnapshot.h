#ifndef MIGHTYTOOLS_SCANSNAPSHOT_H
#define MIGHTYTOOLS_SCANSNAPSHOT_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

class ScanTree;

// Resumen de un escaneo para "What changed": las carpetas de 100 MB o mas con su peso. Pesa unos
// cientos de KB. Se guarda uno por volumen, mas el anterior, en la carpeta de ajustes de la app
// (scans\), que el desinstalador borra entera. En una corrida automatizada no se escribe nada.
struct ScanSnapshot
{
    static constexpr qint64 kMinBytes = 100LL * 1024 * 1024;

    QString root;
    QDateTime takenAt;
    qint64 usedBytes = 0; ///< lo usado del volumen segun el sistema
    QHash<QString, qint64> dirs; ///< ruta -> bytes

    bool isValid() const { return takenAt.isValid() && !root.isEmpty(); }

    // Del arbol de un escaneo completo. El llamador tiene el candado del motor.
    static ScanSnapshot fromTree(const ScanTree &tree, qint64 usedBytes, const QDateTime &now);

    // Una carpeta cuyo peso cambio.
    struct Change
    {
        QString path;
        qint64 delta = 0;
        qint64 bytes = 0; ///< lo que pesa ahora
    };
    // Lo que cambio `minDelta` o mas entre `before` y `after`, de mayor a menor crecimiento. De cada
    // rama se lista la carpeta mas profunda que explica el cambio (no cada una de sus madres).
    static QList<Change> diff(const ScanSnapshot &before, const ScanSnapshot &after, qint64 minDelta);

    // ---- Archivo. `dir` vacia: la carpeta de ajustes de la app.
    static QString storageDir();
    // Guarda `snapshot` como el actual de su volumen. Con `rotate`, el que habia pasa a "anterior"; sin
    // el, se pisa (dos escaneos seguidos no corren la referencia con la que se compara).
    static bool store(const ScanSnapshot &snapshot, bool rotate = true, const QString &dir = QString());
    // El resumen anterior al ultimo guardado (con el que se compara un escaneo recien hecho).
    static ScanSnapshot loadPrevious(const QString &root, const QString &dir = QString());
    static ScanSnapshot loadCurrent(const QString &root, const QString &dir = QString());
};

#endif // MIGHTYTOOLS_SCANSNAPSHOT_H
