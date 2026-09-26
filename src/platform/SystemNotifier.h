#ifndef MIGHTYTOOLS_SYSTEMNOTIFIER_H
#define MIGHTYTOOLS_SYSTEMNOTIFIER_H

#include <QObject>
#include <QSize>
#include <QString>

#include <memory>

// Notificaciones del sistema de la app, con el icono de la app en grande (pedido de Lega: nunca los
// iconos genericos de informacion o advertencia). Copia de las de LGA_PipeSync_2
// (src/services/Notifications.cpp). Una implementacion por plataforma:
//  - Windows (platform/win/SystemNotifierWin.cpp): toast nativo por PowerShell con la plantilla
//    ToastImageAndText02 (titulo, texto e imagen), con los respaldos de PipeSync (BurntToast y un
//    globo). Una cola en un hilo propio con 500 ms entre toasts, y PowerShell sin ventana
//    (CREATE_NO_WINDOW). El notificador del toast no tiene AUMID (un espacio de ancho cero, como
//    PipeSync): el encabezado sale vacio y el click no vuelve a la app.
//  - macOS (platform/mac/SystemNotifierMac.cpp): `osascript display notification`, como PipeSync.
//
// Lo apagado no consume: el hilo y el proceso de PowerShell se crean con la primera notificacion.
// En una corrida automatizada no se lanza nada: se prepara el icono y se anota lo que se mostraria.
class SystemNotifier : public QObject
{
    Q_OBJECT

public:
    explicit SystemNotifier(bool automatedRun, QObject *parent = nullptr);
    ~SystemNotifier() override;

    // Encola la notificacion (no bloquea el hilo de la UI).
    void show(const QString &title, const QString &body);
    // Si el hilo de la cola existe (se crea con la primera notificacion real).
    bool workerRunning() const;

    // El icono del toast: el frame MAS GRANDE del .ico de la app, escrito como PNG en %TEMP% (WIC
    // tomaria el primer frame, 16x16, y lo estiraria). Se reusa si tiene menos de una hora.
    struct IconFile
    {
        QString path;  ///< vacio si no se pudo (sin qico.dll, por ejemplo): el toast sale sin imagen
        QSize size;
        int frames = 0;
        bool reused = false;
    };
    static IconFile prepareIcon(const QString &icoPath, const QString &pngPath);
    // Ruta del PNG temporal: propia de la app, y otra para las corridas automatizadas.
    static QString iconTempPath(bool automatedRun);
    // Comillas simples duplicadas, como PipeSync, para el script de PowerShell.
    static QString escapeForScript(const QString &text);

    // Lo ultimo que se pidio mostrar (para el log y el self-test).
    struct Last
    {
        QString title;
        QString body;
        IconFile icon;
        bool launched = false; ///< false en corrida automatizada: no se lanzo nada
    };
    Last last() const { return m_last; }

private:
    struct Private;
    std::unique_ptr<Private> d;
    bool m_automated = false;
    Last m_last;
};

#endif // MIGHTYTOOLS_SYSTEMNOTIFIER_H
