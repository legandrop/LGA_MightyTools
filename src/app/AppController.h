#ifndef MIGHTYTOOLS_APPCONTROLLER_H
#define MIGHTYTOOLS_APPCONTROLLER_H

#include "app/HostServices.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class MainWindow;
class ModuleHost;
class QMenu;
class QSystemTrayIcon;
class SettingsStore;
class SystemNotifier;
class UpdateService;

// La app residente: el host de herramientas, la ventana, el icono de la bandeja (o de la barra de
// menu), el updater, el inicio con el sistema y la instancia unica. Lo de cada herramienta vive en
// su modulo; aca queda solo lo general (reemplaza al TrayController de Nuke Shortcuts).
class AppController : public QObject, public HostServices
{
    Q_OBJECT

public:
    struct Options
    {
        bool dryRunInput = false;
        // Medicion de consumo (--measure-idle): settings en memoria, sin bandeja, sin updater, sin
        // canal de instancia unica y como corrida automatizada. La ventana se arma y no se muestra.
        bool measurement = false;
        // Abrir la ventana al arrancar: no hay bandeja (la app quedaria invisible) u otra copia la
        // pidio mientras la bandeja no estaba lista.
        bool openWindow = false;
    };

    explicit AppController(const Options &options, QObject *parent = nullptr);
    ~AppController() override;

    ModuleHost *host() const { return m_host; }
    MainWindow *mainWindow() const { return m_window; }

    // HostServices
    void notifyWithChoice(const QString &moduleId, const QString &title, const QString &body,
                          const NoticeChoice &choice) override;
    void notify(const QString &moduleId, const QString &title, const QString &body, ModuleContext::NoticeIcon icon,
                int msecs) override;
    void showPanel(const QString &moduleId) override;
    bool hideWindow() override;
    void showWindow() override;
    QWidget *window() const override;

public slots:
    void showSettings();
    // Un .nk o un link que llega a la residente (mac: QFileOpenEvent). false si ninguna herramienta lo
    // reclama.
    bool openExternal(const QString &argument);
    // Salir de la app: «Quit» de la bandeja, y el --quit de otra copia (instalador.bat).
    void quit();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onToggleRequested(const QString &id, bool on);
    void onReleaseRequested(const QString &id);
    void onFirstToolOn();
    void refreshTray();
    void rebuildMenu();
    void refreshAutoStart();
    // Click en un aviso (ToastActivation): abre el panel de la herramienta, o le pasa el boton.
    void onNoticeClicked(const QString &arguments, const QString &choice);
    void onAutoStartToggled(bool enabled);
    // Conecta la pagina General (la de hoy: rebuildUi() arma otra) y le carga sus valores.
    void wireGeneralPage();
    void onLanguageChangeRequested(const QString &code);
    void onUiSizeChangeRequested(int level);
    bool firstRunView() const;

    Options m_options;
    std::unique_ptr<SettingsStore> m_store;
    ModuleHost *m_host = nullptr;
    MainWindow *m_window = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    UpdateService *m_updates = nullptr;
    SystemNotifier *m_notifier = nullptr;
    QString m_lastNotifier;
    bool m_buildTree = false;
};

#endif // MIGHTYTOOLS_APPCONTROLLER_H
