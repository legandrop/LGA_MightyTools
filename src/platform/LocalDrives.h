#ifndef MIGHTYTOOLS_LOCALDRIVES_H
#define MIGHTYTOOLS_LOCALDRIVES_H

#include "modules/diskspace/DiskSpace.h"

#include <QList>
#include <QString>

class QStorageInfo;

// Discos locales y su espacio libre, para el chequeo de espacio (DiskMonitor).
// La lectura es comun (QStorageInfo, LocalDrivesCommon.cpp); lo que cambia por sistema es que
// cuenta como disco local y como se lo nombra:
//  - Windows (platform/win/LocalDrivesWin.cpp): GetDriveType fijo o removible (D-07); red, CD y RAM
//    disk quedan afuera. Keycap con la letra ("C:") y el nombre del volumen al lado.
//  - macOS   (platform/mac/LocalDrivesMac.cpp): "/" y los volumenes de /Volumes que no son de red ni
//    de solo lectura (una imagen .dmg montada no cuenta). Keycap con el nombre del volumen.
namespace LocalDrives {

// Todos los discos locales listos para leer, ordenados por raiz.
QList<DriveInfo> list();
// Un disco puntual por su raiz. false si no esta enchufado o no se pudo leer. No vuelve a
// preguntar si es local: la raiz ya paso por list() cuando el usuario lo agrego.
bool query(const QString &root, DriveInfo *drive);

// ---- Por plataforma
bool isLocalVolume(const QStorageInfo &storage);
void describe(const QStorageInfo &storage, DriveInfo *drive); // label y name

} // namespace LocalDrives

#endif // MIGHTYTOOLS_LOCALDRIVES_H
