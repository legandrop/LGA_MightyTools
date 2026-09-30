#ifndef MIGHTYTOOLS_DISKSPACE_H
#define MIGHTYTOOLS_DISKSPACE_H

#include <QDateTime>
#include <QList>
#include <QString>

// Chequeo de espacio libre: tipos y reglas puras, sin sistema de archivos ni timers. Los usan
// DiskState (lo que se guarda), DiskMonitor (cuando avisar), la tarjeta "Watched drives" y el self-test.

// Un disco local tal como se leyo del sistema (platform/LocalDrives).
struct DriveInfo
{
    QString root;       ///< raiz del volumen, la clave de todo: "C:/" en Windows, "/Volumes/Cache" en mac
    QString label;      ///< lo que va en el keycap: "C:" en Windows, el nombre del volumen en mac
    QString name;       ///< nombre del volumen al lado del keycap ("Windows", "Cache"); puede ir vacio
    qint64 totalBytes = 0;
    qint64 freeBytes = 0;

    bool operator==(const DriveInfo &other) const
    {
        return root == other.root && label == other.label && name == other.name && totalBytes == other.totalBytes
               && freeBytes == other.freeBytes;
    }
    bool operator!=(const DriveInfo &other) const { return !(*this == other); }
};

// Un disco que el usuario pidio vigilar, con su umbral propio.
struct DiskWatch
{
    enum class Unit { GB, Percent };

    QString root;
    int value = 50;
    Unit unit = Unit::GB;
    // Nombre visto la ultima vez: se muestra cuando el disco esta desenchufado.
    QString name;

    bool operator==(const DiskWatch &other) const
    {
        return root == other.root && value == other.value && unit == other.unit && name == other.name;
    }
    bool operator!=(const DiskWatch &other) const { return !(*this == other); }
};

namespace DiskSpace {

// Umbral de un disco nuevo si todavia no hay ninguno vigilado (D-07).
constexpr int kDefaultGb = 50;
// Al pasar un disco a porcentaje, el valor con el que arranca.
constexpr int kDefaultPercent = 10;
constexpr int kMaxGb = 100000;
constexpr int kMaxPercent = 99;

// El chequeo es fijo, cada 15 min, para todos los discos (pedido de Lega, 2026-09-30).
constexpr int kCheckMinutes = 15;

// Mientras un disco sigue bajo, el aviso se repite cada "Remind me every" (uno solo para todos los
// discos). El minimo es el chequeo: no hay recordatorio mas seguido que la lectura.
constexpr int kDefaultRemindMinutes = 15;
const QList<int> &remindChoices();
bool isValidRemind(int minutes);
QString intervalText(int minutes); ///< "15 min", "1 hour", "6 hours"

// Un recordatorio vence con este margen: un tick de 15 min que llega unos ms antes de los 15 min
// exactos no corre el aviso al tick siguiente.
constexpr qint64 kDueToleranceSeconds = 60;

int clampValue(int value, DiskWatch::Unit unit);
QString unitToString(DiskWatch::Unit unit);                       ///< "GB" / "%" (lo que guarda el .ini)
bool unitFromString(const QString &text, DiskWatch::Unit *unit); ///< false si no es ninguno de los dos

// Bytes por debajo de los cuales el disco cuenta como bajo. En GB se usan GiB (1024^3), los mismos
// "GB" que muestra el Explorador de Windows.
qint64 thresholdBytes(const DiskWatch &watch, qint64 totalBytes);
bool isLow(const DiskWatch &watch, const DriveInfo &drive);

QString formatBytes(qint64 bytes);             ///< "182 GB", "1.82 TB", "512 MB"
QString thresholdText(const DiskWatch &watch); ///< "100 GB", "15%"
// Keycap de un disco que no esta enchufado: la letra en Windows, el nombre guardado en mac.
QString labelForRoot(const QString &root, const QString &storedName);

// Lo que se recuerda de cada disco vigilado entre chequeos, para no repetir el aviso en cada uno.
struct AlertState
{
    bool wasLow = false;
    QDateTime lastNotified;
    // "Remind me again in" del aviso: el proximo recordatorio de ESTE disco, en vez de lastNotified +
    // remindMinutes. Vale para un solo recordatorio: se limpia al avisar o al dejar de estar bajo.
    QDateTime snoozedUntil;
};

// Avisar si el disco esta bajo y: acaba de cruzar el umbral, nunca se aviso, o vencio el proximo
// recordatorio (snoozedUntil si hay, si no lastNotified + remindMinutes). Si sube y vuelve a bajar,
// avisa de nuevo al cruzar.
bool shouldNotify(bool lowNow, const AlertState &state, const QDateTime &now, int remindMinutes);

} // namespace DiskSpace

#endif // MIGHTYTOOLS_DISKSPACE_H
