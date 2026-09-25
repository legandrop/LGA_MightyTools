#ifndef MIGHTYTOOLS_LINKREDIRECTOR_PANEL_H
#define MIGHTYTOOLS_LINKREDIRECTOR_PANEL_H

#include "app/ModuleContext.h"
#include "modules/linkredirector/LinkRedirectorRouting.h"
#include "modules/linkredirector/LinkRedirectorTypes.h"

#include <QList>
#include <QString>
#include <QWidget>

#include <functional>

class QPlainTextEdit;
class QTimer;
class StatusCard;
class LinkRedirectorComboField; ///< definida en LinkRedirectorPanel.cpp: campo "combo" (valor + flecha)

// De donde saca el panel lo que no vive en settings.ini (canvas, seccion 2 "Link Redirector" y
// seccion 3 "Link Redirector - estados"). El modulo arma una version REAL (BrowserRegistration,
// BrowserDetection) y otra de CAPTURA (fixtures en memoria, sin tocar el sistema) segun
// ModuleContext::captureMode(); el panel no sabe cual de las dos tiene.
struct LinkRedirectorPanelSources
{
    std::function<bool()> isDefaultBrowser;
    std::function<QList<DetectedBrowser>()> detectedBrowsers;
    // Handler http actual del sistema, ANTES de pedir "Make Default" (para la sincronizacion). Vacio
    // en captura: ahi "Make Default" no hace nada real.
    std::function<QString()> currentDefaultHandlerId;
    std::function<QString(const QString &handlerId)> exePathForHandlerId;
    // Registra y pide ser el navegador por defecto (Windows: abre el panel del sistema; mac: pide por
    // API). No hace nada visible en captura.
    std::function<void()> requestSetAsDefault;
};

// Panel de Settings de Link Redirector (etapa 2): tarjeta de estado + "Make Default", combos
// "Default browser"/"Alternative browser" y "Match words" con autoguardado. Estetica de Theme/
// UiWidgets: nada del Liquid Glass del origen (v0.173).
class LinkRedirectorPanel : public QWidget
{
    Q_OBJECT

public:
    LinkRedirectorPanel(ModuleContext &context, LinkRedirectorPanelSources sources, QWidget *parent = nullptr);
    ~LinkRedirectorPanel() override;

    // Widget de captura para "combo-open": la lista de un combo ya desplegada, sin exec() ni show().
    static QWidget *buildDropdownPreview(const QList<LinkRedirectorRouting::ComboItem> &items, QWidget *parent);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshStatus();
    void refreshBrowserFields();
    void refreshMatchWordsFromSettings();
    void scheduleAutosave();
    void saveMatchWordsNow();
    // Arma un campo "combo" (valor + flecha, canvas ".combo") y su menu al click ("-", detectados,
    // [custom], separador, "Browse..."). `settingsKey` es "defaultBrowser" o "alternativeBrowser".
    LinkRedirectorComboField *buildBrowserField(const QString &settingsKey, QWidget *parent);
    void showBrowserMenu(const QString &settingsKey, LinkRedirectorComboField *field);
    void chooseBrowser(const QString &settingsKey, LinkRedirectorComboField *field, const QString &exePath);
    void openBrowseDialog(const QString &settingsKey, LinkRedirectorComboField *field);
    void refreshBrowserField(LinkRedirectorComboField *field, const QString &settingsKey);

    ModuleContext &m_context;
    LinkRedirectorPanelSources m_sources;

    StatusCard *m_statusCard = nullptr;
    LinkRedirectorComboField *m_defaultField = nullptr;
    LinkRedirectorComboField *m_alternativeField = nullptr;
    QPlainTextEdit *m_matchWords = nullptr;
    QTimer *m_autosaveTimer = nullptr;

    // Sincronizacion "el que era default pasa a Default browser" (mainwindow.cpp:1562-1601 del
    // origen), via LinkRedirectorRouting::browserToSyncAsDefault.
    QString m_pendingPreviousDefaultExe;
    bool m_makeDefaultRequested = false;
};

#endif // MIGHTYTOOLS_LINKREDIRECTOR_PANEL_H
