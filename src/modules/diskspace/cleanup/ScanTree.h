#ifndef MIGHTYTOOLS_SCANTREE_H
#define MIGHTYTOOLS_SCANTREE_H

#include <QList>
#include <QString>
#include <QStringView>

#include <vector>

// El disco escaneado, en memoria: SOLO carpetas, cada una con lo que pesan sus archivos propios y el
// total de su subarbol. Los archivos no se guardan (se listan al desplegar una carpeta, ver
// ScanEngine::listFiles): con 4,7 millones de archivos y 718 mil carpetas, el arbol ocupa unos 50 MB.
//
// Sin sistema de archivos y sin hilos: es aritmetica pura. Quien lo comparte entre hilos (ScanEngine)
// lo protege con su candado. Los bytes son los ASIGNADOS en disco, que es lo que se libera al borrar.
class ScanTree
{
public:
    using Index = quint32;
    static constexpr Index kNone = 0xFFFFFFFFu;

    enum Flag : quint8 {
        Listed = 0x01,      ///< ya se leyeron sus archivos y subcarpetas
        Denied = 0x02,      ///< no se pudo abrir (sin permiso): no se sabe cuanto pesa
        Complete = 0x04,    ///< ella y todo su subarbol estan leidos
        Removed = 0x08,     ///< borrada: ya no cuelga de su carpeta madre
        CacheTagged = 0x10, ///< tiene un archivo CACHEDIR.TAG (su programa la marca como cache)
    };

    struct Node
    {
        Index parent = kNone;
        Index firstChild = kNone;
        Index nextSibling = kNone;
        quint32 nameOffset = 0;
        quint16 nameLength = 0;
        quint8 flags = 0;
        quint32 pendingChildren = 0; ///< subcarpetas directas cuyo subarbol todavia no esta completo
        quint32 ownFiles = 0;
        quint32 files = 0; ///< archivos del subarbol
        quint32 dirs = 0;  ///< subcarpetas del subarbol
        quint64 ownBytes = 0;
        quint64 bytes = 0; ///< asignados, del subarbol
        qint64 newest = 0; ///< modificacion mas nueva del subarbol (segundos desde 1970)
    };

    // Arbol nuevo con solo la raiz (`rootPath`: "C:\" o "/Volumes/Cache"), pendiente de leer.
    void reset(const QString &rootPath);
    bool isEmpty() const { return m_nodes.empty(); }
    Index root() const { return 0; }
    QString rootPath() const { return m_rootPath; }
    int nodeCount() const { return int(m_nodes.size()); }
    quint32 deniedCount() const { return m_denied; }

    const Node &node(Index index) const { return m_nodes[index]; }
    bool isComplete(Index index) const { return (m_nodes[index].flags & Complete) != 0; }
    QString name(Index index) const;
    // Ruta para mostrar, con los separadores del sistema.
    QString path(Index index) const;
    // Subcarpetas que siguen existiendo, de la mas pesada a la mas liviana.
    QList<Index> children(Index index) const;
    // La carpeta de esa ruta (sin distinguir mayusculas en Windows), o kNone.
    Index find(const QString &path) const;
    // `node` es `ancestor` o cuelga de ella (vale tambien para una carpeta ya borrada).
    bool isInside(Index node, Index ancestor) const;
    // Sigue en el arbol: ni ella ni ninguna de sus ancestros se borro.
    bool isAlive(Index node) const;

    // ---- Escaneo
    Index addChild(Index parent, QStringView name);
    // El resultado de leer una carpeta. Va DESPUES de addChild de todas sus subcarpetas: suma lo propio a
    // ella y a sus ancestros, y si no tiene subcarpetas la da por completa (y hacia arriba).
    void setListed(Index index, quint64 ownBytes, quint32 ownFiles, qint64 newest, bool denied, bool cacheTagged);

    // ---- Despues de borrar
    // La carpeta se borro entera: sale del arbol y su peso se resta hacia arriba.
    void removeSubtree(Index index);
    // Se borro un archivo suelto de esa carpeta.
    void removeFile(Index dir, quint64 bytes);
    // Para volver a leerla: queda vacia y pendiente, con su peso anterior restado hacia arriba.
    void resetSubtree(Index index);

private:
    void completeUp(Index index);
    void subtractUp(Index from, quint64 bytes, quint32 files, quint32 dirs);

    QString m_rootPath;
    std::vector<Node> m_nodes;
    std::vector<char16_t> m_names;
    quint32 m_denied = 0;
};

#endif // MIGHTYTOOLS_SCANTREE_H
