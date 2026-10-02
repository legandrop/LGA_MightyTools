#ifndef MIGHTYTOOLS_GENERALPAGE_H
#define MIGHTYTOOLS_GENERALPAGE_H

#include <QList>
#include <QString>
#include <QWidget>

class Chip;
class SegmentedSwitch;
class ModuleHeader;
class ModuleHost;
class QCheckBox;
class QFrame;
class QLabel;
class QPushButton;
class ToggleSwitch;

// Resultado del chequeo de updates en la fila (D-13): los errores siguen saliendo en cartel.
struct UpdateRowState
{
    enum class Kind { Idle, Checking, Latest, Available };
    Kind kind = Kind::Idle;
    QString version; ///< la ultima publicada (Latest y Available)
};

// Pagina "General" (canvas, secciones 2 y 7): inicio con el sistema, updates, version, las
// herramientas cargadas y "About". En el primer arranque (nada prendido todavia) muestra la
// bienvenida con una tarjeta por herramienta y solo la tarjeta "App".
//
// No escribe nada por su cuenta: avisa con senales y quien la arma (MainWindow/AppController)
// decide. Asi la captura la dibuja sin tocar el sistema.
class GeneralPage : public QWidget
{
    Q_OBJECT

public:
    GeneralPage(ModuleHost *host, QWidget *parent = nullptr);

    void setFirstRun(bool firstRun);
    // El switch de tamano vuelve al de esta sesion (el cambio no se pudo aplicar).
    void showSessionUiSize();
    bool firstRun() const { return m_firstRun; }
    // Vuelve a leer que herramientas estan prendidas (tarjeta Tools e interruptores de la bienvenida).
    void refreshTools();
    void setUpdateState(const UpdateRowState &state);
    // Refleja el inicio con el sistema. installedCopy elige el tooltip (copia instalada o de desarrollo).
    void setAutoStart(bool enabled, bool installedCopy);
    void setCheckUpdatesAtStartup(bool check);

    QCheckBox *autoStartCheck() const { return m_autoStart; }

signals:
    void autoStartToggled(bool enabled);
    void checkUpdatesAtStartupToggled(bool check);
    void checkNowRequested();
    void updateRequested();
    void helpRequested();
    void toolToggleRequested(const QString &id, bool on);
    // El usuario eligio otro idioma ("en" / "es").
    void languageChangeRequested(const QString &code);
    // El usuario eligio otro tamano de interfaz (0..2): se aplica reiniciando la app.
    void uiSizeChangeRequested(int level);
    // macOS: el boton Quit de la tarjeta App (ahi no hay menu en la barra).
    void quitRequested();

private:
    void buildWelcome();
    QFrame *buildAppCard();
    QFrame *buildToolsCard();
    QFrame *buildAboutCard();

    ModuleHost *m_host = nullptr;
    bool m_firstRun = false;

    ModuleHeader *m_header = nullptr;
    QFrame *m_welcome = nullptr;
    QList<ToggleSwitch *> m_welcomeSwitches;
    QFrame *m_toolsCard = nullptr;
    QFrame *m_aboutCard = nullptr;

    QCheckBox *m_autoStart = nullptr;
    QLabel *m_firstRunCaption = nullptr;
    QCheckBox *m_checkUpdates = nullptr;
    QLabel *m_updateResult = nullptr;
    QPushButton *m_updateButton = nullptr;
    QPushButton *m_languageButton = nullptr;
    SegmentedSwitch *m_uiSizeSwitch = nullptr;
    UpdateRowState m_updateState;

    Chip *m_toolsChip = nullptr;
    QLabel *m_running = nullptr;
    QLabel *m_off = nullptr;
};

#endif // MIGHTYTOOLS_GENERALPAGE_H
