#include "modules/openinnukex/OpenInNukeXPanel.h"
#include "core/I18n.h"

#include "app/ModuleContext.h"
#include "core/AppPaths.h"
#include "modules/openinnukex/NukeXPath.h"
#include "modules/openinnukex/OpenInNukeXMessages.h"
#include "modules/openinnukex/OpenInNukeXOperations.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#ifdef Q_OS_WIN
#include "modules/openinnukex/win/OldClientMigration.h"
#include "modules/openinnukex/win/WinFileAssociation.h"
#elif defined(Q_OS_MACOS)
#include "modules/openinnukex/mac/MacFileAssociation.h"
#endif

#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Los 11 estados de fixture del panel (canvas, seccion 3: "primera vez", "escaner de versiones",
// "sin Nuke y con el bridge a mano", "el cliente viejo sigue instalado", "chip del bridge"). El
// duodecimo estado del modulo ("launcher-notice") no toca este panel: lo arma
// OpenInNukeXModule::createCaptureWidget() con OpenInNukeXMessages::buildLaunchNoticeWidget().
const QStringList kFixtureStates = {
    QStringLiteral("associated"),         QStringLiteral("first-time"),   QStringLiteral("scan-starting"),
    QStringLiteral("scan-walking"),       QStringLiteral("scan-found"),   QStringLiteral("scan-none"),
    QStringLiteral("no-nuke-manual-open"), QStringLiteral("old-client"),  QStringLiteral("bridge-update"),
    QStringLiteral("bridge-unknown"),     QStringLiteral("bridge-installed"),
};


QString oldClientCaption()
{
    return I18n::tr("Uninstall it so both apps don't fight over .nk files.");
}

// Campo de ruta con foco solo por click (regla de la app): Enter guarda y suelta, Escape descarta
// y suelta, un click afuera lo suelta guardando (editingFinished tambien dispara ahi). Copia el
// patron de DriveRow (DiskCard.cpp) para QLineEdit en vez de QSpinBox.
QLineEdit *makePathField(QWidget *parent)
{
    auto *field = new QLineEdit(parent);
    field->setObjectName(QStringLiteral("pathField"));
    field->setFocusPolicy(Qt::ClickFocus);
    return field;
}

} // namespace

OpenInNukeXPanel::OpenInNukeXPanel(ModuleContext &context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_interactive(!context.captureMode())
    , m_nukeXPathFile(NukeXPath::defaultFilePath())
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_oldClientCard = buildOldClientNotice();
    layout->addWidget(m_oldClientCard);
    m_associationCard = buildAssociationCard();
    layout->addWidget(m_associationCard);
    m_versionCard = buildVersionCard();
    layout->addWidget(m_versionCard);
    m_bridgeCard = buildBridgeCard();
    layout->addWidget(m_bridgeCard);
    m_oldClientCard->setVisible(false);

    if (m_interactive) {
        // Se reconsulta la asociacion al volver la ventana al frente (inventario: "reconsulta la
        // asociacion al volver al frente"): por si el usuario la cambio desde el Explorador
        // mientras la ventana estaba atras.
        if (QWidget *window = m_context.window()) {
            window->installEventFilter(this);
        }
        refreshAssociation();
        refreshBridge();
        loadSavedNukePath();
        // El escaneo arranca al CONSTRUIRSE el panel, no al prender el modulo (punto 2 del
        // encargo): recien cuando el usuario abre la pagina de la herramienta.
        startScan();
#ifdef Q_OS_WIN
        m_oldClientCard->setVisible(WinFileAssociation::isOldClientInstalled());
#endif
        attachToOperations();
    }
}

OpenInNukeXPanel::~OpenInNukeXPanel()
{
    // Nada que esperar: el Apply y la desinstalacion en curso viven en OpenInNukeXOperations y siguen su camino
    // aunque este panel se borre (cambio de idioma, otra herramienta). Un panel nuevo los retoma.
}

QStringList OpenInNukeXPanel::captureStates()
{
    return kFixtureStates;
}

void OpenInNukeXPanel::report(const OpenInNukeXMessage &message)
{
    OpenInNukeXMessages::report(message, this, m_context.automatedRun());
}

