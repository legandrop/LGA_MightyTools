#include "modules/diskspace/cleanup/ScanTree.h"

#include <QDir>

#include <algorithm>

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

} // namespace

void ScanTree::reset(const QString &rootPath)
{
    m_nodes.clear();
    m_names.clear();
    m_denied = 0;
    m_rootPath = QDir::toNativeSeparators(rootPath);
    m_nodes.emplace_back();
}

QString ScanTree::name(Index index) const
{
    if (index == 0) {
        return m_rootPath;
    }
    const Node &n = m_nodes[index];
    return QString(reinterpret_cast<const QChar *>(m_names.data() + n.nameOffset), n.nameLength);
}

QString ScanTree::path(Index index) const
{
    QList<Index> chain;
    for (Index i = index; i != kNone && i != 0; i = m_nodes[i].parent) {
        chain.prepend(i);
    }
    QString result = m_rootPath;
    const QChar separator = QDir::separator();
    for (const Index i : chain) {
        if (!result.endsWith(separator)) {
            result += separator;
        }
        result += name(i);
    }
    return result;
}

QList<ScanTree::Index> ScanTree::children(Index index) const
{
    QList<Index> result;
    for (Index child = m_nodes[index].firstChild; child != kNone; child = m_nodes[child].nextSibling) {
        result.append(child);
    }
    std::sort(result.begin(), result.end(), [this](Index a, Index b) {
        if (m_nodes[a].bytes != m_nodes[b].bytes) {
            return m_nodes[a].bytes > m_nodes[b].bytes;
        }
        return a < b; // orden estable entre carpetas del mismo peso
    });
    return result;
}

ScanTree::Index ScanTree::find(const QString &path) const
{
    if (m_nodes.empty()) {
        return kNone;
    }
    QString native = QDir::toNativeSeparators(QDir::cleanPath(path));
    QString rootPath = m_rootPath;
    const QChar separator = QDir::separator();
    while (rootPath.size() > 1 && rootPath.endsWith(separator)) {
        rootPath.chop(1);
    }
    if (!native.startsWith(rootPath, kPathCase)) {
        return kNone;
    }
    const QString rest = native.mid(rootPath.size());
    if (!rest.isEmpty() && !rest.startsWith(separator) && !rootPath.endsWith(separator)) {
        return kNone; // "C:\Users2" no esta dentro de "C:\Users"
    }
    Index current = 0;
    for (const QString &part : rest.split(separator, Qt::SkipEmptyParts)) {
        Index next = kNone;
        for (Index child = m_nodes[current].firstChild; child != kNone; child = m_nodes[child].nextSibling) {
            if (name(child).compare(part, kPathCase) == 0) {
                next = child;
                break;
            }
        }
        if (next == kNone) {
            return kNone;
        }
        current = next;
    }
    return current;
}

bool ScanTree::isInside(Index node, Index ancestor) const
{
    for (Index i = node; i != kNone; i = m_nodes[i].parent) {
        if (i == ancestor) {
            return true;
        }
    }
    return false;
}

bool ScanTree::isAlive(Index node) const
{
    for (Index i = node; i != kNone; i = m_nodes[i].parent) {
        if (m_nodes[i].flags & Removed) {
            return false;
        }
    }
    return true;
}

ScanTree::Index ScanTree::addChild(Index parent, QStringView name)
{
    Node child;
    child.parent = parent;
    child.nameOffset = quint32(m_names.size());
    child.nameLength = quint16(qMin<qsizetype>(name.size(), 0xFFFF));
    m_names.insert(m_names.end(), name.utf16(), name.utf16() + child.nameLength);
    const Index index = Index(m_nodes.size());
    child.nextSibling = m_nodes[parent].firstChild;
    m_nodes.push_back(child);
    m_nodes[parent].firstChild = index;
    ++m_nodes[parent].pendingChildren;
    // Una carpeta que ya estaba completa deja de estarlo al sumarle una subcarpeta sin leer: no pasa en
    // un escaneo (los hijos se agregan antes de setListed), y resetSubtree lo maneja aparte.
    for (Index i = parent; i != kNone; i = m_nodes[i].parent) {
        ++m_nodes[i].dirs;
    }
    return index;
}

