#ifndef MIGHTYTOOLS_CLEANUPWINDOW_H
#define MIGHTYTOOLS_CLEANUPWINDOW_H

#include "modules/diskspace/DiskSpace.h"
#include "modules/diskspace/cleanup/CleanupJob.h"
#include "modules/diskspace/cleanup/CleanupModel.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanEngine.h"
#include "modules/diskspace/cleanup/ScanSnapshot.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QWidget>

class CleanupPane;
class DiskState;
class ElidedLabel;
class ModuleContext;
class QAbstractButton;
class QFrame;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class SizeListView;
class TitleBar;
class UsageBar;

// Las pestanas de la ventana: una fila de titulos con la elegida subrayada en violeta.
class TabStrip : public QWidget
{
    Q_OBJECT

public:
    explicit TabStrip(QWidget *parent = nullptr);
    void setTabs(const QStringList &titles);
    void setCurrent(int index);
    int current() const { return m_current; }
    // Dato chico al lado del titulo ("Clean up  60.2 GB").
    void setBadge(int index, const QString &text);

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect tabRect(int index) const;
    int tabAt(const QPoint &pos) const;

    QStringList m_titles;
    QHash<int, QString> m_badges;
    int m_current = 0;
    int m_hovered = -1;
};

// La ventana "Disk Space · C:": que ocupa un disco, como lista ordenada por peso, y que se puede
// limpiar. Es una ventana aparte de la de herramientas; la abre y la posee DiskSpaceModule (apagar la
// herramienta la cierra).
//
// Lo apagado no consume: el motor de escaneo, el trabajo de borrado, el arbol en memoria y el timer
// nacen con la ventana y mueren con ella. Cerrarla no espera a ningun hilo (ver ScanEngine).
//
// En modo captura (QA) no hay motor ni disco: applyFixture() carga datos fijos y nada responde al click.
//
// Se estira desde los bordes (en Windows; ver platform/WindowFrame.h) y recuerda su tamano. Las tres
// listas se ordenan con un click en el titulo de una columna; un segundo click invierte el sentido. El
// orden de cada una tambien se recuerda.
class CleanupWindow : public QWidget
{
    Q_OBJECT

public:
    enum Tab { CleanUp = 0, Folders = 1, Files = 2, Changes = 3 };

    static constexpr int kWidth = 960;  ///< tamano de fabrica (el del canvas)
    static constexpr int kHeight = 620;
    static constexpr int kMinWidth = 860;
    static constexpr int kMinHeight = 440;

    // `state` y `context` son del modulo y viven mas que la ventana. `capture`: sin motor ni borrado.
    CleanupWindow(DiskState *state, ModuleContext *context, bool capture, QWidget *parent = nullptr);
    ~CleanupWindow() override;

    // Abre (o cambia a) ese disco y esa pestana, y escanea si hace falta. `root`: "C:/".
    void openOn(const QString &root, Tab tab);
    QString root() const { return m_root; }

    // ---- QA
    static QStringList fixtureStates();
    // Captura: la franja de acceso total al disco a la vista (macOS, D-43).
    void showAccessStripForCapture();
    // Carga un estado fijo (los del canvas). false si no lo conoce.
    bool applyFixture(const QString &state);
    // Ordena la lista de esa pestana como si se hubiera hecho click en el titulo de `column`
    // (SizeListView::kNameColumn o el indice de la columna). Lo usan la captura y la sonda.
    void sortList(Tab tab, int column);
    SizeListView *listForTab(Tab tab) const;

protected:
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    enum class ScanState { Idle, Scanning, Complete, Stopped };
    enum class JobKind { None, CleanUp, Manual };

    // Por que columna esta ordenada una lista (SizeListView::kNameColumn o el indice) y en que sentido.
    struct SortOrder
    {
        int column = 0;
        bool descending = true;
    };

    // Una fila elegida de Folders o de Largest files.
    struct Picked
    {
        bool isDir = false;
        ScanTree::Index dir = ScanTree::kNone; ///< la carpeta, o la que contiene al archivo
        QString name;                          ///< archivo
        QString path;
        qint64 bytes = 0;
        qint64 files = 0;
        qint64 modified = 0; ///< ultimo cambio (segundos desde 1970)
    };
    struct FileRow
    {
        ScanEngine::TopFile file;
        QString dirPath;
    };

    void buildUi();
    void startScan();
    void stopScan();
    void poll();
    void onScanFinished();
    void onJobFinished();

    void refreshHeader();
    void refreshTabs();
    void refreshActionBar();
    void refreshFolders();
    void refreshFiles();
    void refreshChanges();
    void refreshCategories();
    // Vuelve a armar y a medir las reglas contra el arbol (con el escaneo completo).
    void remeasure();
    void reloadDrive();

