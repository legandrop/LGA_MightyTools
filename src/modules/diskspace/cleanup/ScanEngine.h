#ifndef MIGHTYTOOLS_SCANENGINE_H
#define MIGHTYTOOLS_SCANENGINE_H

#include "modules/diskspace/cleanup/ScanTree.h"

#include <QList>
#include <QString>

#include <memory>
#include <mutex>

// Recorre un volumen entero en paralelo y arma su ScanTree. Sin administrador: lista carpeta por
// carpeta (platform/DirEnumerator), con un pool de hilos propio. Medido en un NVMe con 4,7 M de
// archivos: unos 7 s con el disco en memoria.
//
// No es un QObject y no manda senales: los hilos solo escriben en el arbol (bajo el candado) y en
// contadores atomicos. Quien lo usa (la ventana) mira progress() con un timer mientras isRunning().
// Asi ningun aviso de un hilo puede llegar a una ventana que ya se cerro.
//
// Nada de este objeto espera a un hilo: cancelar o destruirlo levanta la bandera y SUELTA los hilos,
// que terminan solos apenas vuelve la llamada del sistema en curso (un disco externo dormido puede
// tardar segundos en contestar, y la ventana no se cuelga por eso). El estado que usan los hilos vive
// hasta que sale el ultimo. CleanupThreads::alive() cuenta los que quedan.
class ScanEngine
{
public:
    // Un archivo de los mas pesados del volumen (para "Largest files").
    struct TopFile
    {
        quint64 bytes = 0;
        ScanTree::Index dir = ScanTree::kNone;
        QString name;
        qint64 modified = 0;
    };

    // Un archivo de una carpeta, listado a pedido.
    struct FileEntry
    {
        QString name;
        quint64 bytes = 0;
        qint64 modified = 0;
    };
    struct FileListing
    {
        QList<FileEntry> largest; ///< los mas pesados, de mayor a menor
        int restCount = 0;        ///< los demas, sin listar
        quint64 restBytes = 0;
        bool ok = true;
    };

    struct Progress
    {
        bool running = false;
        bool complete = false; ///< la raiz y todo su subarbol estan leidos
        quint64 files = 0;
        quint32 dirs = 0;
        quint64 bytes = 0;
        quint32 denied = 0;
        double seconds = 0.0;
    };

    static constexpr int kTopFiles = 5000;
    static constexpr int kMaxThreads = 16;

    ScanEngine();
    ~ScanEngine();
    ScanEngine(const ScanEngine &) = delete;
    ScanEngine &operator=(const ScanEngine &) = delete;

    // Escaneo completo de `rootPath` ("C:/", "C:\") sobre un arbol nuevo. Corta el que estuviera en curso.
    void start(const QString &rootPath);
    // Vuelve a leer solo esas carpetas y lo que cuelga de ellas (despues de borrar adentro). No hace
    // nada, y devuelve false, si hay una pasada en curso.
    bool rescan(const QList<ScanTree::Index> &nodes);
    // Corta la pasada sin esperar. El arbol queda como estaba, incompleto.
    void cancel();
    bool isRunning() const;
    Progress progress() const;

    // El arbol se lee y se toca SIEMPRE con el candado tomado.
    std::unique_lock<std::mutex> lock() const;
    ScanTree &tree();
    const ScanTree &tree() const;

    // Copia de los archivos mas pesados, de mayor a menor (toma el candado).
    QList<TopFile> topFiles() const;
    // Se borro ese archivo: sale de la lista y su peso se resta del arbol (toma el candado).
    void forgetFile(ScanTree::Index dir, const QString &name, quint64 bytes);
    // Se borro esa carpeta entera (toma el candado).
    void forgetDir(ScanTree::Index dir);
    // Carpetas con CACHEDIR.TAG vistas en el escaneo, sin las borradas (toma el candado).
    QList<ScanTree::Index> cacheTaggedDirs() const;

    // QA (captura): los archivos pesados de un arbol armado a mano. Toma el candado.
    void setTopFilesForCapture(const QList<TopFile> &files);

    // Los archivos de UNA carpeta, leidos del disco en el momento (no usa el arbol ni el candado).
    static FileListing listFiles(const QString &dirPath, int maxEntries);

private:
    struct Shared;
    void launch(int threads);

    std::shared_ptr<Shared> m_shared;
};

// Hilos de la limpieza (escaneo, medicion y borrado) que siguen vivos en el proceso. Con la ventana
// cerrada tiene que volver a cero.
namespace CleanupThreads {
int alive();
// Espera hasta `msecs` a que no quede ninguno. true si quedo en cero.
bool waitForNone(int msecs);
// Para los hilos de la limpieza: cuenta mientras vive el objeto.
class Scope
{
public:
    Scope();
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};
} // namespace CleanupThreads

#endif // MIGHTYTOOLS_SCANENGINE_H
