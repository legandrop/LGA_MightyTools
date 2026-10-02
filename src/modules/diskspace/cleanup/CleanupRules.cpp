#include "modules/diskspace/cleanup/CleanupRules.h"

#include "core/I18n.h"
#include "modules/diskspace/DiskSpace.h"
#include "modules/diskspace/cleanup/CleanupJob.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanEngine.h"
#include "platform/FileSystemOps.h"
#include "platform/RecycleBin.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSet>

#include <algorithm>

namespace {

// Windows y macOS (APFS y HFS+ por defecto) no distinguen mayusculas en las rutas. En un volumen de mac
// que si las distingue, comparar sin distinguir solo hace las guardas mas amplias.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

// Hasta donde se busca una carpeta por nombre dentro de la carpeta de una regla del usuario.
constexpr int kFolderRuleDepth = 4;
constexpr qint64 kMinTaggedBytes = 10 * 1024 * 1024;

QString join(const QString &dir, const QString &name)
{
    return dir.endsWith(QDir::separator()) ? dir + name : dir + QDir::separator() + name;
}

// Una carpeta de cache de uv se reconoce por sus subcarpetas con version ("archive-v0", "wheels-v5").
bool looksLikeUvCache(const QString &dir)
{
    const QStringList children = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    int versioned = 0;
    for (const QString &name : children) {
        for (const char *prefix : {"archive-v", "wheels-v", "simple-v", "builds-v", "interpreter-v", "sdists-v"}) {
            if (name.startsWith(QLatin1String(prefix))) {
                ++versioned;
                break;
            }
        }
    }
    return versioned >= 2;
}

// Ultimos dos tramos de una ruta: "LGA_Flama\target".
QString shortName(const QString &path)
{
    const QStringList parts = path.split(QDir::separator(), Qt::SkipEmptyParts);
    if (parts.size() <= 2) {
        return path;
    }
    return parts.at(parts.size() - 2) + QDir::separator() + parts.last();
}

void measureItem(Cleanup::Item &item, const ScanTree &tree, const QString &volumeRoot, qint64 now)
{
    item.bytes = 0;
    item.files = 0;
    item.newest = 0;
    item.keptBytes = 0;
    for (const Cleanup::Target &target : item.targets) {
        switch (target.action) {
        case Cleanup::Action::RecycleBin: {
            const RecycleBin::Info info = RecycleBin::query(volumeRoot);
            if (info.ok) {
                item.bytes += info.bytes;
                item.files += info.items;
            }
            break;
        }
        case Cleanup::Action::OldChildren: {
            const CleanupJob::Measure measure = CleanupJob::measureOldChildren(target.path, target.minAgeDays, now);
            item.bytes += measure.bytes;
            item.files += measure.files;
            item.keptBytes += measure.keptBytes;
            item.newest = qMax(item.newest, measure.newest);
            break;
        }
        case Cleanup::Action::Contents:
        case Cleanup::Action::Entire: {
            const ScanTree::Index node = tree.find(target.path);
            if (node != ScanTree::kNone && tree.isAlive(node)) {
                const ScanTree::Node &n = tree.node(node);
                item.bytes += qint64(n.bytes);
                item.files += n.files;
                item.newest = qMax(item.newest, n.newest);
            } else {
                // Un archivo suelto (el volcado de memoria): no tiene nodo en el arbol.
                const QFileInfo info(target.path);
                if (info.isFile()) {
                    item.bytes += info.size();
                    item.files += 1;
                    item.newest = qMax(item.newest, info.lastModified().toSecsSinceEpoch());
                }
            }
            break;
        }
        }
    }
    item.measured = true;
}

// Carpetas llamadas `match` debajo de `base`, hasta cierta profundidad y sin entrar en las que encuentra.
void findNamed(const ScanTree &tree, ScanTree::Index base, const QString &match, int depth, QList<ScanTree::Index> &out)
{
    for (const ScanTree::Index child : tree.children(base)) {
        if (tree.name(child).compare(match, kPathCase) == 0) {
            out.append(child);
        } else if (depth > 1) {
            findNamed(tree, child, match, depth - 1, out);
        }
    }
}

bool covers(const QList<Cleanup::Category> &categories, const QString &path)
{
    for (const Cleanup::Category &category : categories) {
        for (const Cleanup::Item &item : category.items) {
            for (const Cleanup::Target &target : item.targets) {
                if (target.action == Cleanup::Action::RecycleBin) {
                    continue;
                }
                if (DeleteGuard::samePath(path, target.path) || DeleteGuard::isInside(path, target.path)
                    || DeleteGuard::isInside(target.path, path)) {
                    return true;
                }
            }
        }
    }
    return false;
}

Cleanup::Category *findCategory(QList<Cleanup::Category> &categories, const QString &id)
{
    for (Cleanup::Category &category : categories) {
        if (category.id == id) {
            return &category;
        }
    }
    return nullptr;
}

} // namespace

