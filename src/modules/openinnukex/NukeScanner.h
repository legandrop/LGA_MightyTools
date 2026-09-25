#ifndef MIGHTYTOOLS_OPENINNUKEX_NUKESCANNER_H
#define MIGHTYTOOLS_OPENINNUKEX_NUKESCANNER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <tuple>

// Una version de Nuke encontrada por el escaneo (inventario, "Preferred Nuke Version").
struct NukeVersion
{
    QString name;        ///< "Nuke15.0v4"
    QString path;        ///< ruta completa al ejecutable (win) o al binario dentro del bundle (mac)
    QString version;     ///< "15.0v4"
    QString displayName; ///< "Nuke 15.0v4"
};

// Escanea las ubicaciones habituales de Nuke (Program Files en Windows, /Applications en mac) y
// arma la lista de versiones instaladas, para el panel "Preferred Nuke Version" (etapa 2).
//
// El escaneo corre en un QThread propio: recorrer Program Files o /Applications es I/O de disco
// que puede tardar, y el contrato del modulo (plan 4.3, Module.h) prohibe bloquear el hilo de UI.
// El escaneo arranca solo cuando el panel lo pide (createPanel, etapa 2), nunca al prender el
// modulo: "lo apagado no consume nada" tambien vale para un modulo prendido sin panel abierto.
class NukeScanner : public QObject
{
    Q_OBJECT

public:
    explicit NukeScanner(QObject *parent = nullptr);
    ~NukeScanner() override;

    // Idempotente: si ya hay un escaneo en curso, no arranca uno segundo.
    void startScan();

    // La logica de escaneo en si, sin hilo: la usa el worker interno y el self-test (que no
    // necesita tocar un QThread para probar el parseo y el orden).
    static QList<NukeVersion> scanSync();

    // "16.0v9" -> (16, 0, 9) comparable. Lo que no matchea el patron "N.NvN" queda en (0,0,0), asi
    // que una version mal formada pierde contra cualquiera bien formada en vez de ganar por
    // accidente (inventario, configwindow.cpp:1528-1541).
    static std::tuple<int, int, int> versionSortKey(const QString &version);

    // True si `a` es mas nueva que `b` segun versionSortKey.
    static bool isNewer(const NukeVersion &a, const NukeVersion &b);

    // La de mayor version de la lista, o un NukeVersion vacio si `versions` esta vacia.
    static NukeVersion newest(const QList<NukeVersion> &versions);

signals:
    void scanStarted();
    void scanProgress(const QString &currentPath);
    void versionFound(const NukeVersion &version);
    void scanFinished(const QList<NukeVersion> &versions);

private:
    void onWorkerFinished(const QList<NukeVersion> &versions);

    QThread *m_thread = nullptr;
    bool m_scanning = false;
};

#endif // MIGHTYTOOLS_OPENINNUKEX_NUKESCANNER_H