void ScanTree::setListed(Index index, quint64 ownBytes, quint32 ownFiles, qint64 newest, bool denied, bool cacheTagged)
{
    Node &n = m_nodes[index];
    n.ownBytes = ownBytes;
    n.ownFiles = ownFiles;
    n.flags |= Listed;
    if (denied) {
        n.flags |= Denied;
        ++m_denied;
    }
    if (cacheTagged) {
        n.flags |= CacheTagged;
    }
    for (Index i = index; i != kNone; i = m_nodes[i].parent) {
        Node &up = m_nodes[i];
        up.bytes += ownBytes;
        up.files += ownFiles;
        if (newest > up.newest) {
            up.newest = newest;
        }
    }
    if (m_nodes[index].pendingChildren == 0) {
        completeUp(index);
    }
}

void ScanTree::completeUp(Index index)
{
    Index i = index;
    for (;;) {
        Node &n = m_nodes[i];
        n.flags |= Complete;
        const Index parent = n.parent;
        if (parent == kNone) {
            return;
        }
        Node &p = m_nodes[parent];
        if (p.pendingChildren > 0) {
            --p.pendingChildren;
        }
        if (p.pendingChildren != 0 || !(p.flags & Listed)) {
            return;
        }
        i = parent;
    }
}

void ScanTree::subtractUp(Index from, quint64 bytes, quint32 files, quint32 dirs)
{
    for (Index i = from; i != kNone; i = m_nodes[i].parent) {
        Node &up = m_nodes[i];
        up.bytes = up.bytes >= bytes ? up.bytes - bytes : 0;
        up.files = up.files >= files ? up.files - files : 0;
        up.dirs = up.dirs >= dirs ? up.dirs - dirs : 0;
    }
}

void ScanTree::removeSubtree(Index index)
{
    if (index == 0 || index == kNone || (m_nodes[index].flags & Removed)) {
        return;
    }
    const Index parent = m_nodes[index].parent;
    const bool wasComplete = isComplete(index);
    subtractUp(parent, m_nodes[index].bytes, m_nodes[index].files, m_nodes[index].dirs + 1);
    // La saca de la lista de hijos de su madre.
    Index *link = &m_nodes[parent].firstChild;
    while (*link != kNone && *link != index) {
        link = &m_nodes[*link].nextSibling;
    }
    if (*link == index) {
        *link = m_nodes[index].nextSibling;
    }
    m_nodes[index].nextSibling = kNone;
    m_nodes[index].flags |= Removed;
    // Si todavia se estaba leyendo, su madre ya no la espera.
    if (!wasComplete && m_nodes[parent].pendingChildren > 0) {
        --m_nodes[parent].pendingChildren;
        if (m_nodes[parent].pendingChildren == 0 && (m_nodes[parent].flags & Listed)) {
            completeUp(parent);
        }
    }
}

void ScanTree::removeFile(Index dir, quint64 bytes)
{
    if (dir == kNone) {
        return;
    }
    Node &n = m_nodes[dir];
    n.ownBytes = n.ownBytes >= bytes ? n.ownBytes - bytes : 0;
    n.ownFiles = n.ownFiles > 0 ? n.ownFiles - 1 : 0;
    subtractUp(dir, bytes, 1, 0);
}

void ScanTree::resetSubtree(Index index)
{
    if (index == kNone || (m_nodes[index].flags & Removed)) {
        return;
    }
    Node &n = m_nodes[index];
    const bool wasComplete = isComplete(index);
    // Lo que pesaba sale de sus ancestros (ella vuelve a cero mas abajo).
    subtractUp(n.parent, n.bytes, n.files, n.dirs);
    for (Index child = n.firstChild; child != kNone;) {
        const Index next = m_nodes[child].nextSibling;
        m_nodes[child].flags |= Removed;
        m_nodes[child].nextSibling = kNone;
        child = next;
    }
    if (n.flags & Denied) {
        m_denied = m_denied > 0 ? m_denied - 1 : 0;
    }
    n.firstChild = kNone;
    n.pendingChildren = 0;
    n.ownBytes = 0;
    n.ownFiles = 0;
    n.bytes = 0;
    n.files = 0;
    n.dirs = 0;
    n.newest = 0;
    n.flags = 0;
    // Sus ancestros vuelven a esperarla: cada uno que estaba completo deja de estarlo y su madre suma uno.
    if (wasComplete) {
        Index i = n.parent;
        bool childWasComplete = true;
        while (i != kNone && childWasComplete) {
            Node &up = m_nodes[i];
            ++up.pendingChildren;
            childWasComplete = (up.flags & Complete) != 0;
            up.flags &= quint8(~Complete);
            i = up.parent;
        }
    }
}
