#ifndef MIGHTYTOOLS_SYSTEMNOTIFIER_H
#define MIGHTYTOOLS_SYSTEMNOTIFIER_H

#include <QList>
#include <QObject>
#include <QPair>
#include <QSize>
#include <QString>

#include <memory>

// Notificaciones del sistema de la app, con el icono de la app en grande (pedido de Lega: nunca los
// iconos genericos de informacion o advertencia). Copia de las de LGA_PipeSync_2
// (src/services/Notifications.cpp). Una implementacion por plataforma:
//  - Windows (platform/win/SystemNotifierWin.cpp): toast nativo por PowerShell con la plantilla
//    ToastImageAndText02 (titulo, texto e imagen), con los respaldos de PipeSync (BurntToast y un
//    globo). Una cola en un hilo propio con 500 ms entre toasts, y PowerShell sin ventana
//    (CREATE_NO_WINDOW). Con la app anotada ante Windows (platform/ToastActivation.h) el toast es
//    ToastGeneric con el AUMID propio: encabezado "LGA Mighty Tools", click que vuelve a la app y,
//    si el aviso lo pide, un desplegable con un boton. Si la anotacion falla, sale el toast de antes
//    (sin AUMID, encabezado vacio, sin click).
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

    // Un aviso. Lo de abajo del texto solo cuenta en Windows con la app anotada.
    struct Notice
    {
        QString title;
        QString body;
        QString launch; ///< argumentos del click en el cuerpo ("module=diskSpace&action=open")
        // Uno nuevo con el mismo tag y grupo reemplaza al anterior en el Centro de notificaciones.
        QString tag;
        QString group;
        bool persistent = false; ///< queda en pantalla hasta que se elige (scenario "reminder")
        // Desplegable con un boton al lado, y un boton "Dismiss" del sistema. Sin opciones, no hay.
        QString choiceLabel;                     ///< "Remind me again in"
        QList<QPair<QString, QString>> choices;  ///< id, texto
        QString choiceDefault;                   ///< id preseleccionado
        QString button;                          ///< "Remind me"
        QString buttonArguments;                 ///< lo que llega en la activacion del boton
        // Segundo boton, al lado del primero ("Free up space"). Sin texto, no hay.
        QString extraButton;
        QString extraButtonArguments;
    };
    // Id del desplegable en el XML: la activacion trae inputs["choice"].
    static QString choiceInputId() { return QStringLiteral("choice"); }

    // Encola la notificacion (no bloquea el hilo de la UI).
    void show(const QString &title, const QString &body);
    void show(const Notice &notice);

    // El XML ToastGeneric del aviso, con todo el texto escapado. `imagePath` vacio: sin imagen.
    static QString toastXml(const Notice &notice, const QString &imagePath);
    // Donde queda el PNG del icono que la anotacion usa en el encabezado: al lado de settings.ini
    // (lo borra el desinstalador con la carpeta), no en %TEMP%, que se limpia solo.
    static QString stableIconPath();
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

#ifdef Q_OS_WIN
    // Solo Windows: el script de PowerShell del toast (con la app anotada y sin anotar) y su forma
    // -EncodedCommand (UTF-16LE en base64). Expuestos para que el self-test compruebe, sin mostrar
    // nada, que acentos, enes y comillas llegan enteros.
    static QString registeredToastScript(const Notice &notice, const QString &imagePath, const QString &exePath);
    static QString plainToastScript(const QString &title, const QString &body, const QString &imagePath,
                                    const QString &exePath);
    static QString encodeCommand(const QString &script);
#endif

    // Lo ultimo que se pidio mostrar (para el log y el self-test).
    struct Last
    {
        QString title;
        QString body;
        IconFile icon;
        bool launched = false; ///< false en corrida automatizada: no se lanzo nada
        Notice notice;
    };
    Last last() const { return m_last; }

private:
    struct Private;
    std::unique_ptr<Private> d;
    bool m_automated = false;
    Last m_last;
    // Anotacion ante Windows: se intenta con el primer aviso real y se reintenta si fallo.
    bool m_registered = false;
};

#endif // MIGHTYTOOLS_SYSTEMNOTIFIER_H