bool OpenInNukeXPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_interactive) {
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_context.window() && event->type() == QEvent::ActivationChange) {
        if (auto *window = qobject_cast<QWidget *>(watched); window && window->isActiveWindow()) {
            refreshAssociation();
#ifdef Q_OS_WIN
            // Tambien el aviso del cliente viejo: se desinstala desde Ajustes de Windows. Con el
            // desinstalador corriendo (boton "Uninstall old app") lo decide su final.
            const OpenInNukeXOperations *operations = OpenInNukeXOperations::existing();
            if (!operations || !operations->uninstallRunning()) {
                m_oldClientCard->setVisible(WinFileAssociation::isOldClientInstalled());
            }
#endif
        }
        return QWidget::eventFilter(watched, event);
    }
    // Escape descarta lo escrito y suelta el campo (regla de la app): repone el ultimo valor
    // conocido en vez de lo que el usuario tipeo, y no dispara Save/Install.
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        if (watched == m_pathField) {
            loadSavedNukePath();
            m_pathField->clearFocus();
            return true;
        }
        if (watched == m_nukeDirField) {
            m_nukeDirField->setText(QDir::toNativeSeparators(NukeBridge::currentNukeDirectory()));
            m_nukeDirField->clearFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ================================================================== ".nk files"

QWidget *OpenInNukeXPanel::buildAssociationCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *head = new QHBoxLayout();
    head->addWidget(Ui::label(I18n::tr(".nk files"), "cardTitle", card), 1);
    m_assocChip = new Chip(card);
    head->addWidget(m_assocChip, 0, Qt::AlignVCenter);
    layout->addLayout(head);

    auto *row = new QHBoxLayout();
    row->setSpacing(12);
    auto *description = Ui::caption(
        I18n::tr("Associate .nk files with LGA Mighty Tools to open them directly in your preferred NukeX version."),
        card);
    description->setWordWrap(true);
    row->addWidget(description, 1);
    m_applyButton = Ui::button(I18n::tr("Apply"), QStringLiteral("primary"), QString(), card);
    row->addWidget(m_applyButton, 0, Qt::AlignTop);
    layout->addLayout(row);

    if (m_interactive) {
        connect(m_applyButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onApplyClicked);
    }
    return card;
}

void OpenInNukeXPanel::refreshAssociation()
{
    if (!m_interactive || m_applyRunning) {
        return;
    }
#ifdef Q_OS_WIN
    const bool associated = WinFileAssociation::isNkAssociatedWithUs();
#elif defined(Q_OS_MACOS)
    const bool associated = MacFileAssociation::isDefaultNkHandler();
#else
    const bool associated = false;
#endif
    m_associated = associated;
    m_assocChip->set(associated ? QStringLiteral("ok") : QStringLiteral("warn"),
                     associated ? I18n::tr("Associated") : I18n::tr("Not associated"));
    m_applyButton->setText(associated ? I18n::tr("Re-apply") : I18n::tr("Apply"));
    Ui::setStyleProperty(m_applyButton, "variant", associated ? QString() : QStringLiteral("primary"));
}

void OpenInNukeXPanel::onApplyClicked()
{
    // Una operacion a la vez (Apply y desinstalacion escriben la misma asociacion).
    if (!m_interactive || m_applyRunning || OpenInNukeXOperations::busy()) {
        return;
    }
    if (m_context.automatedRun()) {
        qInfo("[openInNukeX] (automatedRun) Apply/Re-apply: no se toca el registro ni la asociacion");
        return;
    }
#ifdef Q_OS_MACOS
    // Solo en mac, como en el cliente original (executeMacAssociation): ahi se asocia la RUTA del
    // bundle, y el `limpiar.sh` del proximo build la borra. En Windows Apply es una accion explicita
    // del usuario y desde un build SI se hace (contrato: ModuleContext::persistentRegistrationAllowed).
    if (AppPaths::isBuildTree()) {
        report(OpenInNukeXMessages::runningFromBuildFolder());
        return;
    }
#endif

    m_applyRunning = true;
    // El HWND se captura ACA, en el hilo de UI (winId() no es seguro desde otro hilo): es el dueño del selector
    // nativo "Abrir con" si el hash silencioso no alcanza. window() puede ser nullptr fuera de captura solo en
    // teoria; con ventana real esto nunca pasa.
    QWidget *topLevel = m_context.window();
    void *parentWindow = topLevel ? reinterpret_cast<void *>(topLevel->winId()) : nullptr;
    OpenInNukeXOperations::instance()->startApply(m_associated, parentWindow);
    updateBusyButtons();
}

void OpenInNukeXPanel::onApplyFinished(bool success, bool needsConfirmation, const QList<int> &issues)
{
    m_applyRunning = false;
    updateBusyButtons();
    refreshAssociation();

    if (success) {
        report(OpenInNukeXMessages::associationCompleted());
    } else if (needsConfirmation) {
        report(OpenInNukeXMessages::oneMoreStepInWindows());
    } else if (!issues.isEmpty()) {
#ifdef Q_OS_MACOS
        // En mac el unico camino de fallo real es que Launch Services no haya entregado los .nk
        // (inventario: "Almost done"), con el detalle crudo solo en el log.
        report(OpenInNukeXMessages::almostDoneMac());
#else
        // El texto se arma ACA, en el hilo de la UI y en el idioma de este momento (el hilo de trabajo solo
        // devolvio codigos).
        QStringList lines;
        for (const int issue : issues) {
            lines << OpenInNukeXMessages::applyIssueText(static_cast<ApplyIssue>(issue));
        }
        report(OpenInNukeXMessages::associationFinishedWithWarnings(lines.join(QStringLiteral("<br>"))));
#endif
    } else {
        report(OpenInNukeXMessages::associationError(I18n::tr("Unknown error.")));
    }
}

// ================================================================== "Preferred Nuke version"