    void setTab(int tab);
    SortOrder &sortFor(Tab tab);
    void loadSortOrders();
    void saveSortOrder(Tab tab);
    // El tamano guardado (o el de fabrica), sin pasarse del area util de la pantalla.
    void restoreSize();
    void showDriveMenu();
    void showCompareMenu();
    // Compara el escaneo recien hecho contra ese resumen (vacio: nada con que comparar).
    void applyBaseline(const ScanSnapshot &baseline);
    void setFolderColumns(bool scanning);
    void toggleFolder(const QString &rowId);
    QList<Picked> pickedItems() const;
    // Lo elegido sin lo que cuelga de otra carpeta elegida (ya viaja con ella): lo que de verdad se borra.
    QList<Picked> topPickedItems() const;
    void revealPicked();
    // "Export for AI...": lo elegido para borrar, como texto con la pregunta hecha (copiar o guardar).
    void exportForAi();
    // Un aviso corto en la barra de abajo, que se va solo.
    void flash(const QString &text, bool error = false);
    void deletePicked(bool toTrash);
    void runCleanup();
    void addFolderRule();
    void removeFolderRule(const QString &categoryId);
    QList<Cleanup::FolderRule> loadFolderRules() const;
    void saveFolderRules(const QList<Cleanup::FolderRule> &rules);
    void reviewPath(const QString &path);
    bool busy() const;

    DiskState *m_state = nullptr;
    ModuleContext *m_context = nullptr;
    bool m_capture = false;

    QString m_root;       ///< "C:/", como lo nombra DiskState
    QString m_scanRoot;   ///< la ruta real que se escanea ("C:\")
    DriveInfo m_drive;
    bool m_driveKnown = false;
    DeleteGuard m_guard;

    ScanEngine m_engine;
    CleanupJob m_job;
    QTimer *m_poll = nullptr;
    ScanState m_scanState = ScanState::Idle;
    bool m_fullScan = false; ///< la pasada en curso es un escaneo completo (no una relectura)
    QDateTime m_scannedAt;
    ScanEngine::Progress m_progress;

    QList<Cleanup::Category> m_categories;
    QList<Cleanup::Category> m_tickSource; ///< las del escaneo anterior: de ahi sale lo tildado tras un Rescan
    QString m_banner;
    JobKind m_jobKind = JobKind::None;
    QList<CleanupJob::Request> m_requests;
    QList<Picked> m_jobItems; ///< borrado a mano: lo elegido, en el orden de los pedidos
    qint64 m_freeBeforeJob = 0;
    QString m_notice; ///< resultado del ultimo borrado a mano, en la barra de abajo
    QString m_flash;  ///< aviso que se va solo ("Copied.")
    bool m_flashError = false;
    QTimer *m_flashTimer = nullptr;

    QSet<ScanTree::Index> m_expanded;
    QHash<ScanTree::Index, ScanEngine::FileListing> m_listings;
    QHash<QString, Picked> m_folderRows; ///< id de fila -> que es
    QList<FileRow> m_fileRows;
    int m_fileFilter = 0;
    SortOrder m_folderSort;
    SortOrder m_fileSort;
    SortOrder m_changeSort;
    ScanSnapshot m_baseline;            ///< el resumen con el que se compara
    ScanSnapshot m_snapshot;            ///< el del escaneo recien hecho
    QList<ScanSnapshot> m_baselines;    ///< los anteriores disponibles ("Compare with...")
    QSet<ScanTree::Index> m_fixtureCounting; ///< captura: carpetas que se muestran a medio contar
    int m_folderColumns = -1;           ///< 0 completas, 1 escaneando (para no rearmarlas en cada refresco)
    QList<ScanSnapshot::Change> m_changes;
    qint64 m_usedDelta = 0;

    TitleBar *m_titleBar = nullptr;
    QAbstractButton *m_driveButton = nullptr;
    QLabel *m_freeLabel = nullptr;
    ElidedLabel *m_scanLabel = nullptr;
    UsageBar *m_bar = nullptr;
    QPushButton *m_rescan = nullptr;
    QWidget *m_scanLine = nullptr;
    // macOS sin acceso total al disco (D-43): avisa que lo privado no se escaneo y lleva a Ajustes.
    QFrame *m_accessStrip = nullptr;
    bool m_captureAccessStrip = false; ///< captura: la franja a la vista
    void refreshAccessStrip();
    TabStrip *m_tabs = nullptr;
    QStackedWidget *m_stack = nullptr;
    CleanupPane *m_cleanPane = nullptr;
    SizeListView *m_folders = nullptr;
    SizeListView *m_files = nullptr;
    SizeListView *m_changesList = nullptr;
    QList<QPushButton *> m_filterChips;
    QLabel *m_actionText = nullptr;
    QPushButton *m_actionPrimary = nullptr;
    QPushButton *m_actionReveal = nullptr;
    QPushButton *m_actionTrash = nullptr;
    QPushButton *m_actionDelete = nullptr;
    QPushButton *m_actionCompare = nullptr;
    QPushButton *m_actionExport = nullptr;
    bool m_nativeFrameApplied = false;
    bool m_shownOnce = false; ///< el tamano guardado y el centrado van solo la primera vez
    bool m_sizeRestored = false; ///< ya se mostro con el tamano guardado: desde ahi el tamano se guarda
};

#endif // MIGHTYTOOLS_CLEANUPWINDOW_H
