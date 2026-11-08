#ifndef MIGHTYTOOLS_MAINWINDOW_H
#define MIGHTYTOOLS_MAINWINDOW_H

#include "app/GeneralPage.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QString>

class QTimer;

class HowItWorksStrip;
class ModuleHeader;
class ModuleHost;
class NavShell;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class SettingsStore;
class SidebarItem;
class TitleBar;

// La ventana de LGA Mighty Tools en el lenguaje «Pliego» (D-73): barra de titulo propia sobre la mesa
// negra, el indice lateral de 224 px con "General" arriba y una fila por herramienta en orden alfabetico, y
// a la derecha la hoja de la pagina elegida, unida a su ficha del indice.
//
// La pagina de una herramienta es su encabezado (del descriptor, con el estado vivo que sube el panel), la
// franja «How it works» (su ayuda, cerrada al entrar) y, debajo, su panel (creado recien al elegirla, ModuleHost::panel) o el
// panel de apagado que dibuja el host. Cerrar no cierra la app: oculta la ventana a la bandeja.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Capture: la arma --ui-shot. No lee el sistema ni settings, no guarda la posicion.
    enum class Mode { Normal, Capture };

    static constexpr int kWidth = 880;
    static constexpr int kHeight = 720;
    static inline const QString kGeneral = QStringLiteral("general");

    // appStore: donde se guarda la posicion de la ventana (nullptr en captura).
    MainWindow(ModuleHost *host, Mode mode, SettingsStore *appStore, QWidget *parent = nullptr);
    ~MainWindow() override;

    TitleBar *titleBar() const { return m_titleBar; }
    GeneralPage *generalPage() const { return m_general; }

    // "general" o el id de una herramienta.
    void selectPage(const QString &id);
    QString currentPage() const { return m_current; }
    // La pagina siguiente (+1) o la anterior (-1) en el orden del indice; despues de la ultima vuelve a General.
    void stepPage(int direction);

    // Abre o cierra la franja «How it works» de la pagina que se ve (el «?» de la barra hace lo mismo).
    void setHelpOpen(bool open);
    bool helpOpen() const;

    void setFirstRun(bool firstRun);
    void setUpdateState(const UpdateRowState &state);
    // Vuelve a armar todo el contenido (otro idioma) en la misma pagina. Los paneles de las
    // herramientas se borran en el acto y se piden de nuevo al verse. La pagina General es otra:
    // quien la escuchaba se reconecta con rebuilt().
    void rebuildUi();
    // Otra paleta de colores: lo que ya esta armado toma los colores en el acto (Theme::refresh) y la ventana se
    // rearma, porque hay textos y glifos que se arman con su color. Junta los cambios seguidos (las flechas del
    // menu, el selector del editor) y con un menu abierto solo repinta: el rearmado espera a que se cierre.
    void schedulePaletteRebuild();
    // Captura: estado de prueba del aviso de apagado de una herramienta (offNotice del descriptor).
    void setOffNoticeCaptureState(const QString &id, const QString &state);

signals:
    // El usuario prendio o apago una herramienta (fila, encabezado, "Turn on" o la bienvenida).
    void toggleRequested(const QString &id, bool on);
    void releaseRequested(const QString &id);
    // rebuildUi() termino: generalPage() es un objeto nuevo.
    void rebuilt();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void applyPaletteChange();
    void rebuildWhenNoPopup();
    QTimer *m_paletteTimer = nullptr;
    bool m_paletteRebuildWaiting = false;
    int m_rebuildSerial = 0;
    struct ToolPage
    {
        QScrollArea *scroll = nullptr;
        ModuleHeader *header = nullptr;
        HowItWorksStrip *help = nullptr;
        QWidget *body = nullptr;         ///< contenedor del panel o del panel de apagado
        QVBoxLayout *bodyLayout = nullptr;
        QPointer<QWidget> offPanel;
        QPointer<QWidget> panel;
        QString helpText; ///< los pasos y la nota que muestra la franja, para no rearmarla si no cambiaron
    };

    void buildUi();
    QScrollArea *makePage(QWidget **content, QVBoxLayout **layout);
    void onModuleToggled(const QString &id, bool running);
    void refreshItem(const QString &id);
    void refreshGeneralItem();
    // Pone en la pagina de `id` su panel (si esta prendida y la pagina se esta viendo) o el de apagado.
    void syncBody(const QString &id);
    // Vuelve a repartir el alto de la pagina que se ve (ver la nota en el .cpp).
    void relayoutCurrentPage();
    void refreshOffNotices();
    // La ayuda de la pagina `id`: texto al dia (lee los atajos configurados) y, si se esta entrando a la
    // pagina, cerrada.
    void syncHelp(const QString &id, bool entering);
    // Solo el texto de la ayuda de `id`, si cambio (un atajo nuevo): lee settings, no construye nada.
    void refreshHelpText(const QString &id);
    void refreshHelpButton();
    void restorePosition();
    void savePosition();
    bool m_nativeFrameApplied = false;

    ModuleHost *m_host = nullptr;
    Mode m_mode = Mode::Normal;
    SettingsStore *m_appStore = nullptr;
    bool m_positionRestored = false;

    TitleBar *m_titleBar = nullptr;
    NavShell *m_shell = nullptr;
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