QWidget *OpenInNukeXPanel::buildVersionCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    layout->addWidget(Ui::label(I18n::tr("Preferred Nuke version"), "cardTitle", card));
    auto *description = Ui::caption(
        I18n::tr("When no NukeX session is running, .nk files open with this NukeX version."), card);
    description->setWordWrap(true);
    layout->addWidget(description);

    m_scanStatusLabel = Ui::label(QString(), "caption", card);
    m_scanStatusLabel->setWordWrap(true);
    layout->addWidget(m_scanStatusLabel);
    m_scanChooseLabel = Ui::caption(I18n::tr("Choose one of the found versions or browse your own:"), card);
    m_scanChooseLabel->setVisible(false);
    layout->addWidget(m_scanChooseLabel);

    m_versionButtonsRow = new QWidget(card);
    auto *versionsLayout = new QHBoxLayout(m_versionButtonsRow);
    versionsLayout->setContentsMargins(0, 4, 0, 0);
    versionsLayout->setSpacing(6);
    versionsLayout->addStretch(1);
    layout->addWidget(m_versionButtonsRow);

    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(6);
    m_pathField = makePathField(card);
    m_pathField->setPlaceholderText(I18n::tr("Path to NukeX executable"));
    pathRow->addWidget(m_pathField, 1);
    m_browseButton = Ui::button(I18n::tr("Browse..."), QString(), QString(), card);
    pathRow->addWidget(m_browseButton);
    m_saveButton = Ui::button(I18n::tr("Save"), QStringLiteral("primary"), QString(), card);
    pathRow->addWidget(m_saveButton);
    layout->addLayout(pathRow);

    m_showNoticeCheck = new QCheckBox(I18n::tr("Show a notice while a new NukeX opens"), card);
    layout->addWidget(m_showNoticeCheck);
    auto *noticeCaption = Ui::caption(I18n::tr("A small window that closes itself after 3 seconds."), card);
    layout->addWidget(noticeCaption);

    if (m_interactive) {
        connect(m_browseButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onBrowseNukeXClicked);
        connect(m_saveButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onSaveNukeXClicked);
        connect(m_showNoticeCheck, &QCheckBox::toggled, this, &OpenInNukeXPanel::onShowLaunchNoticeToggled);
        // Enter guarda y suelta el campo; Escape lo suelta reponiendo lo guardado; un click afuera
        // dispara editingFinished solo (tambien guarda), igual que el umbral de disco (DiskCard).
        connect(m_pathField, &QLineEdit::editingFinished, this, [this]() {
            if (m_pathField->hasFocus()) {
                onSaveNukeXClicked();
                m_pathField->clearFocus();
            }
        });
        m_pathField->installEventFilter(this);
        m_showNoticeCheck->setChecked(m_context.value(QStringLiteral("showLaunchNotice"), false).toBool());
    }
    return card;
}

void OpenInNukeXPanel::loadSavedNukePath()
{
    m_pathField->setText(QDir::toNativeSeparators(NukeXPath::read(m_nukeXPathFile)));
}

void OpenInNukeXPanel::startScan()
{
    m_scanStatusLabel->setText(I18n::tr("Scanning for installed Nuke versions…"));
    m_scanner = new NukeScanner(this);
    connect(m_scanner, &NukeScanner::scanProgress, this, [this](const QString &path) {
        // Inventario: path truncado a 50 caracteres con "..." adelante.
        const QString shown = path.size() > 50 ? QStringLiteral("...") + path.right(47) : path;
        m_scanStatusLabel->setText(I18n::tr("Scanning: %1").arg(shown));
    });
    connect(m_scanner, &NukeScanner::scanFinished, this, [this](const QList<NukeVersion> &versions) {
        m_foundVersions = versions;
        if (versions.isEmpty()) {
            m_scanStatusLabel->setText(I18n::tr("No Nuke installations found in common locations"));
            Ui::setStyleProperty(m_scanStatusLabel, "tone", QStringLiteral("err"));
        } else {
            m_scanStatusLabel->setText(versions.size() == 1 ? I18n::tr("%1 Nuke version found:").arg(versions.size())
                                                      : I18n::tr("%1 Nuke versions found:").arg(versions.size()));
            Ui::setStyleProperty(m_scanStatusLabel, "tone", QString());
        }
        m_scanChooseLabel->setVisible(!versions.isEmpty());
        rebuildVersionButtons();
        // healStalePath SOLO con la ruta real (nunca en captura: startScan() no corre ahi).
        const QString healed = NukeXPath::healStalePath(m_nukeXPathFile, m_pathField->text().trimmed(), versions);
        if (!healed.isEmpty() && healed != m_pathField->text()) {
            m_pathField->setText(QDir::toNativeSeparators(healed));
        }
        updateChosenVersionHighlight();
    });
    m_scanner->startScan();
}

void OpenInNukeXPanel::rebuildVersionButtons()
{
    auto *versionsLayout = qobject_cast<QHBoxLayout *>(m_versionButtonsRow->layout());
    while (QLayoutItem *item = versionsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const NukeVersion &version : m_foundVersions) {
        auto *button = Ui::button(version.displayName, QString(), QStringLiteral("sm"), m_versionButtonsRow);
        // Ruta nativa guardada como propiedad: updateChosenVersionHighlight() compara contra ella
        // sin importar / o \ ni mayusculas (auditoria, punto 3).
        button->setProperty("versionPath", QDir::toNativeSeparators(version.path));
        connect(button, &QPushButton::clicked, this, [this, version]() { onVersionButtonClicked(version); });
        versionsLayout->addWidget(button);
    }
    versionsLayout->addStretch(1);
    updateChosenVersionHighlight();
}

