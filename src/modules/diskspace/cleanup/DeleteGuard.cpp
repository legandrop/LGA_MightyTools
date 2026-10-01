#include "modules/diskspace/cleanup/DeleteGuard.h"

#include "platform/FileSystemOps.h"
#include "platform/SystemPaths.h"

#include <QDir>

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

} // namespace

QString DeleteGuard::clean(const QString &path)
{
    QString text = QDir::toNativeSeparators(QDir::cleanPath(path));
    const QChar separator = QDir::separator();
#ifdef Q_OS_WIN
    // "C:" suelto es la unidad, no la carpeta actual de esa unidad.
    if (text.size() == 2 && text.at(1) == QLatin1Char(':')) {
        text += separator;
    }
    const int rootLength = 3;
#else
    const int rootLength = 1;
#endif
    while (text.size() > rootLength && text.endsWith(separator)) {
        text.chop(1);
    }
    return text;
}

bool DeleteGuard::samePath(const QString &a, const QString &b)
{
    return clean(a).compare(clean(b), kPathCase) == 0;
}

bool DeleteGuard::isInside(const QString &path, const QString &ancestor)
{
    const QString child = clean(path);
    QString parent = clean(ancestor);
    if (parent.isEmpty() || child.size() <= parent.size()) {
        return false;
    }
    const QChar separator = QDir::separator();
    if (!parent.endsWith(separator)) {
        parent += separator; // limite de carpeta: "C:\Users2" no esta dentro de "C:\Users"
    }
    return child.startsWith(parent, kPathCase);
}

DeleteGuard DeleteGuard::forVolume(const QString &volumeRoot)
{
    DeleteGuard guard;
    const QString real = FileSystemOps::canonicalPath(volumeRoot);
    guard.volumeRoot = clean(real.isEmpty() ? volumeRoot : real);
    guard.protectedFolders = SystemPaths::protectedFolders();
    guard.protectedTrees = SystemPaths::protectedTrees();
    guard.cloudFolders = SystemPaths::cloudFolders();
    // Del propio volumen: la Papelera (se vacia por su API) y lo que guarda el sistema.
    const QChar separator = QDir::separator();
    QString base = guard.volumeRoot;
    if (!base.endsWith(separator)) {
        base += separator;
    }
    // Tambien los archivos que Windows tiene en la raiz (memoria virtual, hibernacion, arranque).
    for (const char *name : {"$Recycle.Bin", "$RECYCLE.BIN", "System Volume Information", "Recovery", ".Trashes", "pagefile.sys",
                             "hiberfil.sys", "swapfile.sys", "DumpStack.log.tmp", "DumpStack.log", "bootmgr", "BOOTNXT", "Boot",
                             "EFI", "$WinREAgent"}) {
        guard.protectedTrees.append(base + QLatin1String(name));
    }
    return guard;
}

DeleteGuard::Verdict DeleteGuard::check(const QString &path, Scope scope, bool toTrash) const
{
    const QString target = clean(path);
    if (target.isEmpty() || QDir::isRelativePath(target)) {
        return Verdict::Missing;
    }
    if (samePath(target, volumeRoot)) {
        return Verdict::Protected;
    }
    if (!isInside(target, volumeRoot)) {
        return Verdict::OutsideVolume;
    }
    for (const QString &tree : protectedTrees) {
        if (samePath(target, tree) || isInside(target, tree)) {
            return Verdict::ProtectedTree;
        }
        if (isInside(tree, target)) {
            return Verdict::ContainsProtected;
        }
    }
    // Con Scope::Children la carpeta misma no se toca y cada hijo pasa despues por aca como Entire: la
    // carpeta temporal del usuario esta protegida y aun asi se le pueden sacar los hijos viejos. Lo que
    // no puede es CONTENER algo protegido: si la "carpeta temporal" resulta ser el perfil (Windows
    // devuelve eso cuando faltan TMP y TEMP), no se le saca nada.
    for (const QString &folder : protectedFolders) {
        // Vaciar una carpeta protegida es tan grave como borrarla: tampoco "solo el contenido".
        if (scope != Scope::Children && samePath(target, folder)) {
            return Verdict::Protected;
        }
        if (isInside(folder, target)) {
            return Verdict::ContainsProtected;
        }
    }
    for (const QString &cloud : cloudFolders) {
        if (isInside(cloud, target) || samePath(target, cloud)) {
            return Verdict::ContainsProtected;
        }
        // Dentro de una carpeta de nube lo borrado se borra en todos los dispositivos: solo a la
        // Papelera, que es lo que haria el usuario desde el Explorador.
        if (isInside(target, cloud) && !toTrash) {
            return Verdict::CloudFolder;
        }
    }
    if (toTrash) {
        // La Shell normaliza los nombres: "informe." o "informe " pueden terminar apuntando a "informe".
        const QStringList parts = target.split(QDir::separator(), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))) {
                return Verdict::BadNameForTrash;
            }
        }
    }
    return Verdict::Ok;
}

bool DeleteGuard::isViewOnly(const QString &path) const
{
    const Verdict verdict = check(path, Scope::Entire, true);
    return verdict != Verdict::Ok && verdict != Verdict::BadNameForTrash;
}
