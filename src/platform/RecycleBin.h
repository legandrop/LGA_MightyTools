#ifndef MIGHTYTOOLS_RECYCLEBIN_H
#define MIGHTYTOOLS_RECYCLEBIN_H

#include <QString>

// La Papelera de un volumen: cuanto pesa, vaciarla y mandar algo ahi.
//  - Windows (platform/win/RecycleBinWin.cpp): SHQueryRecycleBinW / SHEmptyRecycleBinW, sin carteles ni
//    sonido. Mandar a la Papelera: QFile::moveToTrash (IFileOperation), que ABORTA si Windows fuera a
//    borrar definitivo porque el item no entra en la Papelera: nunca se degrada a un borrado definitivo.
//  - macOS (platform/mac/RecycleBinMac.cpp): deshabilitado (sin probar); todo responde que no.
//
// En una corrida automatizada (core/AutomatedRun.h) vaciar y mandar a la Papelera son inertes: anotan
// lo que harian y devuelven false. Preguntar el tamano es solo lectura.
namespace RecycleBin {

struct Info
{
    bool ok = false;
    qint64 bytes = 0;
    qint64 items = 0;
};

// `volumeRoot`: "C:/" o "C:\".
Info query(const QString &volumeRoot);
bool empty(const QString &volumeRoot);
// Un archivo o una carpeta entera. `error` dice por que no se pudo.
bool moveToTrash(const QString &path, QString *error);

} // namespace RecycleBin

#endif // MIGHTYTOOLS_RECYCLEBIN_H