void OpenInNukeXPanel::updateChosenVersionHighlight()
{
    // Canvas "Preferred Nuke version": el boton de la version cuya ruta coincide con la cargada
    // queda resaltado (--chk-bg/--chk-border, ver Theme::kChosenBg/kChosenBorder/kChosenText).
    // Insensible a mayusculas y a / vs \: la ruta puede venir del escaneo (QFileInfo, con /) o de
    // lo que el usuario escribio o eligio con Browse (nativa, con \ en Windows).
    const QString current = QDir::toNativeSeparators(m_pathField->text().trimmed());
    auto *versionsLayout = qobject_cast<QHBoxLayout *>(m_versionButtonsRow->layout());
    for (int i = 0; i < versionsLayout->count(); ++i) {
        QLayoutItem *item = versionsLayout->itemAt(i);
        auto *button = item ? qobject_cast<QPushButton *>(item->widget()) : nullptr;
        if (!button) {
            continue;
        }
        const QString buttonPath = button->property("versionPath").toString();
        const bool chosen = !current.isEmpty() && !buttonPath.isEmpty() && buttonPath.compare(current, Qt::CaseInsensitive) == 0;
        Ui::setStyleProperty(button, "chosen", chosen);
    }
}

void OpenInNukeXPanel::onVersionButtonClicked(const NukeVersion &version)
{
    // Solo carga el path en el campo (inventario): SAVE es un paso aparte, a proposito.
    m_pathField->setText(QDir::toNativeSeparators(version.path));
    updateChosenVersionHighlight();
}

void OpenInNukeXPanel::onBrowseNukeXClicked()
{
    const QString start = m_pathField->text().trimmed().isEmpty() ? QStringLiteral("C:/Program Files")
                                                                   : QFileInfo(m_pathField->text()).absolutePath();
    const QString picked =
        QFileDialog::getOpenFileName(this, I18n::tr("Path to NukeX executable"), start, I18n::tr("Executable (*.exe)"));
    if (!picked.isEmpty()) {
        m_pathField->setText(QDir::toNativeSeparators(picked));
        updateChosenVersionHighlight();
    }
}

void OpenInNukeXPanel::onSaveNukeXClicked()
{
    const QString path = m_pathField->text().trimmed();
    if (path.isEmpty()) {
        report(OpenInNukeXMessages::chooseVersionFirst());
        return;
    }
    if (!QFile::exists(path)) {
        report(OpenInNukeXMessages::fileNoLongerExists());
        return;
    }
    if (!QFileInfo(path).fileName().contains(QStringLiteral("nuke"), Qt::CaseInsensitive)) {
        // Inventario: "Saving it anyway" — el aviso no bloquea el guardado.
        report(OpenInNukeXMessages::notANukeExecutable());
    }
    updateChosenVersionHighlight();
    if (m_context.automatedRun()) {
        qInfo("[openInNukeX] (automatedRun) Save: no se escribe nukeXpath.txt");
        return;
    }
    NukeXPath::write(m_nukeXPathFile, path);
    report(OpenInNukeXMessages::nukeVersionSaved(path));
}

void OpenInNukeXPanel::onShowLaunchNoticeToggled(bool checked)
{
    if (m_context.automatedRun()) {
        return;
    }
    m_context.setValue(QStringLiteral("showLaunchNotice"), checked);
}

// ================================================================== "Nuke Bridge"

