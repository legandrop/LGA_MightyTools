#ifndef MIGHTYTOOLS_OPENINNUKEX_PANEL_H
#define MIGHTYTOOLS_OPENINNUKEX_PANEL_H

#include "modules/openinnukex/NukeBridge.h"
#include "modules/openinnukex/NukeScanner.h"

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class ModuleContext;
class Chip;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class StatusCard;

// El panel de "Open in NukeX" (canvas, seccion 2 "Open in NukeX"): tres tarjetas — ".nk files",
// "Preferred Nuke version" y "Nuke Bridge" — mas el aviso ambar del cliente viejo instalado.
//
// Interactivo vs. captura (Module.h, "Captura"): con `context.captureMode()` el panel NUNCA
// escanea Nuke, NUNCA lee nukeXpath.txt/el registro/el bridge de verdad — solo pinta el fixture
// que le da applyCaptureState(). Con la app de verdad, arranca su propio escaneo al construirse
// (no al prender el modulo: "el escaneo arranca al construir el panel", punto 2 del encargo) y
// lee el sistema en el acto.
class OpenInNukeXPanel : public QWidget
{
    Q_OBJECT

public:
    explicit OpenInNukeXPanel(ModuleContext &context, QWidget *parent = nullptr);
    ~OpenInNukeXPanel() override;

    // Los 11 estados de fixture del panel (los otros que administra el modulo — "old-client" y
    // "launcher-notice" — se resuelven fuera de este widget, ver OpenInNukeXModule).
    static QStringList captureStates();
    // False si `state` no es uno de captureStates(): el arnes lo reporta como error (Module.h).
    bool applyCaptureState(const QString &state);

signals:
    // El boton "Uninstall old app" termino: el modulo vuelve a leer su estado (la fila de la barra).
    void oldClientStateChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // ---- construccion de las tres tarjetas ----
    QWidget *buildAssociationCard();
    QWidget *buildVersionCard();
    QWidget *buildBridgeCard();
    QWidget *buildOldClientNotice();
    void onUninstallOldClicked();
    void showUninstalling();
    void updateBusyButtons();
    void attachToOperations();
    void onUninstallOldFinished(bool stillInstalled, bool launched);

    // ---- ".nk files" ----
    void refreshAssociation();
    void onApplyClicked();
    void onApplyFinished(bool success, bool needsConfirmation, const QList<int> &issues);

    // ---- "Preferred Nuke version" ----
    void startScan();
    void rebuildVersionButtons();
    void updateChosenVersionHighlight(); ///< resalta el boton cuya ruta coincide con la cargada
    void onVersionButtonClicked(const NukeVersion &version);
    void onBrowseNukeXClicked();
    void onSaveNukeXClicked();
    void loadSavedNukePath();
    void onShowLaunchNoticeToggled(bool checked);

    // ---- "Nuke Bridge" ----
    void refreshBridge();       ///< version completa: TAMBIEN repone el campo .nuke
    void refreshBridgeStatus(); ///< solo chip/hint/boton, segun lo que HOY dice el campo
    void onBrowseNukeDirClicked();
    void onInstallClicked();
    void onExportClicked();
    void onCopyLineClicked();
    void onToggleManualClicked();

    // Unico punto de mensajes del panel: pasa automatedRun del contexto y `this` como padre.
    void report(const struct OpenInNukeXMessage &message);

    ModuleContext &m_context;
    bool m_interactive = false; ///< !captureMode(): solo asi escanea, lee o escribe de verdad
    QString m_fixtureState;     ///< estado de captura activo (vacio si m_interactive)

    NukeScanner *m_scanner = nullptr;
    QList<NukeVersion> m_foundVersions;
    QString m_nukeXPathFile; ///< NukeXPath::defaultFilePath(), resuelta una sola vez

    // ".nk files"
    Chip *m_assocChip = nullptr;
    // El estado de la asociacion: lo que decide Apply o Re-apply (nunca el texto del chip, que se traduce).
    bool m_associated = false;
    QPushButton *m_applyButton = nullptr;
    bool m_applyRunning = false;

    // "Preferred Nuke version"
    QLabel *m_scanStatusLabel = nullptr;
    QLabel *m_scanChooseLabel = nullptr; ///< "Choose one of the found versions or browse your own:"
    QWidget *m_versionButtonsRow = nullptr;
    QLineEdit *m_pathField = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QCheckBox *m_showNoticeCheck = nullptr;

    // "Nuke Bridge"
    Chip *m_bridgeChip = nullptr;
    QLineEdit *m_nukeDirField = nullptr;
    QPushButton *m_bridgeBrowseButton = nullptr;
    QPushButton *m_installButton = nullptr;
    QLabel *m_bridgeHint = nullptr;
    QPushButton *m_manualToggle = nullptr;
    QWidget *m_manualPanel = nullptr;
    QPushButton *m_copyLineButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    bool m_manualOpen = false;

    QWidget *m_oldClientCard = nullptr;
    StatusCard *m_oldClientStatus = nullptr; ///< la misma tarjeta, para cambiarle el texto
    QPushButton *m_uninstallOldButton = nullptr;
    QWidget *m_associationCard = nullptr;
    QWidget *m_versionCard = nullptr;
    QWidget *m_bridgeCard = nullptr;
};

#endif // MIGHTYTOOLS_OPENINNUKEX_PANEL_H
