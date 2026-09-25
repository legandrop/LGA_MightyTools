#ifndef MIGHTYTOOLS_MAINWINDOW_H
#define MIGHTYTOOLS_MAINWINDOW_H

#include "app/GeneralPage.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QString>

class ModuleHeader;
class ModuleHost;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class SettingsStore;
class SidebarItem;
class TitleBar;

// La ventana de LGA Mighty Tools, forma A del canvas (D-01): barra de titulo propia, barra lateral
// de 224 px con "General" arriba y una fila por herramienta, y a la derecha la pagina elegida.
//
// La pagina de una herramienta es su encabezado (del descriptor) y, debajo, su panel (creado
// recien al elegirla, ModuleHost::panel) o el panel de apagado que dibuja el host. Cerrar no cierra
// la app: oculta la ventana a la bandeja.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Capture: la arma --ui-shot. No lee el sistema ni settings, no guarda la posicion.
    enum class Mode { Normal, Capture };

    static constexpr int kWidth = 780;
    static constexpr int kHeight = 676;
    static inline const QString kGeneral = QStringLiteral("general");

    // appStore: donde se guarda la posicion (nullptr en captura).
    MainWindow(ModuleHost *host, Mode mode, SettingsStore *appStore, QWidget *parent = nullptr);
    ~MainWindow() override;

    TitleBar *titleBar() const { return m_titleBar; }
    GeneralPage *generalPage() const { return m_general; }

    // "general" o el id de una herramienta.
    void selectPage(const QString &id);
    QString currentPage() const { return m_current; }

    void setFirstRun(bool firstRun);
    void setUpdateState(const UpdateRowState &state);
    // Captura: estado de prueba del aviso de apagado de una herramienta (offNotice del descriptor).
    void setOffNoticeCaptureState(const QString &id, const QString &state);

signals:
    void helpRequested();
    // El usuario prendio o apago una herramienta (fila, encabezado, "Turn on" o la bienvenida).
    void toggleRequested(const QString &id, bool on);
    void releaseRequested(const QString &id);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    struct ToolPage
    {
        QScrollArea *scroll = nullptr;
        ModuleHeader *header = nullptr;
        QWidget *body = nullptr;         ///< contenedor del panel o del panel de apagado
        QVBoxLayout *bodyLayout = nullptr;
        QPointer<QWidget> offPanel;
        QPointer<QWidget> panel;
    };

    void buildUi();
    QScrollArea *makePage(QWidget **content, QVBoxLayout **layout);
    void onModuleToggled(const QString &id, bool running);
    void refreshItem(const QString &id);
    void refreshGeneralItem();
    // Pone en la pagina de `id` su panel (si esta prendida y la pagina se esta viendo) o el de apagado.
    void syncBody(const QString &id);
    void refreshOffNotices();
    void restorePosition();
    void savePosition();
    bool m_nativeFrameApplied = false;

    ModuleHost *m_host = nullptr;
    Mode m_mode = Mode::Normal;
    SettingsStore *m_appStore = nullptr;
    bool m_positionRestored = false;

    TitleBar *m_titleBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    SidebarItem *m_generalItem = nullptr;
    QHash<QString, SidebarItem *> m_items;
    QHash<QString, ToolPage> m_pages;
    QScrollArea *m_generalScroll = nullptr;
    GeneralPage *m_general = nullptr;
    UpdateRowState m_updateState;
    QString m_current;
    QHash<QString, QString> m_offNoticeStates;
};

#endif // MIGHTYTOOLS_MAINWINDOW_H