QWidget *OpenInNukeXPanel::buildBridgeCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *head = new QHBoxLayout();
    head->addWidget(Ui::label(QStringLiteral("Nuke Bridge"), "cardTitle", card), 1);
    m_bridgeChip = new Chip(card);
    head->addWidget(m_bridgeChip, 0, Qt::AlignVCenter);
    layout->addLayout(head);

    auto *description = Ui::caption(
        I18n::tr("Lets LGA Mighty Tools find a running NukeX session and open .nk files directly in it."), card);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *dirRow = new QHBoxLayout();
    dirRow->setSpacing(6);
    m_nukeDirField = makePathField(card);
    m_nukeDirField->setPlaceholderText(I18n::tr("Path to your .nuke folder"));
    dirRow->addWidget(m_nukeDirField, 1);
    m_bridgeBrowseButton = Ui::button(I18n::tr("Browse..."), QString(), QString(), card);
    dirRow->addWidget(m_bridgeBrowseButton);
    m_installButton = Ui::button(I18n::tr("Install"), QStringLiteral("primary"), QString(), card);
    dirRow->addWidget(m_installButton);
    layout->addLayout(dirRow);

    // Texto enriquecido (el camino va en negrita): Ui::caption dibuja texto plano y mostraba las etiquetas
    // <b> tal cual. Mismo objectName que el caption, asi toma sus colores de Theme.
    m_bridgeHint = new RichLineLabel(QString(), 17, card);
    m_bridgeHint->setObjectName(QStringLiteral("caption"));
    m_bridgeHint->setWordWrap(true);
    m_bridgeHint->setVisible(false);
    layout->addWidget(m_bridgeHint);

    m_manualToggle = Ui::button(I18n::tr("Install manually instead..."), QStringLiteral("ghost"), QString(), card);
    layout->addWidget(m_manualToggle, 0, Qt::AlignLeft);

    m_manualPanel = new QWidget(card);
    auto *manualLayout = new QVBoxLayout(m_manualPanel);
    manualLayout->setContentsMargins(10, 10, 10, 10);
    manualLayout->setSpacing(8);
    manualLayout->addWidget(Ui::caption(I18n::tr("1. Export the bridge files with the button below."), m_manualPanel));
    manualLayout->addWidget(
        Ui::caption(I18n::tr("2. Copy the LGA_OpenInNukeX folder into your .nuke folder."), m_manualPanel));
    manualLayout->addWidget(
        Ui::caption(I18n::tr("3. Add this line to the init.py inside .nuke:"), m_manualPanel));
    auto *codeRow = new QHBoxLayout();
    auto *codeLabel = new QLabel(NukeBridge::pluginAddPathLine(), m_manualPanel);
    codeLabel->setObjectName(QStringLiteral("codeLine"));
    codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeRow->addWidget(codeLabel, 1);
    m_copyLineButton = Ui::button(I18n::tr("Copy line"), QString(), QStringLiteral("sm"), m_manualPanel);
    codeRow->addWidget(m_copyLineButton);
    manualLayout->addLayout(codeRow);
    m_exportButton = Ui::button(I18n::tr("Export bridge files..."), QStringLiteral("primary"), QString(), m_manualPanel);
    manualLayout->addWidget(m_exportButton, 0, Qt::AlignLeft);
    m_manualPanel->setVisible(false);
    layout->addWidget(m_manualPanel);

    if (m_interactive) {
        connect(m_bridgeBrowseButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onBrowseNukeDirClicked);
        connect(m_installButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onInstallClicked);
        connect(m_manualToggle, &QPushButton::clicked, this, &OpenInNukeXPanel::onToggleManualClicked);
        connect(m_copyLineButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onCopyLineClicked);
        connect(m_exportButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onExportClicked);
        connect(m_nukeDirField, &QLineEdit::editingFinished, this, [this]() {
            if (m_nukeDirField->hasFocus()) {
                m_nukeDirField->clearFocus();
                refreshBridgeStatus();
            }
        });
        m_nukeDirField->installEventFilter(this);
    }
    return card;
}

void OpenInNukeXPanel::refreshBridge()
{
    // Version COMPLETA: repone el campo desde el registro LGA/autodeteccion. Solo al construir el
    // panel y despues de un Install/Reinstall exitoso (recien publicado). Un refresco disparado
    // por el USUARIO editando el campo usa refreshBridgeStatus(), que nunca le pisa lo que tipeo.
    m_nukeDirField->setText(QDir::toNativeSeparators(NukeBridge::currentNukeDirectory()));
    refreshBridgeStatus();
}

void OpenInNukeXPanel::refreshBridgeStatus()
{
    const QString nukeDir = m_nukeDirField->text().trimmed();
    const NukeBridge::Status status = NukeBridge::inspect(nukeDir);
    const NukeBridge::ChipState chip = NukeBridge::chipState(status);

    QString chipTone;
    QString chipText;
    QString buttonText = I18n::tr("Install");
    QString buttonVariant;
    switch (chip) {
    case NukeBridge::ChipState::NotInstalled:
        chipTone = QStringLiteral("warn"); // falta un paso, como «Not associated»
        chipText = I18n::tr("Not installed");
        buttonVariant = QStringLiteral("primary");
        break;
    case NukeBridge::ChipState::Installed:
        chipTone = QStringLiteral("ok");
        // La instalada: igual a la embebida, o MAS NUEVA (otra copia de la app mas reciente).
        chipText = I18n::tr("Installed · v%1").arg(status.installedVersion);
        buttonText = I18n::tr("Reinstall");
        break;
    case NukeBridge::ChipState::UpdateAvailable:
        chipTone = QStringLiteral("warn");
        chipText = I18n::tr("Update available · v%1").arg(NukeBridge::bundledVersion());
        buttonText = I18n::tr("Reinstall");
        buttonVariant = QStringLiteral("primary");
        break;
    case NukeBridge::ChipState::InstalledUnknownVersion:
        chipTone = QStringLiteral("warn");
        chipText = I18n::tr("Installed · unknown version");
        buttonText = I18n::tr("Reinstall");
        buttonVariant = QStringLiteral("primary");
        break;
    }
    m_bridgeChip->set(chipTone, chipText);
    m_installButton->setText(buttonText);
    Ui::setStyleProperty(m_installButton, "variant", buttonVariant);

    if (nukeDir.isEmpty()) {
        m_bridgeHint->setText(I18n::tr("No Nuke folder found. Pick the one you use before installing."));
        Ui::setStyleProperty(m_bridgeHint, "tone", QStringLiteral("warn"));
        m_bridgeHint->setVisible(true);
    } else if (chip == NukeBridge::ChipState::NotInstalled) {
        m_bridgeHint->setText(I18n::tr("Found your Nuke folder at <b>%1</b>. Change it if you use a different one.").arg(QDir::toNativeSeparators(nukeDir).toHtmlEscaped()));
        Ui::setStyleProperty(m_bridgeHint, "tone", QString());
        m_bridgeHint->setVisible(true);
    } else {
        m_bridgeHint->setVisible(false);
    }
}