namespace CleanupRules {

QString usableDir(const QString &path, const Context &context)
{
    if (path.isEmpty()) {
        return QString();
    }
    // La ruta real: si algun tramo es un enlace a otro disco, queda fuera del volumen y no se ofrece.
    const QString real = FileSystemOps::canonicalPath(path);
    if (real.isEmpty() || FileSystemOps::kind(real) != FileSystemOps::Kind::Dir) {
        return QString();
    }
    if (!DeleteGuard::isInside(real, context.volumeRoot)) {
        return QString();
    }
#ifdef Q_OS_MACOS
    // En mac todo cuelga de "/": otro disco montado en /Volumes tambien "esta adentro". Se compara el
    // volumen de verdad.
    if (FileSystemOps::identity(real).volume != FileSystemOps::identity(context.volumeRoot).volume) {
        return QString();
    }
#endif
    return real;
}

Cleanup::Item makeItem(const QString &name, const QString &shownPath, const QStringList &dirs, Cleanup::Action action,
                       const Context &context)
{
    Cleanup::Item item;
    item.name = name;
    for (const QString &dir : dirs) {
        const QString real = usableDir(dir, context);
        if (real.isEmpty()) {
            continue;
        }
        Cleanup::Target target;
        target.path = real;
        target.action = action;
        item.targets.append(target);
    }
    if (!item.targets.isEmpty()) {
        const QString shown = usableDir(shownPath, context);
        item.path = shown.isEmpty() ? item.targets.first().path : shown;
    }
    return item;
}

bool hasCacheTagSignature(const QString &dir)
{
    QFile file(join(dir, QStringLiteral("CACHEDIR.TAG")));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.read(43) == QByteArrayLiteral("Signature: 8a477f597d28d172789f06886806bc55");
}

QList<Cleanup::Category> build(const Context &context)
{
    QList<Cleanup::Category> categories = systemCategories(context);
    // La misma carpeta puede aparecer por dos rutas (una app empaquetada la muestra tambien en
    // AppData\Roaming): se queda la primera.
    QList<FileSystemOps::Identity> seen;
    for (Cleanup::Category &category : categories) {
        QList<Cleanup::Item> kept;
        for (const Cleanup::Item &item : category.items) {
            if (item.targets.isEmpty()) {
                // Un renglon que solo informa puede no tener nada que medir ni que borrar.
                if (category.info) {
                    kept.append(item);
                }
                continue;
            }
            const Cleanup::Target &first = item.targets.first();
            if (first.action != Cleanup::Action::RecycleBin) {
                const FileSystemOps::Identity id = FileSystemOps::identity(first.path);
                if (id.valid && seen.contains(id)) {
                    continue;
                }
                if (id.valid) {
                    seen.append(id);
                }
            }
            kept.append(item);
        }
        category.items = kept;
    }
    return categories;
}

QList<Cleanup::Category> refresh(const Context &context, ScanEngine &engine, const QList<Cleanup::Category> &previous)
{
    QList<Cleanup::Category> categories = build(context);
    const DeleteGuard guard = DeleteGuard::forVolume(context.volumeRoot);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QList<ScanTree::Index> tagged = engine.cacheTaggedDirs();

    {
        const auto lock = engine.lock();
        const ScanTree &tree = engine.tree();

        // ---- Reglas de carpetas del usuario.
        for (int i = 0; i < context.folderRules.size(); ++i) {
            const Cleanup::FolderRule &rule = context.folderRules.at(i);
            const QString base = usableDir(rule.base, context);
            if (base.isEmpty()) {
                continue;
            }
            Cleanup::Category category;
            category.id = QStringLiteral("rule:%1").arg(i);
            category.group = Cleanup::Group::Yours;
            const QString folderName = QFileInfo(base).fileName();
            if (rule.match.isEmpty()) {
                category.single = true;
                category.title = rule.name.isEmpty() ? folderName : rule.name;
                category.caption = base;
                Cleanup::Item item;
                item.name = folderName;
                item.path = base;
                item.targets.append(Cleanup::Target{base, Cleanup::Action::Entire, 0});
                category.items.append(item);
            } else {
                const ScanTree::Index node = tree.find(base);
                QList<ScanTree::Index> found;
                if (node != ScanTree::kNone) {
                    findNamed(tree, node, rule.match, kFolderRuleDepth, found);
                }
                category.title = rule.name.isEmpty() ? I18n::tr("Folders named %1").arg(rule.match) : rule.name;
                category.caption = found.size() == 1 ? I18n::tr("1 folder named %1 in %2.").arg(rule.match, base)
                                                     : I18n::tr("%1 folders named %2 in %3.").arg(found.size()).arg(rule.match, base);
                for (const ScanTree::Index index : found) {
                    const QString path = tree.path(index);
                    Cleanup::Item item;
                    // El nombre es lo que hay entre la carpeta de la regla y la coincidencia.
                    QString inner = QFileInfo(path).absolutePath();
                    inner = QDir::toNativeSeparators(inner).mid(base.size());
                    while (inner.startsWith(QDir::separator())) {
                        inner.remove(0, 1);
                    }
                    item.name = inner.isEmpty() ? folderName : inner;
                    item.path = path;
                    item.targets.append(Cleanup::Target{path, Cleanup::Action::Entire, 0});
                    category.items.append(item);
                }
            }
            categories.append(category);
        }

        // ---- Carpetas que su programa marco como cache (CACHEDIR.TAG).
        QStringList taggedPaths;
        for (const ScanTree::Index index : tagged) {
            taggedPaths.append(tree.path(index));
        }
        // De afuera hacia adentro: una marcada dentro de otra marcada ya viaja con la de afuera.
        std::sort(taggedPaths.begin(), taggedPaths.end(), [](const QString &a, const QString &b) { return a.size() < b.size(); });
        QStringList accepted;
        Cleanup::Category other;
        other.id = QStringLiteral("tagged");
        other.group = Cleanup::Group::Yours;
        other.title = I18n::tr("Folders marked as cache");
        other.caption = I18n::tr("Their programs flag them as cache (CACHEDIR.TAG) and rebuild them when needed.");
        for (const QString &path : taggedPaths) {
            bool nested = false;
            for (const QString &outer : accepted) {
                if (DeleteGuard::isInside(path, outer)) {
                    nested = true;
                    break;
                }
            }
            if (nested || covers(categories, path)) {
                continue;
            }
            if (guard.check(path, DeleteGuard::Scope::Entire, false) != DeleteGuard::Verdict::Ok) {
                continue;
            }
            // uv marca tambien cada entorno virtual: eso no es cache, es el entorno de un proyecto.
            if (QFileInfo::exists(join(path, QStringLiteral("pyvenv.cfg"))) || !hasCacheTagSignature(path)) {
                continue;
            }
            accepted.append(path);
            Cleanup::Item item;
            item.path = path;
            if (looksLikeUvCache(path)) {
                // Una cache de uv fuera de su lugar de fabrica (la de UV_CACHE_DIR ya entro por su regla):
                // se muestra con las demas caches de Python, pero SIN tildar. Puede ser la cache privada
                // del runtime de otra herramienta, y eso lo sabe el usuario.
                if (Cleanup::Category *python = findCategory(categories, QStringLiteral("python"))) {
                    item.name = QStringLiteral("uv cache");
                    item.optional = true;
                    item.targets.append(Cleanup::Target{path, Cleanup::Action::Contents, 0});
                    item.blockers = {QStringLiteral("uv"), QStringLiteral("uvx")};
                    item.blockerLabel = QStringLiteral("uv");
                    python->items.append(item);
                    continue;
                }
            }
            item.name = shortName(path);
            item.targets.append(Cleanup::Target{path, Cleanup::Action::Entire, 0});
            other.items.append(item);
        }
        if (!other.items.isEmpty()) {
            categories.append(other);
        }

        // ---- Medir.
        for (Cleanup::Category &category : categories) {
            for (Cleanup::Item &item : category.items) {
                if (category.measurable) {
                    measureItem(item, tree, context.volumeRoot, now);
                }
            }
            // Dos leyendas llevan lo medido: cuantas cosas hay en la Papelera, y cuanto de la carpeta
            // temporal se queda por nuevo o por estar en uso.
            if (category.items.isEmpty()) {
                continue;
            }
            const Cleanup::Item &first = category.items.first();
            if (category.id == QLatin1String("bin") && first.files > 0) {
                category.caption = first.files == 1 ? I18n::tr("1 item already deleted once.")
                                                    : I18n::tr("%1 items already deleted once.")
                                                          .arg(QLocale(QLocale::English).toString(first.files));
            } else if (category.id == QLatin1String("temp") && first.keptBytes > 0) {
                category.caption += QLatin1Char(' ') + I18n::tr("%1 newer or in use are left alone.").arg(DiskSpace::formatSize(first.keptBytes));
            }
        }
    }

    // ---- Programas que bloquean, lo tildado y el orden.
    const QSet<QString> programs = SystemPaths::runningPrograms();
    QHash<QString, bool> wasChecked;
    for (const Cleanup::Category &category : previous) {
        for (const Cleanup::Item &item : category.items) {
            if (item.measured) {
                wasChecked.insert(category.id + QLatin1Char('|') + item.path.toLower(), item.checked);
            }
        }
    }
    QList<Cleanup::Category> result;
    for (Cleanup::Category &category : categories) {
        QList<Cleanup::Item> kept;
        for (Cleanup::Item &item : category.items) {
            // Lo que no pesa nada no se muestra (salvo lo informativo).
            if (category.measurable && item.bytes <= 0 && !category.info) {
                continue;
            }
            // Una carpeta marcada como cache que pesa poco es ruido en la lista.
            if (category.id == QLatin1String("tagged") && item.bytes < kMinTaggedBytes) {
                continue;
            }
            // Lo que el borrado rechazaria (la carpeta desde la que corre esta app, por ejemplo) no se ofrece.
            if (!category.info) {
                bool refused = false;
                for (const Cleanup::Target &target : item.targets) {
                    if (target.action == Cleanup::Action::RecycleBin) {
                        continue;
                    }
                    DeleteGuard::Scope scope = DeleteGuard::Scope::Entire;
                    if (target.action == Cleanup::Action::Contents) {
                        scope = DeleteGuard::Scope::Contents;
                    } else if (target.action == Cleanup::Action::OldChildren) {
                        scope = DeleteGuard::Scope::Children;
                    }
                    if (guard.check(target.path, scope, false) != DeleteGuard::Verdict::Ok) {
                        refused = true;
                        break;
                    }
                }
                if (refused) {
                    continue;
                }
            }
            item.blocked = false;
            for (const QString &blocker : item.blockers) {
                if (programs.contains(blocker)) {
                    item.blocked = true;
                    break;
                }
            }
            const QString key = category.id + QLatin1Char('|') + item.path.toLower();
            const bool byDefault = category.group == Cleanup::Group::Safe && !item.optional;
            item.checked = !category.info && !item.blocked && wasChecked.value(key, byDefault);
            kept.append(item);
        }
        std::stable_sort(kept.begin(), kept.end(), [](const Cleanup::Item &a, const Cleanup::Item &b) { return a.bytes > b.bytes; });
        category.items = kept;
        if (!category.items.isEmpty()) {
            result.append(category);
        }
    }
    return result;
}

qint64 selectedBytes(const QList<Cleanup::Category> &categories)
{
    qint64 total = 0;
    for (const Cleanup::Category &category : categories) {
        if (category.info) {
            continue;
        }
        for (const Cleanup::Item &item : category.items) {
            if (item.checked && !item.blocked) {
                total += item.bytes;
            }
        }
    }
    return total;
}

int checkState(const Cleanup::Category &category)
{
    int checkable = 0;
    int checked = 0;
    for (const Cleanup::Item &item : category.items) {
        if (item.blocked) {
            continue;
        }
        ++checkable;
        if (item.checked) {
            ++checked;
        }
    }
    if (checked == 0) {
        return 0;
    }
    return checked == checkable ? 2 : 1;
}

} // namespace CleanupRules