void OpenInNukeXPanel::onBrowseNukeDirClicked()
{
    const QString start = m_nukeDirField->text().trimmed().isEmpty() ? QDir::homePath() : m_nukeDirField->text();
    QFileDialog dialog(this, I18n::tr("Path to your .nuke folder"), start);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, false); // inventario: "incluye ocultas"
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) {
        m_nukeDirField->setText(QDir::toNativeSeparators(dialog.selectedFiles().first()));
        refreshBridgeStatus();
    }
}

void OpenInNukeXPanel::onInstallClicked()
{
    if (m_context.automatedRun()) {
        qInfo("[openInNukeX] (automatedRun) Install/Reinstall: no se instala el bridge de verdad");
        return;
    }
    // Sin guard de arbol de build: instalar el bridge desde un build es legitimo y no rompe nada si
    // el build se borra despues — el bridge instalado en `.nuke` sigue andando solo, no depende del
    // exe. (Apply/Re-apply solo se frena desde un build en mac, donde se asocia la ruta del bundle.)
    const QString dir = m_nukeDirField->text().trimmed();
    QString detail;
    const NukeBridge::Error err = NukeBridge::install(dir, &detail, /*automatedRun=*/false);
    if (err == NukeBridge::Error::None) {
        report(OpenInNukeXMessages::bridgeInstalled(QDir(dir).filePath(NukeBridge::pluginFolderName())));
    } else {
        report(OpenInNukeXMessages::bridgeError(err));
    }
    refreshBridge();
    emit bridgeStateChanged();
}

void OpenInNukeXPanel::onExportClicked()
{
    const QString dest = QFileDialog::getExistingDirectory(this, I18n::tr("Export bridge files"), QDir::homePath());
    if (dest.isEmpty()) {
        return;
    }
    if (m_context.automatedRun()) {
        qInfo("[openInNukeX] (automatedRun) Export bridge files: no se exporta de verdad");
        return;
    }
    QString detail;
    const NukeBridge::Error err = NukeBridge::exportPayload(dest, &detail);
    if (err == NukeBridge::Error::None) {
        report(OpenInNukeXMessages::bridgeExported(QDir(dest).filePath(NukeBridge::pluginFolderName())));
    } else {
        report(OpenInNukeXMessages::bridgeError(err));
    }
}

void OpenInNukeXPanel::onCopyLineClicked()
{
    QGuiApplication::clipboard()->setText(NukeBridge::pluginAddPathLine());
    m_copyLineButton->setText(I18n::tr("Copied"));
    QTimer::singleShot(1500, this, [this]() {
        if (m_copyLineButton) {
            m_copyLineButton->setText(I18n::tr("Copy line"));
        }
    });
}

void OpenInNukeXPanel::onToggleManualClicked()
{
    m_manualOpen = !m_manualOpen;
    m_manualPanel->setVisible(m_manualOpen);
    m_manualToggle->setText(m_manualOpen ? I18n::tr("Hide manual instructions")
                                        : I18n::tr("Install manually instead..."));
}

QWidget *OpenInNukeXPanel::buildOldClientNotice()
{
    auto *card = new StatusCard(this);
    card->set(QStringLiteral("warn"), I18n::tr("The old LGA OpenInNukeX is still installed"),
              oldClientCaption(), I18n::tr("Open Apps settings"), QString(), QStringLiteral("warn"),
              QStringLiteral("sm"));
    m_oldClientStatus = card;
    // "Uninstall old app", arriba de "Open Apps settings" y con su mismo aspecto (boton chico de
    // la tarjeta: objectName statusButton, btnSize sm). En columna y del mismo ancho: uno al lado
    // del otro ensanchaban la tarjeta mas que la pagina.
    m_uninstallOldButton = Ui::button(I18n::tr("Uninstall old app"), QString(), QStringLiteral("sm"), card);
    m_uninstallOldButton->setObjectName(QStringLiteral("statusButton"));
    if (auto *row = qobject_cast<QHBoxLayout *>(card->layout())) {
        const int index = row->indexOf(card->button());
        row->removeWidget(card->button());
        auto *buttons = new QVBoxLayout();
        buttons->setSpacing(6);
        buttons->addWidget(m_uninstallOldButton);
        buttons->addWidget(card->button());
        row->insertLayout(index, buttons);
        row->setAlignment(buttons, Qt::AlignVCenter);
    }
#ifndef Q_OS_WIN
    m_uninstallOldButton->setVisible(false);
#endif
    if (m_interactive) {
        connect(card->button(), &QPushButton::clicked, this,
                [this]() { QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:appsfeatures"))); });
        connect(m_uninstallOldButton, &QPushButton::clicked, this, &OpenInNukeXPanel::onUninstallOldClicked);
    }
    return card;
}

void OpenInNukeXPanel::onUninstallOldClicked()
{
    auto *operations = OpenInNukeXOperations::instance();
    if (!m_interactive || OpenInNukeXOperations::busy()) {
        return;
    }
#ifdef Q_OS_WIN
    showUninstalling();
    operations->startUninstall(AppPaths::isBuildTree(), m_context.automatedRun());
    updateBusyButtons();
#endif
}

// El estado "desinstalando": el boton deshabilitado y la leyenda que dice que se siga al desinstalador. Tambien lo
// pone un panel nuevo si la desinstalacion ya estaba en curso cuando se armo.
void OpenInNukeXPanel::showUninstalling()
{
    m_uninstallOldButton->setText(I18n::tr("Uninstalling..."));
    m_oldClientStatus->set(QStringLiteral("warn"), m_oldClientStatus->title(),
                           I18n::tr("Follow the uninstaller. Windows may ask for permission."),
                           m_oldClientStatus->button()->text(), QString(), QStringLiteral("warn"), QStringLiteral("sm"));
}

// Retoma lo que este en curso (Apply, desinstalacion) si el panel anterior se borro con eso corriendo, y recibe sus
// resultados: tambien uno que llego mientras no habia panel.
void OpenInNukeXPanel::attachToOperations()
{
    auto *operations = OpenInNukeXOperations::instance();
    connect(operations, &OpenInNukeXOperations::applyFinished, this, &OpenInNukeXPanel::onApplyFinished);
    connect(operations, &OpenInNukeXOperations::uninstallFinished, this, &OpenInNukeXPanel::onUninstallOldFinished);
    m_applyRunning = operations->applyRunning();
#ifdef Q_OS_WIN
    if (operations->uninstallRunning()) {
        m_oldClientCard->setVisible(true);
        showUninstalling();
    }
#endif
    updateBusyButtons();
    // Un resultado que llego sin panel se toma RECIEN al mostrarlo, y fuera del constructor (el de Apply abre un
    // cuadro modal): si este panel se borra antes (otro cambio de idioma), el resultado sigue esperando en
    // OpenInNukeXOperations y lo muestra el panel que lo reemplace.
    QTimer::singleShot(0, this, [this]() {
        auto *pending = OpenInNukeXOperations::instance();
        const auto apply = pending->takePendingApply();
        const auto uninstall = pending->takePendingUninstall();
        if (apply.valid) {
            onApplyFinished(apply.success, apply.needsConfirmation, apply.issues);
        }
        if (uninstall.valid) {
            onUninstallOldFinished(uninstall.stillInstalled, uninstall.launched);
        }
    });
}

// Los botones que escriben la asociacion (Apply y Uninstall old app) quedan deshabilitados mientras cualquiera de
// las dos operaciones este en curso.
void OpenInNukeXPanel::updateBusyButtons()
{
    const bool busy = OpenInNukeXOperations::busy();
    m_applyButton->setEnabled(!busy);
    m_uninstallOldButton->setEnabled(!busy);
}

void OpenInNukeXPanel::onUninstallOldFinished(bool stillInstalled, bool launched)
{
    updateBusyButtons();
    m_uninstallOldButton->setText(I18n::tr("Uninstall old app"));
    const QString caption = !stillInstalled ? oldClientCaption()
                            : launched      ? I18n::tr("It's still installed. Try again, or remove it from Apps settings.")
                                            : I18n::tr("Couldn't start its uninstaller. Remove it from Apps settings.");
    m_oldClientStatus->set(QStringLiteral("warn"), m_oldClientStatus->title(), caption, m_oldClientStatus->button()->text(),
                           QString(), QStringLiteral("warn"), QStringLiteral("sm"));
    m_oldClientCard->setVisible(stillInstalled);
    refreshAssociation();
    emit oldClientStateChanged();
}

// ================================================================== Captura (fixtures)

bool OpenInNukeXPanel::applyCaptureState(const QString &state)
{
    if (!kFixtureStates.contains(state)) {
        return false;
    }
    m_fixtureState = state;

    // Nada de esto lee el sistema: son datos de prueba fijos, como en el canvas. Rutas con
    // separador nativo (\ en Windows), como las que de verdad carga el campo (auditoria, punto 3).
    const NukeVersion v151{QStringLiteral("Nuke15.1v6"),
                          QDir::toNativeSeparators(QStringLiteral("C:/Program Files/Nuke15.1v6/Nuke15.1.exe")),
                          QStringLiteral("15.1v6"), QStringLiteral("Nuke 15.1v6")};
    const NukeVersion v160{QStringLiteral("Nuke16.0v4"),
                          QDir::toNativeSeparators(QStringLiteral("C:/Program Files/Nuke16.0v4/Nuke16.0.exe")),
                          QStringLiteral("16.0v4"), QStringLiteral("Nuke 16.0v4")};
    const NukeVersion v170{QStringLiteral("Nuke17.0v4"),
                          QDir::toNativeSeparators(QStringLiteral("C:/Program Files/Nuke17.0v4/Nuke17.0.exe")),
                          QStringLiteral("17.0v4"), QStringLiteral("Nuke 17.0v4")};

    const bool isOldClient = state == QStringLiteral("old-client");
    m_oldClientCard->setVisible(isOldClient);
    m_associationCard->setVisible(!isOldClient);
    m_versionCard->setVisible(!isOldClient);
    m_bridgeCard->setVisible(!isOldClient);

    if (isOldClient) {
        return true;
    }

    const bool assocOk = state != QStringLiteral("first-time");
    m_associated = assocOk;
    m_assocChip->set(assocOk ? QStringLiteral("ok") : QStringLiteral("warn"),
                     assocOk ? I18n::tr("Associated") : I18n::tr("Not associated"));
    m_applyButton->setText(assocOk ? I18n::tr("Re-apply") : I18n::tr("Apply"));
    Ui::setStyleProperty(m_applyButton, "variant", assocOk ? QString() : QStringLiteral("primary"));

    m_foundVersions.clear();
    if (state == QStringLiteral("scan-starting")) {
        m_scanStatusLabel->setText(I18n::tr("Scanning for installed Nuke versions…"));
    } else if (state == QStringLiteral("scan-walking") || state == QStringLiteral("first-time")) {
        m_scanStatusLabel->setText(I18n::tr("Scanning: %1").arg(QStringLiteral("...\\Program Files\\Nuke16.0v4")));
    } else if (state == QStringLiteral("scan-none") || state == QStringLiteral("no-nuke-manual-open")) {
        m_scanStatusLabel->setText(I18n::tr("No Nuke installations found in common locations"));
        Ui::setStyleProperty(m_scanStatusLabel, "tone", QStringLiteral("err"));
    } else {
        m_foundVersions = {v151, v160, v170};
        m_scanStatusLabel->setText(I18n::tr("%1 Nuke versions found:").arg(3));
        Ui::setStyleProperty(m_scanStatusLabel, "tone", QString());
    }
    m_scanChooseLabel->setVisible(!m_foundVersions.isEmpty());
    rebuildVersionButtons();
    const bool noVersionsFound = state == QStringLiteral("scan-none") || state == QStringLiteral("no-nuke-manual-open");
    m_pathField->setText((assocOk && !noVersionsFound) ? v170.path : QString());
    updateChosenVersionHighlight();

    // "Nuke Bridge"
    QString chipTone = QStringLiteral("ok");
    QString chipText = I18n::tr("Installed · v%1").arg(NukeBridge::bundledVersion());
    QString buttonText = I18n::tr("Reinstall");
    QString buttonVariant;
    bool hintVisible = false;
    QString hintText;
    QString hintTone;

    if (state == QStringLiteral("bridge-update")) {
        chipTone = QStringLiteral("warn");
        chipText = I18n::tr("Update available · v%1").arg(NukeBridge::bundledVersion());
        buttonVariant = QStringLiteral("primary");
    } else if (state == QStringLiteral("bridge-unknown")) {
        chipTone = QStringLiteral("warn");
        chipText = I18n::tr("Installed · unknown version");
        buttonVariant = QStringLiteral("primary");
    } else if (state == QStringLiteral("first-time") || state == QStringLiteral("scan-none")) {
        chipTone = QStringLiteral("warn"); // falta un paso, como «Not associated»
        chipText = I18n::tr("Not installed");
        buttonText = I18n::tr("Install");
        buttonVariant = QStringLiteral("primary");
        hintVisible = true;
        hintText = I18n::tr("Found your Nuke folder at <b>%1</b>. Change it if you use a different one.")
                       .arg(QStringLiteral("C:\\Users\\lega\\.nuke"));
    } else if (state == QStringLiteral("no-nuke-manual-open")) {
        chipTone = QStringLiteral("warn"); // falta un paso, como «Not associated»
        chipText = I18n::tr("Not installed");
        buttonText = I18n::tr("Install");
        buttonVariant = QStringLiteral("primary");
        hintVisible = true;
        hintText = I18n::tr("No Nuke folder found. Pick the one you use before installing.");
        hintTone = QStringLiteral("warn");
    }
    m_bridgeChip->set(chipTone, chipText);
    m_installButton->setText(buttonText);
    Ui::setStyleProperty(m_installButton, "variant", buttonVariant);
    m_bridgeHint->setText(hintText);
    Ui::setStyleProperty(m_bridgeHint, "tone", hintTone);
    m_bridgeHint->setVisible(hintVisible);
    m_nukeDirField->setText(state == QStringLiteral("no-nuke-manual-open") || state == QStringLiteral("first-time")
                                ? QString()
                                : QStringLiteral("C:\\Users\\lega\\.nuke"));

    m_manualOpen = state == QStringLiteral("no-nuke-manual-open");
    m_manualPanel->setVisible(m_manualOpen);
    m_manualToggle->setText(m_manualOpen ? I18n::tr("Hide manual instructions")
                                        : I18n::tr("Install manually instead..."));
    return true;
}

#include "OpenInNukeXPanel.moc"
