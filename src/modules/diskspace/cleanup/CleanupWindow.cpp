#include "modules/diskspace/cleanup/CleanupWindow.h"

#include "app/ModuleContext.h"
#include "core/I18n.h"
#include "modules/diskspace/DiskCard.h"
#include "modules/diskspace/DiskState.h"
#include "core/AutomatedRun.h"
#include "modules/diskspace/cleanup/CleanupDialogs.h"
#include "modules/diskspace/cleanup/CleanupExport.h"
#include "modules/diskspace/cleanup/CleanupFixture.h"
#include "modules/diskspace/cleanup/CleanupPane.h"
#include "modules/diskspace/cleanup/CleanupRules.h"
#include "modules/diskspace/cleanup/SizeListView.h"
#include "platform/FileSystemOps.h"
#include "platform/LocalDrives.h"
#include "platform/SystemPaths.h"
#include "platform/WindowFrame.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"
#include "ui/UiWidgets.h"

#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int kPollMs = 200;
constexpr int kFilesPerFolder = 200;   ///< archivos que se listan al desplegar una carpeta
constexpr qint64 kUnreadableMin = 100LL * 1024 * 1024;
constexpr qint64 kChangeMin = ScanSnapshot::kMinBytes;
constexpr qint64 kBaselineAgeSecs = 3600; ///< un resumen mas nuevo que esto no se toma como referencia

// "4.69 M" o "284,000": cantidad de archivos para la cabecera.
QString countText(quint64 count)
{
    if (count >= 1000000) {
        return QString::number(double(count) / 1000000.0, 'f', 2) + QStringLiteral(" M");
    }
    return QLocale(QLocale::English).toString(qulonglong(count));
}

QString grouped(qint64 count)
{
    return QLocale(QLocale::English).toString(count);
}

// "today 12:41", "yesterday 18:10" o la fecha entera.
QString dayTimeText(const QDateTime &when, const QDateTime &now)
{
    const QString time = when.toString(QStringLiteral("HH:mm"));
    const qint64 days = when.date().daysTo(now.date());
    if (days == 0) {
        return I18n::tr("today %1").arg(time);
    }
    if (days == 1) {
        return I18n::tr("yesterday %1").arg(time);
    }
    return when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// Alguna categoria ya paso por la medicion (sus tildes son los del usuario, no los de fabrica).
bool hasMeasured(const QList<Cleanup::Category> &categories)
{
    for (const Cleanup::Category &category : categories) {
        for (const Cleanup::Item &item : category.items) {
            if (item.measured) {
                return true;
            }
        }
    }
    return false;
}

// Id de fila de "Largest files": la carpeta y el nombre, no la posicion (la lista se rearma).
QString fileRowId(quint32 dir, const QString &name)
{
    return QStringLiteral("t%1|%2").arg(dir).arg(name);
}

QString strong(const QString &text)
{
    return QStringLiteral("<span style=\"font-weight:600; color:%1\">%2</span>").arg(QLatin1String(Theme::kTextBright), text);
}

QString joinPath(const QString &dir, const QString &name)
{
    return dir.endsWith(QDir::separator()) ? dir + name : dir + QDir::separator() + name;
}

// Lo que decide el lugar de una fila al ordenar: el nombre, el numero de la columna elegida y el peso,
// que desempata.
struct SortKey
{
    QString name;
    qint64 number = 0;
    qint64 bytes = 0;
};

int compareNames(const QString &a, const QString &b)
{
    // Como el Explorador: sin distinguir mayusculas y con los numeros por su valor ("2" antes de "10").
    // A mano y no con QCollator: sin ICU, QCollator llama al sistema en cada comparacion, y ordenar una
    // carpeta de 28.000 hijos (WinSxS) llevaba 120 ms con el arbol trabado, cinco veces por segundo.
    const int lengthA = int(a.size());
    const int lengthB = int(b.size());
    int i = 0;
    int j = 0;
    while (i < lengthA && j < lengthB) {
        const QChar ca = a.at(i);
        const QChar cb = b.at(j);
        if (ca.isDigit() && cb.isDigit()) {
            // Dos tramos de cifras: sin los ceros de adelante, el mas largo es el mayor; a igual largo,
            // cifra por cifra.
            int startA = i;
            int startB = j;
            while (startA < lengthA && a.at(startA) == QLatin1Char('0')) {
                ++startA;
            }
            while (startB < lengthB && b.at(startB) == QLatin1Char('0')) {
                ++startB;
            }
            int endA = startA;
            int endB = startB;
            while (endA < lengthA && a.at(endA).isDigit()) {
                ++endA;
            }
            while (endB < lengthB && b.at(endB).isDigit()) {
                ++endB;
            }
            if (endA - startA != endB - startB) {
                return endA - startA < endB - startB ? -1 : 1;
            }
            for (int k = 0; k < endA - startA; ++k) {
                if (a.at(startA + k) != b.at(startB + k)) {
                    return a.at(startA + k) < b.at(startB + k) ? -1 : 1;
                }
            }
            i = endA;
            j = endB;
            continue;
        }
        const char16_t fa = ca.toCaseFolded().unicode();
        const char16_t fb = cb.toCaseFolded().unicode();
        if (fa != fb) {
            return fa < fb ? -1 : 1;
        }
        ++i;
        ++j;
    }
    if (lengthA - i != lengthB - j) {
        return lengthA - i < lengthB - j ? -1 : 1;
    }
    // Iguales salvo mayusculas o ceros: un orden fijo igual (std::sort necesita uno estricto).
    return a < b ? -1 : (b < a ? 1 : 0);
}

// `a` va antes que `b`. Los empates van por peso (mayor primero) y despues por nombre.
bool sortsBefore(const SortKey &a, const SortKey &b, bool byName, bool descending)
{
    const int order = byName ? compareNames(a.name, b.name) : (a.number < b.number ? -1 : (a.number > b.number ? 1 : 0));
    if (order != 0) {
        return descending ? order > 0 : order < 0;
    }
    if (a.bytes != b.bytes) {
        return a.bytes > b.bytes;
    }
    return compareNames(a.name, b.name) < 0;
}

// Nombre de cada lista en settings.ini y cuantas columnas tiene ademas del nombre.
const char *sortSettingName(CleanupWindow::Tab tab)
{
    return tab == CleanupWindow::Folders ? "folders" : (tab == CleanupWindow::Files ? "files" : "changes");
}

int sortColumnCount(CleanupWindow::Tab tab)
{
    return tab == CleanupWindow::Folders ? 4 : 2;
}

// La linea de 2 px debajo de la cabecera: un tramo violeta que recorre el ancho mientras hay trabajo.
class ScanLine : public QWidget
{
public:
    explicit ScanLine(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(2);
        m_clock.start();
    }
    void setActive(bool active)
    {
        if (active != m_active) {
            m_active = active;
            update();
        }
    }
    // La captura de QA la deja quieta en un lugar fijo.
    void setFrozen(bool frozen) { m_frozen = frozen; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        if (!m_active) {
            return;
        }
        painter.fillRect(rect(), Theme::color(Theme::kTile));
        const int segment = int(width() * 0.38);
        const int travel = width() + segment;
        const int x = m_frozen ? 0 : int((m_clock.elapsed() % 1600) / 1600.0 * travel) - segment;
        painter.fillRect(QRect(x, 0, segment, height()), Theme::color(Theme::kAccent));
    }

private:
    QElapsedTimer m_clock;
    bool m_active = false;
    bool m_frozen = false;
};

// El boton del disco (`.drive` del canvas): la letra en su keycap, el nombre del volumen y la flecha.
class DriveButton : public QAbstractButton
{
public:
    explicit DriveButton(QWidget *parent)
        : QAbstractButton(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }
    void setDrive(const QString &label, const QString &name)
    {
        m_label = label;
        m_name = name;
        updateGeometry();
        update();
    }
    QSize sizeHint() const override
    {
        // 6 + keycap + 8 + nombre + 8 + flecha + 8, con 28 de alto.
        const int name = m_name.isEmpty() ? 0 : QFontMetrics(Theme::uiFont(13, QFont::Medium)).horizontalAdvance(m_name) + 8;
        return QSize(6 + keycapWidth() + 8 + name + 8 + 8, 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(underMouse() ? QColor(0x3d, 0x3d, 0x3d) : Theme::color(Theme::kFieldBorder), 1.0));
        painter.setBrush(underMouse() ? QColor(0x1f, 0x1f, 0x1f) : Theme::color(Theme::kField));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        // Keycap (`.kc`): 20 de alto, borde y fondo propios.
        const QRectF keycap(6.5, (height() - 20) / 2.0 + 0.5, keycapWidth() - 1, 19);
        painter.setPen(QPen(QColor(0x38, 0x38, 0x38), 1.0));
        painter.setBrush(QColor(0x2b, 0x2b, 0x2b));
        painter.drawRoundedRect(keycap, 4, 4);
        painter.setFont(Theme::uiFont(11.5, QFont::DemiBold));
        painter.setPen(QColor(0xc5, 0xc8, 0xc7));
        painter.drawText(keycap, Qt::AlignCenter, m_label);
        int x = 6 + keycapWidth() + 8;
        if (!m_name.isEmpty()) {
            painter.setFont(Theme::uiFont(13, QFont::Medium));
            painter.setPen(Theme::color(Theme::kTextStrong));
            const int width = QFontMetrics(painter.font()).horizontalAdvance(m_name);
            painter.drawText(QRect(x, 0, width, height()), Qt::AlignLeft | Qt::AlignVCenter, m_name);
            x += width + 8;
        }
        Icons::paint(painter, Icon::ChevronDown, QRectF(x, (height() - 8) / 2.0, 8, 8), Theme::color(Theme::kTextMuted));
    }

private:
    int keycapWidth() const { return QFontMetrics(Theme::uiFont(11.5, QFont::DemiBold)).horizontalAdvance(m_label) + 14; }

    QString m_label;
    QString m_name;
};

// Tipos de archivo de los filtros de "Largest files".
bool matchesFilter(int filter, const QString &name, qint64 modified, qint64 now)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    switch (filter) {
    case 1:
        return QStringList{QStringLiteral("mov"), QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
                           QStringLiteral("mxf"), QStringLiteral("r3d"), QStringLiteral("braw"), QStringLiteral("webm"),
                           QStringLiteral("m4v"), QStringLiteral("wmv")}
            .contains(suffix);
    case 2:
        return QStringList{QStringLiteral("vhdx"), QStringLiteral("vhd"), QStringLiteral("vmdk"), QStringLiteral("iso"),
                           QStringLiteral("img"), QStringLiteral("wim"), QStringLiteral("esd"), QStringLiteral("qcow2")}
            .contains(suffix);
    case 3:
        return QStringList{QStringLiteral("msi"), QStringLiteral("msp"), QStringLiteral("msix"), QStringLiteral("appx"),
                           QStringLiteral("cab"), QStringLiteral("pkg"), QStringLiteral("dmg")}
            .contains(suffix);
    case 4:
        return QStringList{QStringLiteral("dmp"), QStringLiteral("mdmp"), QStringLiteral("db"), QStringLiteral("sqlite"),
                           QStringLiteral("sqlite3"), QStringLiteral("mdf"), QStringLiteral("ldf"), QStringLiteral("log")}
            .contains(suffix);
    case 5:
        return modified > 0 && now - modified >= 365LL * 86400;
    default:
        return true;
    }
}

} // namespace

// ------------------------------------------------------------------ TabStrip

TabStrip::TabStrip(QWidget *parent)
    : QWidget(parent)
{
    // `.tabs`: 34 de alto mas el borde de abajo.
    setFixedHeight(35);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
}

void TabStrip::setTabs(const QStringList &titles)
{
    m_titles = titles;
    update();
}

void TabStrip::setCurrent(int index)
{
    if (index == m_current) {
        return;
    }
    m_current = index;
    update();
}

void TabStrip::setBadge(int index, const QString &text)
{
    if (m_badges.value(index) == text) {
        return;
    }
    m_badges.insert(index, text);
    update();
}

QRect TabStrip::tabRect(int index) const
{
    // `.tabs` con 12 de relleno; cada `.tab` con 12 a los lados, 7 entre titulo y dato, 2 entre pestanas.
    const QFontMetrics title(Theme::uiFont(13, QFont::Medium));
    const QFontMetrics badge(Theme::uiFont(11.5));
    int x = 12;
    for (int i = 0; i < m_titles.size(); ++i) {
        int width = 24 + title.horizontalAdvance(m_titles.at(i));
        const QString text = m_badges.value(i);
        if (!text.isEmpty()) {
            width += 7 + badge.horizontalAdvance(text);
        }
        if (i == index) {
            return QRect(x, 0, width, 34);
        }
        x += width + 2;
    }
    return QRect();
}

int TabStrip::tabAt(const QPoint &pos) const
{
    for (int i = 0; i < m_titles.size(); ++i) {
        if (tabRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void TabStrip::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(QRect(0, height() - 1, width(), 1), Theme::color(Theme::kDivider));
    const QFont titleFont = Theme::uiFont(13, QFont::Medium);
    const QFont badgeFont = Theme::uiFont(11.5);
    for (int i = 0; i < m_titles.size(); ++i) {
        const QRect rect = tabRect(i);
        const bool current = i == m_current;
        painter.setFont(titleFont);
        painter.setPen(Theme::color(current ? Theme::kTextBright : (i == m_hovered ? Theme::kTextStrong : Theme::kTextMuted)));
        const int titleWidth = QFontMetrics(titleFont).horizontalAdvance(m_titles.at(i));
        painter.drawText(QRect(rect.left() + 12, 0, titleWidth, 34), Qt::AlignLeft | Qt::AlignVCenter, m_titles.at(i));
        const QString badge = m_badges.value(i);
        if (!badge.isEmpty()) {
            painter.setFont(badgeFont);
            painter.setPen(Theme::color(Theme::kTextFaint));
            painter.drawText(QRect(rect.left() + 12 + titleWidth + 7, 0, rect.width(), 34), Qt::AlignLeft | Qt::AlignVCenter, badge);
        }
        if (current) {
            painter.fillRect(QRect(rect.left(), 32, rect.width(), 2), Theme::color(Theme::kAccent));
        }
    }
}

void TabStrip::mousePressEvent(QMouseEvent *event)
{
    const int index = event->button() == Qt::LeftButton ? tabAt(event->pos()) : -1;
    if (index >= 0 && index != m_current) {
        m_current = index;
        update();
        emit currentChanged(index);
    }
}

void TabStrip::mouseMoveEvent(QMouseEvent *event)
{
    const int index = tabAt(event->pos());
    if (index != m_hovered) {
        m_hovered = index;
        setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TabStrip::leaveEvent(QEvent *event)
{
    m_hovered = -1;
    update();
    QWidget::leaveEvent(event);
}

// ------------------------------------------------------------------ CleanupWindow

CleanupWindow::CleanupWindow(DiskState *state, ModuleContext *context, bool capture, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_context(context)
    , m_capture(capture)
{
    setObjectName(QStringLiteral("cleanupWindow"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("LGA Mighty Tools - Disk Space"));
    // La barra de titulo la dibuja la app, como en la ventana principal.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    buildUi();
    if (m_capture) {
        setFixedSize(kWidth, kHeight);
    } else {
        // El tamano guardado se aplica al mostrarla (restoreSize), cuando ya se sabe en que pantalla esta.
        setMinimumSize(kMinWidth, kMinHeight);
        resize(kWidth, kHeight);
    }
    loadSortOrders();
    m_poll = new QTimer(this);
    m_poll->setInterval(kPollMs);
    connect(m_poll, &QTimer::timeout, this, &CleanupWindow::poll);
    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    m_flashTimer->setInterval(5000);
    connect(m_flashTimer, &QTimer::timeout, this, [this]() {
        m_flash.clear();
        refreshActionBar();
    });
}

CleanupWindow::~CleanupWindow()
{
    // Nada espera a un hilo: el motor y el trabajo levantan su bandera y los hilos terminan solos.
    m_engine.cancel();
    m_job.cancel();
    // El tamano se guarda al irse (cerrarla, o apagar la herramienta con la ventana abierta).
    if (m_context && m_sizeRestored) {
        const QSize size = isMaximized() ? normalGeometry().size() : this->size();
        m_context->setValue(QStringLiteral("cleanup/windowWidth"), size.width());
        m_context->setValue(QStringLiteral("cleanup/windowHeight"), size.height());
    }
}

bool CleanupWindow::busy() const
{
    return m_jobKind != JobKind::None || m_scanState == ScanState::Scanning;
}

void CleanupWindow::refreshAccessStrip()
{
#ifdef Q_OS_MACOS
    m_accessStrip->setVisible(m_capture ? m_captureAccessStrip : !SystemPaths::hasFullDiskAccess());
#else
    m_accessStrip->setVisible(false);
#endif
}

void CleanupWindow::showAccessStripForCapture()
{
    m_captureAccessStrip = true;
    refreshAccessStrip();
}

void CleanupWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // Al volver de Ajustes: si ya se dio el permiso, la franja se va (el proximo Rescan ve todo).
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && !m_capture) {
        refreshAccessStrip();
    }
}

void CleanupWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = new TitleBar(QStringLiteral("Disk Space"), false, this);
    root->addWidget(m_titleBar);

    // ---- Cabecera (`.dhead`): disco, cuanto queda libre, estado del escaneo y Rescan.
    auto *header = new QWidget(this);
    auto *head = new QHBoxLayout(header);
    head->setContentsMargins(16, 12, 16, 12);
    head->setSpacing(14);
    m_driveButton = new DriveButton(header);
    head->addWidget(m_driveButton, 0, Qt::AlignVCenter);

    auto *meter = new QVBoxLayout();
    meter->setContentsMargins(0, 0, 0, 0);
    meter->setSpacing(3);
    auto *meterLine = new QHBoxLayout();
    meterLine->setSpacing(10);
    // Lo libre va entero; el estado del escaneo toma lo que queda y, con la ventana angosta, se recorta
    // (el texto entero, en su tooltip).
    m_freeLabel = Ui::label(QString(), "meterText", header);
    m_freeLabel->setTextFormat(Qt::RichText);
    meterLine->addWidget(m_freeLabel, 0);
    m_scanLabel = new ElidedLabel(header);
    m_scanLabel->setObjectName(QStringLiteral("meterText"));
    m_scanLabel->setAlignment(Qt::AlignRight);
    meterLine->addWidget(m_scanLabel, 1);
    meter->addLayout(meterLine);
    m_bar = new UsageBar(header);
    meter->addWidget(m_bar);
    head->addLayout(meter, 1);

    m_rescan = Ui::button(I18n::tr("Rescan"), QString(), QStringLiteral("sm"), header);
    Ui::setIcon(m_rescan, Icon::Refresh, Theme::color(Theme::kText), 13);
    head->addWidget(m_rescan, 0, Qt::AlignVCenter);
    root->addWidget(header);

    m_scanLine = new ScanLine(this);
    root->addWidget(m_scanLine);

    m_accessStrip = new QFrame(this);
    m_accessStrip->setObjectName(QStringLiteral("accessStrip"));
    auto *access = new QHBoxLayout(m_accessStrip);
    access->setContentsMargins(16, 7, 16, 7);
    access->setSpacing(10);
    QLabel *accessText = Ui::label(I18n::tr("Private folders of macOS (Desktop, Documents, other apps' data...) are not "
                                            "scanned. Give Full Disk Access to see everything."),
                                   "accessText", m_accessStrip);
    accessText->setWordWrap(true);
    access->addWidget(accessText, 1);
    auto *accessButton = Ui::button(I18n::tr("Open Settings"), QString(), QStringLiteral("sm"), m_accessStrip);
    access->addWidget(accessButton, 0, Qt::AlignVCenter);
    connect(accessButton, &QPushButton::clicked, this, []() { SystemPaths::openFullDiskAccessSettings(); });
    m_accessStrip->setVisible(false);
    root->addWidget(m_accessStrip);

    m_tabs = new TabStrip(this);
    m_tabs->setTabs({I18n::tr("Clean up"), I18n::tr("Folders"), I18n::tr("Largest files"), I18n::tr("What changed")});
    root->addWidget(m_tabs);

    // ---- Las cuatro pestanas.
    m_stack = new QStackedWidget(this);
    m_cleanPane = new CleanupPane(m_stack);
    m_cleanPane->setOpen(QStringLiteral("python"), true);
    m_stack->addWidget(m_cleanPane);

    m_folders = new SizeListView(m_stack);
    setFolderColumns(false);
    m_stack->addWidget(m_folders);

    auto *filesPage = new QWidget(m_stack);
    auto *filesLayout = new QVBoxLayout(filesPage);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    filesLayout->setSpacing(0);
    auto *filters = new QWidget(filesPage);
    auto *filterRow = new QHBoxLayout(filters);
    // `.filters`: 10 / 16 de relleno, 6 entre filtros.
    filterRow->setContentsMargins(16, 10, 16, 10);
    filterRow->setSpacing(6);
    const QStringList filterNames = {I18n::trc("filter", "All"),          I18n::trc("filter", "Video"),
                                     I18n::trc("filter", "Disk images"),  I18n::trc("filter", "Installers"),
                                     I18n::trc("filter", "Dumps and databases"), I18n::trc("filter", "Not changed in a year")};
    for (int i = 0; i < filterNames.size(); ++i) {
        auto *chip = Ui::button(filterNames.at(i), QString(), QString(), filters);
        chip->setObjectName(QStringLiteral("filterChip"));
        chip->setCheckable(true);
        chip->setChecked(i == 0);
        connect(chip, &QPushButton::clicked, this, [this, i]() {
            m_fileFilter = i;
            m_files->clearSelection();
            refreshFiles();
        });
        filterRow->addWidget(chip);
        m_filterChips.append(chip);
    }
    filterRow->addStretch(1);
    filesLayout->addWidget(filters);
    auto *filterRule = new QFrame(filesPage);
    filterRule->setObjectName(QStringLiteral("divider"));
    filesLayout->addWidget(filterRule);
    m_files = new SizeListView(filesPage);
    m_files->setTree(false);
    m_files->setColumns(I18n::trc("column", "Name"), {{I18n::trc("column", "Size"), 84, SizeListColumn::Kind::Size},
                                                      {I18n::trc("column", "Modified"), 72, SizeListColumn::Kind::Text}});
    filesLayout->addWidget(m_files, 1);
    m_stack->addWidget(filesPage);

    m_changesList = new SizeListView(m_stack);
    m_changesList->setTree(false);
    m_changesList->setColumns(I18n::trc("column", "Folder"), {{I18n::trc("column", "Change"), 96, SizeListColumn::Kind::Size},
                                                              {I18n::trc("column", "Size now"), 84, SizeListColumn::Kind::Text}});
    m_stack->addWidget(m_changesList);
    root->addWidget(m_stack, 1);

    // ---- Barra de acciones (`.abar`).
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("actionBar"));
    bar->setMinimumHeight(48);
    auto *actions = new QHBoxLayout(bar);
    actions->setContentsMargins(16, 8, 16, 8);
    actions->setSpacing(10);
    m_actionText = Ui::label(QString(), "actionText", bar);
    m_actionText->setTextFormat(Qt::RichText);
    actions->addWidget(m_actionText, 1);
    // Lo elegido para borrar, para preguntarle a un asistente de IA antes de hacerlo.
    // (Sin tooltip: lo que hace lo explica el cartel que abre.)
    m_actionExport = Ui::button(I18n::tr("Export for AI..."), QString(), QStringLiteral("sm"), bar);
#ifdef Q_OS_MACOS
    m_actionReveal = Ui::button(I18n::tr("Show in Finder"), QString(), QStringLiteral("sm"), bar);
#else
    m_actionReveal = Ui::button(I18n::tr("Show in Explorer"), QString(), QStringLiteral("sm"), bar);
#endif
    Ui::setIcon(m_actionReveal, Icon::Reveal, Theme::color(Theme::kText), 13);
#ifdef Q_OS_MACOS
    m_actionTrash = Ui::button(I18n::tr("Move to Trash"), QString(), QStringLiteral("sm"), bar);
#else
    m_actionTrash = Ui::button(I18n::tr("Move to Recycle Bin"), QString(), QStringLiteral("sm"), bar);
#endif
    Ui::setIcon(m_actionTrash, Icon::Trash, Theme::color(Theme::kText), 13);
    m_actionDelete = Ui::button(I18n::tr("Delete permanently"), QStringLiteral("danger"), QStringLiteral("sm"), bar);
    // "Compare with... v": el texto y, a su derecha, la flecha.
    // (El espacio final separa el texto de la flecha: con el orden invertido, Qt los pega.)
    m_actionCompare = Ui::button(I18n::tr("Compare with...") + QStringLiteral("  "), QString(), QStringLiteral("sm"), bar);
    Ui::setIcon(m_actionCompare, Icon::ChevronDown, Theme::color(Theme::kText), 11);
    m_actionCompare->setLayoutDirection(Qt::RightToLeft);
    m_actionPrimary = Ui::button(QString(), QStringLiteral("primary"), QString(), bar);
    for (QPushButton *button : {m_actionExport, m_actionReveal, m_actionTrash, m_actionDelete, m_actionCompare, m_actionPrimary}) {
        actions->addWidget(button, 0, Qt::AlignVCenter);
    }
    root->addWidget(bar);

    // Ordenar no toca el disco: anda tambien en la captura (la sonda lo usa; ahi nada hace click).
    for (const Tab tab : {Folders, Files, Changes}) {
        connect(listForTab(tab), &SizeListView::sortRequested, this, [this, tab](int column) { sortList(tab, column); });
    }

    if (m_capture) {
        m_cleanPane->setInteractive(true); // dibujado con sus controles habilitados; nada esta conectado
        for (SizeListView *list : {m_folders, m_files, m_changesList}) {
            list->setInteractive(false);
        }
        static_cast<ScanLine *>(m_scanLine)->setFrozen(true);
        return;
    }

    connect(m_tabs, &TabStrip::currentChanged, this, &CleanupWindow::setTab);
    connect(m_driveButton, &QAbstractButton::clicked, this, &CleanupWindow::showDriveMenu);
    connect(m_rescan, &QPushButton::clicked, this, [this]() {
        if (m_scanState == ScanState::Scanning && m_fullScan) {
            stopScan();
        } else if (!busy()) {
            startScan();
        }
    });
    connect(m_cleanPane, &CleanupPane::categoryToggled, this, [this](const QString &id) {
        for (Cleanup::Category &category : m_categories) {
            if (category.id != id) {
                continue;
            }
            const bool all = CleanupRules::checkState(category) == 2;
            for (Cleanup::Item &item : category.items) {
                item.checked = !all && !item.blocked;
            }
        }
        refreshCategories();
    });
    connect(m_cleanPane, &CleanupPane::itemToggled, this, [this](const QString &id, int index) {
        for (Cleanup::Category &category : m_categories) {
            if (category.id == id && index >= 0 && index < category.items.size() && !category.items.at(index).blocked) {
                category.items[index].checked = !category.items.at(index).checked;
            }
        }
        refreshCategories();
    });
    connect(m_cleanPane, &CleanupPane::reviewRequested, this, &CleanupWindow::reviewPath);
    connect(m_cleanPane, &CleanupPane::systemCleanupRequested, this, []() { SystemPaths::openSystemCleanup(); });
    connect(m_cleanPane, &CleanupPane::addRuleRequested, this, &CleanupWindow::addFolderRule);
    connect(m_cleanPane, &CleanupPane::removeRuleRequested, this, &CleanupWindow::removeFolderRule);

    connect(m_folders, &SizeListView::expanderClicked, this, &CleanupWindow::toggleFolder);
    for (SizeListView *list : {m_folders, m_files}) {
        connect(list, &SizeListView::selectionChanged, this, [this]() {
            m_notice.clear();
            m_flash.clear();
            refreshActionBar();
        });
        connect(list, &SizeListView::activated, this, [this](const QString &) { revealPicked(); });
    }
    connect(m_actionReveal, &QPushButton::clicked, this, &CleanupWindow::revealPicked);
    connect(m_actionTrash, &QPushButton::clicked, this, [this]() { deletePicked(true); });
    connect(m_actionDelete, &QPushButton::clicked, this, [this]() { deletePicked(false); });
    connect(m_actionCompare, &QPushButton::clicked, this, &CleanupWindow::showCompareMenu);
    connect(m_actionExport, &QPushButton::clicked, this, &CleanupWindow::exportForAi);
    connect(m_actionPrimary, &QPushButton::clicked, this, [this]() {
        if (m_jobKind != JobKind::None) {
            m_job.cancel();
        } else {
            runCleanup();
        }
    });
}

// ------------------------------------------------------------------ disco y escaneo

void CleanupWindow::reloadDrive()
{
    if (m_capture) {
        return;
    }
    DriveInfo drive;
    m_driveKnown = LocalDrives::query(m_root, &drive);
    if (m_driveKnown) {
        m_drive = drive;
    }
}

void CleanupWindow::openOn(const QString &root, Tab tab)
{
    const bool sameDrive = root == m_root && m_scanState != ScanState::Idle;
    if (!sameDrive && !busy()) {
        if (root != m_root) {
            // Otro disco: lo tildado del anterior no viaja.
            m_categories.clear();
            m_tickSource.clear();
        }
        m_root = root;
        reloadDrive();
        m_guard = DeleteGuard::forVolume(m_root);
        startScan();
    }
    m_tabs->setCurrent(tab);
    setTab(tab);
    show();
    raise();
    activateWindow();
}

void CleanupWindow::startScan()
{
    if (m_capture || m_root.isEmpty()) {
        return;
    }
    // La ruta real del disco: con una unidad `subst` se escanea (y se protege) la carpeta de verdad.
    const QString real = FileSystemOps::canonicalPath(m_root);
    m_scanRoot = real.isEmpty() ? QDir::toNativeSeparators(m_root) : real;
    m_expanded.clear();
    m_listings.clear();
    m_folderRows.clear();
    m_fileRows.clear();
    m_changes.clear();
    m_baseline = ScanSnapshot();
    m_snapshot = ScanSnapshot();
    m_baselines.clear();
    m_banner.clear();
    m_notice.clear();
    m_cleanPane->setBanner(QString());
    m_cleanPane->setSkipped(QString(), {});
    m_folders->clearSelection();
    m_files->clearSelection();

    // Lo que el usuario tildo o destildo se guarda aparte: las categorias se rearman sin medir, y al
    // terminar el escaneo se vuelven a tildar como estaban.
    if (hasMeasured(m_categories)) {
        m_tickSource = m_categories;
    }
    CleanupRules::Context context;
    context.bases = SystemPaths::cleanupBases();
    context.volumeRoot = m_guard.volumeRoot;
    m_categories = CleanupRules::build(context);

    m_engine.start(m_scanRoot, SystemPaths::scanExclusions());
    m_fullScan = true;
    m_scanState = ScanState::Scanning;
    m_progress = m_engine.progress();
    // Los archivos sueltos de la raiz (la memoria virtual, por ejemplo) se ven desde el primer momento.
    m_listings.insert(0, ScanEngine::listFiles(m_scanRoot, kFilesPerFolder));
    m_poll->start();
    refreshHeader();
    refreshCategories();
    refreshFolders();
    refreshFiles();
    refreshChanges();
    refreshActionBar();
}

void CleanupWindow::stopScan()
{
    m_engine.cancel();
}

void CleanupWindow::poll()
{
    static_cast<ScanLine *>(m_scanLine)->update();
    if (m_jobKind != JobKind::None) {
        if (!m_job.isRunning()) {
            onJobFinished();
        } else {
            refreshActionBar();
        }
    } else if (m_scanState == ScanState::Scanning) {
        m_progress = m_engine.progress();
        if (!m_engine.isRunning()) {
            onScanFinished();
        } else {
            refreshHeader();
            if (m_tabs->current() == Folders) {
                refreshFolders();
            }
        }
    }
    if (!busy()) {
        m_poll->stop();
    }
}

void CleanupWindow::onScanFinished()
{
    m_progress = m_engine.progress();
    m_scanState = m_progress.complete ? ScanState::Complete : ScanState::Stopped;
    reloadDrive();
    if (m_scanState == ScanState::Complete) {
        const QDateTime now = QDateTime::currentDateTime();
        if (m_fullScan) {
            m_scannedAt = now;
            // "What changed": contra el resumen anterior de este disco, si es de hace una hora o mas;
            // si no, contra el de antes (dos escaneos seguidos no corren la referencia). El otro queda
            // a mano en "Compare with...".
            ScanSnapshot snapshot;
            {
                const auto lock = m_engine.lock();
                snapshot = ScanSnapshot::fromTree(m_engine.tree(), m_drive.totalBytes - m_drive.freeBytes, now);
            }
            const ScanSnapshot last = ScanSnapshot::loadCurrent(snapshot.root);
            const ScanSnapshot previous = ScanSnapshot::loadPrevious(snapshot.root);
            const bool lastIsOld = last.isValid() && last.takenAt.secsTo(now) >= kBaselineAgeSecs;
            ScanSnapshot::store(snapshot, lastIsOld);
            m_snapshot = snapshot;
            m_baselines.clear();
            for (const ScanSnapshot &candidate : {last, previous}) {
                if (candidate.isValid()) {
                    m_baselines.append(candidate);
                }
            }
            applyBaseline(lastIsOld || !previous.isValid() ? last : previous);
        }
        // Los archivos mas pesados, con la ruta de su carpeta ya resuelta.
        const QList<ScanEngine::TopFile> top = m_engine.topFiles();
        m_fileRows.clear();
        {
            const auto lock = m_engine.lock();
            for (const ScanEngine::TopFile &file : top) {
                m_fileRows.append(FileRow{file, m_engine.tree().path(file.dir)});
            }
        }
        // Los archivos sueltos de las carpetas desplegadas se vuelven a leer: pudieron cambiar.
        const QList<ScanTree::Index> listed = m_listings.keys();
        for (const ScanTree::Index index : listed) {
            QString path;
            bool alive = false;
            {
                const auto lock = m_engine.lock();
                alive = m_engine.tree().isAlive(index);
                if (alive) {
                    path = m_engine.tree().path(index);
                }
            }
            if (alive) {
                m_listings.insert(index, ScanEngine::listFiles(path, kFilesPerFolder));
            } else {
                m_listings.remove(index);
            }
        }
    }
    if (m_scanState == ScanState::Complete) {
        remeasure();
    }
    for (SizeListView *list : {m_folders, m_files}) {
        list->setInteractive(true);
    }
    m_cleanPane->setInteractive(true);
    refreshHeader();
    refreshCategories();
    refreshFolders();
    refreshFiles();
    refreshChanges();
    refreshActionBar();
}

void CleanupWindow::remeasure()
{
    if (m_capture) {
        return;
    }
    CleanupRules::Context context;
    context.bases = SystemPaths::cleanupBases();
    context.volumeRoot = m_guard.volumeRoot;
    context.folderRules = loadFolderRules();
    m_categories = CleanupRules::refresh(context, m_engine, hasMeasured(m_categories) ? m_categories : m_tickSource);
}

// ------------------------------------------------------------------ lo que se ve

void CleanupWindow::refreshHeader()
{
    const QString label = m_driveKnown ? m_drive.label : DiskSpace::labelForRoot(m_root, QString());
    const QString name = m_driveKnown ? DiskSpace::displayName(m_drive.name) : QString();
    m_titleBar->setTitle(QStringLiteral("Disk Space · %1").arg(label));
    static_cast<DriveButton *>(m_driveButton)->setDrive(label, name);

    DiskWatch watch;
    bool watched = false;
    for (const DiskWatch &candidate : m_state->diskWatches()) {
        if (candidate.root == m_root) {
            watch = candidate;
            watched = true;
        }
    }
    const bool low = watched && m_driveKnown && DiskSpace::isLow(watch, m_drive);
    if (m_driveKnown) {
        const QString freeText = DiskSpace::formatSize(m_drive.freeBytes);
        const QString totalText = DiskSpace::formatBytes(m_drive.totalBytes);
        // Las mismas dos claves que la fila del disco en el panel.
        QString text = low ? I18n::tr("<span style=\"font-weight:600\">%1 free</span> of %2 · under %3")
                                 .arg(freeText, totalText, DiskSpace::thresholdText(watch))
                           : I18n::tr("<span style=\"font-weight:600\">%1 free</span> of %2").arg(freeText, totalText);
        text.replace(QStringLiteral("font-weight:600"),
                     QStringLiteral("font-weight:600; color:%1").arg(QLatin1String(low ? Theme::kWarn : Theme::kTextStrong)));
        m_freeLabel->setText(text);
        const double total = double(m_drive.totalBytes);
        const double used = total <= 0 ? 0.0 : double(m_drive.totalBytes - m_drive.freeBytes) / total;
        const double mark = watched && total > 0 ? double(m_drive.totalBytes - DiskSpace::thresholdBytes(watch, m_drive.totalBytes)) / total
                                                 : -1.0;
        m_bar->set(true, qBound(0.0, used, 1.0), mark < 0 ? -1.0 : qBound(0.0, mark, 1.0), low);
    } else {
        m_freeLabel->setText(I18n::tr("Not connected"));
        m_bar->set(false, 0, -1.0, false);
    }

    QString scan;
    const QString seconds = QString::number(qMax(1, qRound(m_progress.seconds)));
    switch (m_scanState) {
    case ScanState::Idle:
        break;
    case ScanState::Scanning:
        // Mientras cuenta, el numero entero (se lo ve crecer); al terminar, abreviado.
        scan = m_fullScan ? I18n::tr("Scanning… %1 files · %2 s").arg(grouped(qint64(m_progress.files)), seconds) : I18n::tr("Updating…");
        break;
    case ScanState::Complete:
        scan = I18n::tr("Scanned %1 · %2 files · %3 s")
                   .arg(dayTimeText(m_scannedAt, QDateTime::currentDateTime()), countText(m_progress.files), seconds);
        break;
    case ScanState::Stopped:
        scan = I18n::tr("Stopped · %1 files").arg(countText(m_progress.files));
        break;
    }
    m_scanLabel->setText(scan);
    const bool scanning = m_scanState == ScanState::Scanning && m_fullScan;
    m_rescan->setText(scanning ? I18n::tr("Stop") : I18n::tr("Rescan"));
    Ui::setIcon(m_rescan, scanning ? Icon::X : Icon::Refresh, Theme::color(Theme::kText), scanning ? 11 : 13);
    m_rescan->setEnabled(m_capture || scanning || !busy());
    static_cast<ScanLine *>(m_scanLine)->setActive(busy());
}

void CleanupWindow::refreshTabs()
{
    const qint64 selected = CleanupRules::selectedBytes(m_categories);
    m_tabs->setBadge(CleanUp, m_scanState == ScanState::Complete && selected > 0 ? DiskSpace::formatSize(selected) : QString());
}

void CleanupWindow::refreshCategories()
{
    m_cleanPane->setCategories(m_categories, m_scanState != ScanState::Complete);
    refreshTabs();
    refreshActionBar();
}

void CleanupWindow::setFolderColumns(bool scanning)
{
    if (m_folderColumns == int(scanning)) {
        return;
    }
    m_folderColumns = int(scanning);
    // Escaneando, el porcentaje es de lo contado hasta ahora, y archivos y fecha todavia no dicen nada.
    QList<SizeListColumn> columns = {{I18n::trc("column", "Size"), 84, SizeListColumn::Kind::Size},
                                     {scanning ? I18n::trc("column", "% so far") : I18n::trc("column", "% of parent"), 132,
                                      SizeListColumn::Kind::Share}};
    if (!scanning) {
        columns.append({I18n::trc("column", "Files"), 86, SizeListColumn::Kind::Text});
        columns.append({I18n::trc("column", "Modified"), 72, SizeListColumn::Kind::Text});
    }
    m_folders->setColumns(I18n::trc("column", "Name"), columns);
}

void CleanupWindow::refreshFolders()
{
    QList<SizeListRow> rows;
    m_folderRows.clear();
    const QDateTime now = QDateTime::currentDateTime();
    const bool scanning = m_scanState == ScanState::Scanning;
    setFolderColumns(scanning && m_fullScan);
    // Escaneando no estan Files ni Modified: mientras tanto se ordena por peso, sin perder lo elegido.
    const bool sortHidden = scanning && m_fullScan && m_folderSort.column >= 2;
    const int sortColumn = sortHidden ? 0 : m_folderSort.column;
    const bool sortDescending = sortHidden || m_folderSort.descending;
    m_folders->setSort(sortColumn, sortDescending);
    // Size y "% of parent" ordenan igual: la parte de cada fila es su peso sobre el de la misma carpeta.
    const auto folderNumber = [sortColumn](qint64 bytes, qint64 files, qint64 modified) {
        return sortColumn == 2 ? files : (sortColumn == 3 ? modified : bytes);
    };
    quint64 rootBytes = 0;
    {
        const auto lock = m_engine.lock();
        const ScanTree &tree = m_engine.tree();
        if (!tree.isEmpty()) {
            rootBytes = tree.node(tree.root()).bytes;
            // Carpetas y archivos de un nivel, juntos, en el orden elegido (de fabrica, de mayor a menor).
            struct Entry
            {
                bool isDir = false;
                ScanTree::Index dir = ScanTree::kNone;
                int file = -1;
                quint64 bytes = 0;
                SortKey key;
            };
            const bool byName = sortColumn == SizeListView::kNameColumn;
            std::function<void(ScanTree::Index, int)> addLevel = [&](ScanTree::Index node, int depth) {
                QList<Entry> entries;
                for (const ScanTree::Index child : tree.children(node)) {
                    const ScanTree::Node &n = tree.node(child);
                    Entry entry{true, child, -1, n.bytes, {}};
                    entry.key = {tree.name(child), folderNumber(qint64(n.bytes), qint64(n.files), qint64(n.newest)), qint64(n.bytes)};
                    entries.append(entry);
                }
                const ScanEngine::FileListing listing = m_listings.value(node);
                for (int i = 0; i < listing.largest.size(); ++i) {
                    const ScanEngine::FileEntry &file = listing.largest.at(i);
                    Entry entry{false, node, i, file.bytes, {}};
                    entry.key = {file.name, folderNumber(qint64(file.bytes), 1, qint64(file.modified)), qint64(file.bytes)};
                    entries.append(entry);
                }
                std::sort(entries.begin(), entries.end(), [byName, sortDescending](const Entry &a, const Entry &b) {
                    return sortsBefore(a.key, b.key, byName, sortDescending);
                });
                const double parentBytes = double(qMax<quint64>(1, tree.node(node).bytes));
                const QString nodePath = tree.path(node);
                for (const Entry &entry : entries) {
                    SizeListRow row;
                    row.depth = depth;
                    const double share = qBound(0.0, double(entry.bytes) / parentBytes, 1.0);
                    const QString shareText = share < 0.01 ? QStringLiteral("<1%") : QStringLiteral("%1%").arg(qRound(share * 100.0));
                    Picked picked;
                    if (entry.isDir) {
                        const ScanTree::Node &n = tree.node(entry.dir);
                        row.id = QStringLiteral("d%1").arg(entry.dir);
                        row.name = tree.name(entry.dir);
                        const bool hasInside = n.dirs > 0 || n.ownFiles > 0;
                        const bool open = m_expanded.contains(entry.dir);
                        row.expander = !hasInside ? SizeListRow::Expander::None
                                                  : (open ? SizeListRow::Expander::Open : SizeListRow::Expander::Closed);
                        if (scanning && (!tree.isComplete(entry.dir) || m_fixtureCounting.contains(entry.dir))) {
                            row.note = I18n::tr("counting…");
                        }
                        row.cells = {DiskSpace::formatSize(qint64(n.bytes)), shareText, n.files > 0 ? grouped(n.files) : QString(),
                                     DiskSpace::ageText(n.newest, now)};
                        picked.isDir = true;
                        picked.dir = entry.dir;
                        picked.path = joinPath(nodePath, row.name);
                        picked.bytes = qint64(n.bytes);
                        picked.files = n.files;
                        picked.modified = n.newest;
                    } else {
                        const ScanEngine::FileEntry &file = listing.largest.at(entry.file);
                        row.id = QStringLiteral("f%1|%2").arg(node).arg(file.name);
                        row.name = file.name;
                        row.icon = Icon::File;
                        row.cells = {DiskSpace::formatSize(qint64(file.bytes)), shareText, QString(), DiskSpace::ageText(file.modified, now)};
                        picked.dir = node;
                        picked.name = file.name;
                        picked.path = joinPath(nodePath, file.name);
                        picked.bytes = qint64(file.bytes);
                        picked.files = 1;
                        picked.modified = file.modified;
                    }
                    row.share = share;
                    row.tooltip = picked.path;
                    // En la raiz, lo que es del sistema lleva su etiqueta: se ve pero no se borra desde aca.
                    if (depth == 0 && m_guard.check(picked.path, DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::ProtectedTree) {
                        row.chip = I18n::trc("chip", "system");
                    }
                    m_folderRows.insert(row.id, picked);
                    rows.append(row);
                    if (entry.isDir && m_expanded.contains(entry.dir)) {
                        addLevel(entry.dir, depth + 1);
                    }
                }
                if (listing.restCount > 0) {
                    SizeListRow rest;
                    rest.id = QStringLiteral("r%1").arg(node);
                    rest.depth = depth;
                    rest.hasIcon = false;
                    rest.muted = true;
                    rest.selectable = false;
                    rest.name = listing.restCount == 1 ? I18n::tr("1 smaller file") : I18n::tr("%1 smaller files").arg(grouped(listing.restCount));
                    rest.cells = {DiskSpace::formatSize(qint64(listing.restBytes)), QString(), QString(), QString()};
                    rows.append(rest);
                }
            };
            addLevel(tree.root(), 0);
        }
    }
    // Lo que el sistema dice que esta usado y el escaneo no vio: carpetas sin permiso y datos del propio disco.
    if (m_scanState == ScanState::Complete && m_driveKnown) {
        const qint64 hidden = (m_drive.totalBytes - m_drive.freeBytes) - qint64(rootBytes);
        if (hidden >= kUnreadableMin) {
            SizeListRow row;
            row.id = QStringLiteral("x");
            row.hasIcon = false;
            row.muted = true;
            row.selectable = false;
#ifdef Q_OS_MACOS
            // Lo del sistema (snapshots, memoria virtual, otros volumenes del contenedor) y lo privado.
            row.name = I18n::tr("System data and folders not readable");
#else
            row.name = I18n::tr("Not readable without administrator");
#endif
            row.cells = {DiskSpace::formatSize(hidden), QString(), QString(), QString()};
            rows.append(row);
        }
    }
    m_folders->setEmptyText(scanning ? I18n::tr("Scanning…") : QString());
    m_folders->setRows(rows);
}

void CleanupWindow::refreshFiles()
{
    QList<SizeListRow> rows;
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 nowSecs = now.toSecsSinceEpoch();
    // Columnas: 0 Size, 1 Modified.
    QList<int> order;
    QList<SortKey> keys;
    for (int i = 0; i < m_fileRows.size(); ++i) {
        const ScanEngine::TopFile &file = m_fileRows.at(i).file;
        keys.append({file.name, m_fileSort.column == 1 ? qint64(file.modified) : qint64(file.bytes), qint64(file.bytes)});
        if (matchesFilter(m_fileFilter, file.name, file.modified, nowSecs)) {
            order.append(i);
        }
    }
    const bool byName = m_fileSort.column == SizeListView::kNameColumn;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return sortsBefore(keys.at(a), keys.at(b), byName, m_fileSort.descending); });
    m_files->setSort(m_fileSort.column, m_fileSort.descending);
    for (const int i : order) {
        const FileRow &entry = m_fileRows.at(i);
        SizeListRow row;
        row.id = fileRowId(entry.file.dir, entry.file.name);
        row.name = entry.file.name;
        row.detail = entry.dirPath;
        row.icon = Icon::File;
        row.tooltip = joinPath(entry.dirPath, entry.file.name);
        row.cells = {DiskSpace::formatSize(qint64(entry.file.bytes)), DiskSpace::ageText(entry.file.modified, now)};
        rows.append(row);
    }
    for (int i = 0; i < m_filterChips.size(); ++i) {
        m_filterChips.at(i)->setChecked(i == m_fileFilter);
    }
    m_files->setEmptyText(m_scanState == ScanState::Complete ? I18n::tr("No files of this kind among the largest ones.")
                                                             : I18n::tr("The largest files appear when the scan finishes."));
    m_files->setRows(rows);
}

void CleanupWindow::refreshChanges()
{
    QList<SizeListRow> rows;
    // Columnas: 0 Change (lo que crecio, con signo), 1 Size now.
    QList<ScanSnapshot::Change> changes = m_changes;
    const bool byName = m_changeSort.column == SizeListView::kNameColumn;
    const int column = m_changeSort.column;
    const bool descending = m_changeSort.descending;
    std::sort(changes.begin(), changes.end(), [=](const ScanSnapshot::Change &a, const ScanSnapshot::Change &b) {
        return sortsBefore({a.path, column == 1 ? a.bytes : a.delta, qAbs(a.delta)}, {b.path, column == 1 ? b.bytes : b.delta, qAbs(b.delta)},
                           byName, descending);
    });
    m_changesList->setSort(m_changeSort.column, m_changeSort.descending);
    for (const ScanSnapshot::Change &change : changes) {
        SizeListRow row;
        row.id = change.path;
        row.name = change.path;
        row.tooltip = change.path;
        row.selectable = false;
        row.tone = change.delta > 0 ? 1 : 2;
        row.cells = {(change.delta > 0 ? QStringLiteral("+") : QStringLiteral("−")) + DiskSpace::formatSize(qAbs(change.delta)),
                     DiskSpace::formatSize(change.bytes)};
        rows.append(row);
    }
    QString empty;
    if (m_scanState != ScanState::Complete) {
        empty = I18n::tr("What changed appears when the scan finishes.");
    } else if (!m_baseline.isValid()) {
        empty = I18n::tr("Nothing to compare yet. The next scan shows what grew since this one.");
    } else {
        empty = I18n::tr("No folder changed by 100 MB or more.");
    }
    m_changesList->setEmptyText(empty);
    m_changesList->setRows(rows);
}

QList<CleanupWindow::Picked> CleanupWindow::pickedItems() const
{
    QList<Picked> items;
    if (m_tabs->current() == Folders) {
        for (const QString &id : m_folders->selectedIds()) {
            if (m_folderRows.contains(id)) {
                items.append(m_folderRows.value(id));
            }
        }
    } else if (m_tabs->current() == Files) {
        const QStringList selectedIds = m_files->selectedIds();
        const QSet<QString> selected(selectedIds.begin(), selectedIds.end());
        for (const FileRow &entry : m_fileRows) {
            if (selected.contains(fileRowId(entry.file.dir, entry.file.name))) {
                Picked picked;
                picked.dir = entry.file.dir;
                picked.name = entry.file.name;
                picked.path = joinPath(entry.dirPath, entry.file.name);
                picked.bytes = qint64(entry.file.bytes);
                picked.files = 1;
                picked.modified = entry.file.modified;
                items.append(picked);
            }
        }
    }
    return items;
}

void CleanupWindow::refreshActionBar()
{
    const QString label = m_driveKnown ? m_drive.label : DiskSpace::labelForRoot(m_root, QString());
    QString text;
    bool reveal = false;
    bool trash = false;
    bool remove = false;
    bool compare = false;
    bool exportShown = false;
    bool exportEnabled = false;
    bool trashEnabled = false;
    bool removeEnabled = false;
    QString primary;
    bool primaryEnabled = false;
    bool primaryStrong = true;

    const int tab = m_tabs->current();
    if (m_jobKind != JobKind::None) {
        const CleanupJob::Progress progress = m_job.progress();
        text = I18n::tr("Deleting… %1 files · %2").arg(grouped(progress.files), DiskSpace::formatSize(progress.bytes));
        primary = I18n::tr("Stop");
        primaryEnabled = true;
        primaryStrong = false;
    } else if (tab == CleanUp) {
        const qint64 selected = CleanupRules::selectedBytes(m_categories);
        if (m_scanState == ScanState::Scanning) {
            text = QStringLiteral("<span style=\"color:%1\">%2</span>")
                       .arg(QLatin1String(Theme::kTextFaint), I18n::tr("Scanning… what can be cleaned appears when it finishes."));
            primary = I18n::tr("Clean up");
        } else if (m_scanState != ScanState::Complete) {
            text = QStringLiteral("<span style=\"color:%1\">%2</span>")
                       .arg(QLatin1String(Theme::kTextFaint), I18n::tr("The scan was stopped. Press Rescan to see what can be cleaned."));
            primary = I18n::tr("Clean up");
        } else if (selected > 0) {
            text = I18n::tr("%1 · %2 goes from %3 to %4 free")
                       .arg(strong(I18n::tr("%1 selected").arg(DiskSpace::formatSize(selected))), label,
                            DiskSpace::formatSize(m_drive.freeBytes), DiskSpace::formatSize(m_drive.freeBytes + selected));
            primary = I18n::tr("Clean up %1").arg(DiskSpace::formatSize(selected));
            primaryEnabled = true;
            exportShown = true;
            exportEnabled = true;
        } else {
            text = QStringLiteral("<span style=\"color:%1\">%2</span>").arg(QLatin1String(Theme::kTextFaint), I18n::tr("Nothing selected."));
            primary = I18n::tr("Clean up");
            exportShown = true;
        }
    } else if (tab == Changes) {
        if (m_scanState == ScanState::Complete && m_baseline.isValid()) {
            const QString delta = (m_usedDelta >= 0 ? QStringLiteral("+") : QStringLiteral("−")) + DiskSpace::formatSize(qAbs(m_usedDelta));
            text = I18n::tr("%1 since the scan from %2").arg(strong(delta), dayTimeText(m_baseline.takenAt, QDateTime::currentDateTime()));
            compare = true;
        }
    } else {
        const QList<Picked> items = pickedItems();
        if (items.isEmpty()) {
#ifdef Q_OS_MACOS
            QString hint = I18n::tr("Select folders or files to delete them. Double click opens a folder in Finder.");
#else
            QString hint = I18n::tr("Select folders or files to delete them. Double click opens a folder in Explorer.");
#endif
            if (!m_notice.isEmpty()) {
                hint = m_notice;
            } else if (tab == Folders && m_scanState == ScanState::Scanning && m_fullScan) {
                hint = I18n::tr("The list is usable while it fills in. Sizes grow as folders are counted.");
            }
            text = QStringLiteral("<span style=\"color:%1\">%2</span>").arg(QLatin1String(Theme::kTextFaint), hint);
        } else {
            qint64 total = 0;
            bool viewOnly = false;
            bool trashAllowed = true;
            bool removeAllowed = true;
            for (const Picked &item : items) {
                total += item.bytes;
                const DeleteGuard::Verdict toTrash = m_guard.check(item.path, DeleteGuard::Scope::Entire, true);
                const DeleteGuard::Verdict forGood = m_guard.check(item.path, DeleteGuard::Scope::Entire, false);
                trashAllowed = trashAllowed && toTrash == DeleteGuard::Verdict::Ok;
                removeAllowed = removeAllowed && forGood == DeleteGuard::Verdict::Ok;
                viewOnly = viewOnly || (toTrash != DeleteGuard::Verdict::Ok && forGood != DeleteGuard::Verdict::Ok);
            }
            text = strong(I18n::tr("%1 selected · %2").arg(items.size()).arg(DiskSpace::formatSize(total)));
            if (viewOnly) {
                text += QStringLiteral(" <span style=\"color:%1\">· %2</span>")
                            .arg(QLatin1String(Theme::kTextFaint),
#ifdef Q_OS_MACOS
                                 I18n::tr("Protected: macOS and installed apps are not deleted from here."));
#else
                                 I18n::tr("Protected: Windows and installed programs are not deleted from here."));
#endif
            }
            const bool ready = m_capture || m_scanState == ScanState::Complete;
            reveal = true;
            trash = true;
            remove = true;
            trashEnabled = ready && trashAllowed;
            removeEnabled = ready && removeAllowed;
            // Solo si algo de lo elegido se puede borrar: de lo protegido no hay nada que preguntar.
            exportShown = true;
            exportEnabled = ready && (trashAllowed || removeAllowed);
        }
    }
    // El aviso que se va solo tapa el texto de la barra mientras dura.
    if (!m_flash.isEmpty() && m_jobKind == JobKind::None) {
        text = QStringLiteral("<span style=\"color:%1\">%2</span>")
                   .arg(QLatin1String(m_flashError ? Theme::kError : Theme::kOk), m_flash.toHtmlEscaped());
    }

    m_actionText->setText(text);
    m_actionExport->setVisible(exportShown);
    m_actionExport->setEnabled(exportEnabled);
    m_actionReveal->setVisible(reveal);
    m_actionTrash->setVisible(trash);
    m_actionTrash->setEnabled(trashEnabled);
    m_actionDelete->setVisible(remove);
    m_actionDelete->setEnabled(removeEnabled);
    m_actionCompare->setVisible(compare);
    m_actionPrimary->setVisible(!primary.isEmpty());
    m_actionPrimary->setText(primary);
    m_actionPrimary->setEnabled(primaryEnabled);
    Ui::setStyleProperty(m_actionPrimary, "variant", primaryStrong ? QStringLiteral("primary") : QString());
}

void CleanupWindow::setTab(int tab)
{
    m_flash.clear();
    m_stack->setCurrentIndex(tab);
    if (tab == Folders) {
        refreshFolders();
    }
    refreshActionBar();
}

SizeListView *CleanupWindow::listForTab(Tab tab) const
{
    switch (tab) {
    case Folders:
        return m_folders;
    case Files:
        return m_files;
    case Changes:
        return m_changesList;
    case CleanUp:
        break;
    }
    return nullptr;
}

CleanupWindow::SortOrder &CleanupWindow::sortFor(Tab tab)
{
    return tab == Folders ? m_folderSort : (tab == Files ? m_fileSort : m_changeSort);
}

void CleanupWindow::sortList(Tab tab, int column)
{
    SizeListView *list = listForTab(tab);
    if (!list || column < SizeListView::kNameColumn || column >= sortColumnCount(tab)) {
        return;
    }
    // Una columna nueva arranca por lo mas pesado, lo mas nuevo o lo que mas crecio; el nombre, de la A a la Z.
    // Se compara con lo que la lista muestra: escaneando, Folders va por Size aunque lo elegido sea Files
    // o Modified, y un click en Size tiene que invertir lo que se ve.
    SortOrder &order = sortFor(tab);
    if (list->sortColumn() == column) {
        order.column = column;
        order.descending = !list->sortDescending();
    } else {
        order.column = column;
        order.descending = column != SizeListView::kNameColumn;
    }
    saveSortOrder(tab);
    // Lo elegido se queda (las filas conservan su id); la vista vuelve arriba, al principio del orden nuevo.
    list->verticalScrollBar()->setValue(0);
    if (tab == Folders) {
        refreshFolders();
    } else if (tab == Files) {
        refreshFiles();
    } else {
        refreshChanges();
    }
}

void CleanupWindow::loadSortOrders()
{
    if (!m_context) {
        return;
    }
    for (const Tab tab : {Folders, Files, Changes}) {
        const QString prefix = QStringLiteral("cleanup/sort/%1/").arg(QLatin1String(sortSettingName(tab)));
        bool ok = false;
        const int column = m_context->value(prefix + QStringLiteral("column")).toInt(&ok);
        if (ok && column >= SizeListView::kNameColumn && column < sortColumnCount(tab)) {
            sortFor(tab).column = column;
            sortFor(tab).descending = m_context->value(prefix + QStringLiteral("descending"), true).toBool();
        }
    }
}

void CleanupWindow::saveSortOrder(Tab tab)
{
    if (!m_context) {
        return;
    }
    const QString prefix = QStringLiteral("cleanup/sort/%1/").arg(QLatin1String(sortSettingName(tab)));
    m_context->setValue(prefix + QStringLiteral("column"), sortFor(tab).column);
    m_context->setValue(prefix + QStringLiteral("descending"), sortFor(tab).descending);
}

void CleanupWindow::restoreSize()
{
    QSize size(kWidth, kHeight);
    if (m_context) {
        const int width = m_context->value(QStringLiteral("cleanup/windowWidth")).toInt();
        const int height = m_context->value(QStringLiteral("cleanup/windowHeight")).toInt();
        if (width > 0 && height > 0) {
            size = QSize(width, height);
        }
    }
    // Nunca mas grande que el area util (otra pantalla, u otro tamano de interfaz); nunca mas chica que
    // el minimo, aunque la pantalla lo sea (como antes con el tamano fijo).
    if (const QScreen *screen = this->screen() ? this->screen() : QGuiApplication::primaryScreen()) {
        size = size.boundedTo(screen->availableGeometry().size());
    }
    resize(size.expandedTo(minimumSize()));
    m_sizeRestored = true;
}

// ------------------------------------------------------------------ acciones

void CleanupWindow::showDriveMenu()
{
    if (busy()) {
        return;
    }
    QMenu menu(this);
    for (const DriveInfo &drive : LocalDrives::list()) {
        QString text = drive.label;
        if (!drive.name.isEmpty()) {
            text += QStringLiteral("  ") + DiskSpace::displayName(drive.name);
        }
        text += QStringLiteral("  ·  ") + I18n::tr("%1 free").arg(DiskSpace::formatBytes(drive.freeBytes));
        QAction *action = menu.addAction(text);
        action->setData(drive.root);
        action->setCheckable(true);
        action->setChecked(drive.root == m_root);
    }
    const QPointer<CleanupWindow> self(this);
    QAction *chosen = menu.exec(m_driveButton->mapToGlobal(QPoint(0, m_driveButton->height() + 2)));
    if (self && chosen && chosen->data().toString() != m_root && !busy()) {
        openOn(chosen->data().toString(), static_cast<Tab>(m_tabs->current()));
    }
}

void CleanupWindow::applyBaseline(const ScanSnapshot &baseline)
{
    m_baseline = baseline;
    const bool valid = m_baseline.isValid() && m_snapshot.isValid();
    m_changes = valid ? ScanSnapshot::diff(m_baseline, m_snapshot, kChangeMin) : QList<ScanSnapshot::Change>();
    m_usedDelta = valid ? m_snapshot.usedBytes - m_baseline.usedBytes : 0;
}

void CleanupWindow::showCompareMenu()
{
    if (busy() || m_baselines.isEmpty()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QMenu menu(this);
    for (int i = 0; i < m_baselines.size(); ++i) {
        QAction *action = menu.addAction(I18n::tr("The scan from %1").arg(dayTimeText(m_baselines.at(i).takenAt, now)));
        action->setData(i);
        action->setCheckable(true);
        action->setChecked(m_baselines.at(i).takenAt == m_baseline.takenAt);
    }
    // La barra esta abajo de todo: el menu se abre hacia arriba.
    const QPointer<CleanupWindow> self(this);
    QAction *chosen = menu.exec(m_actionCompare->mapToGlobal(QPoint(0, -menu.sizeHint().height() - 2)));
    if (!self || !chosen || busy()) {
        return;
    }
    const int index = chosen->data().toInt();
    if (index >= 0 && index < m_baselines.size()) {
        applyBaseline(m_baselines.at(index));
        refreshChanges();
        refreshActionBar();
    }
}

void CleanupWindow::toggleFolder(const QString &rowId)
{
    if (!rowId.startsWith(QLatin1Char('d'))) {
        return;
    }
    const ScanTree::Index index = rowId.mid(1).toUInt();
    if (m_expanded.contains(index)) {
        m_expanded.remove(index);
    } else {
        m_expanded.insert(index);
        // Los archivos de esa carpeta se leen ahora, del disco: el arbol solo guarda carpetas.
        if (!m_capture && !m_listings.contains(index)) {
            QString path;
            {
                const auto lock = m_engine.lock();
                path = m_engine.tree().path(index);
            }
            m_listings.insert(index, ScanEngine::listFiles(path, kFilesPerFolder));
        }
    }
    refreshFolders();
}

QList<CleanupWindow::Picked> CleanupWindow::topPickedItems() const
{
    const QList<Picked> items = pickedItems();
    QList<Picked> top;
    for (const Picked &item : items) {
        bool inside = false;
        for (const Picked &other : items) {
            if (other.isDir && other.path != item.path && DeleteGuard::isInside(item.path, other.path)) {
                inside = true;
                break;
            }
        }
        if (!inside) {
            top.append(item);
        }
    }
    return top;
}

void CleanupWindow::flash(const QString &text, bool error)
{
    m_flashError = error;
    m_flash = text;
    m_flashTimer->start();
    refreshActionBar();
}

void CleanupWindow::exportForAi()
{
    if (busy() || m_capture) {
        return;
    }
    CleanupExport::Request request;
    const int tab = m_tabs->current();
    if (tab == CleanUp) {
        if (m_scanState != ScanState::Complete) {
            return;
        }
        request.entries = CleanupExport::entriesForChecked(m_categories);
    } else if (tab == Folders || tab == Files) {
        // Lo mismo que se borraria: una subcarpeta de otra carpeta elegida no se cuenta dos veces.
        for (const Picked &item : topPickedItems()) {
            request.entries.append(CleanupExport::entryForPath(item.path, item.isDir, item.bytes, item.files, item.modified));
        }
    }
    if (request.entries.isEmpty()) {
        return;
    }
    // Lo que tienen adentro las carpetas mas pesadas: es lo que le permite al asistente opinar.
    CleanupExport::detail(request.entries, m_engine);
    request.driveLabel = m_drive.label;
    request.system = QSysInfo::prettyProductName();
    request.freeBytes = m_drive.freeBytes;
    request.totalBytes = m_drive.totalBytes;
    request.when = QDateTime::currentDateTime();

    const QPointer<CleanupWindow> self(this);
    QDialog *dialog = CleanupDialogs::exportForAi(this, int(request.entries.size()), DiskSpace::formatSize(CleanupExport::totalBytes(request)));
    const int choice = dialog->exec();
    delete dialog;
    if (!self || choice == CleanupDialogs::ExportCancel) {
        return;
    }
    const QString text = CleanupExport::markdown(request);
    // En una corrida automatizada no se toca el portapapeles ni se escribe un archivo del usuario.
    if (AutomatedRun::active()) {
        return;
    }
    if (choice == CleanupDialogs::ExportCopy) {
        QGuiApplication::clipboard()->setText(text);
        flash(I18n::tr("Copied. Paste it into your AI assistant."));
        return;
    }
    // Guardar: en la ultima carpeta usada, o en Documentos.
    const QString key = QStringLiteral("cleanup/exportDir");
    QString dir = m_context ? m_context->value(key).toString() : QString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    const QString path = QFileDialog::getSaveFileName(this, I18n::tr("Save the list"), QDir(dir).filePath(CleanupExport::suggestedFileName(request)),
                                                      QStringLiteral("Markdown (*.md)"));
    if (!self || path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    const bool saved = file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) >= 0 && file.commit();
    if (!saved) {
        flash(I18n::tr("The file could not be saved."), true);
        return;
    }
    if (m_context) {
        m_context->setValue(key, QFileInfo(path).absolutePath());
    }
    // Solo el nombre: la ruta entera no entra en la barra, y la carpeta la acaba de elegir el usuario.
    flash(I18n::tr("Saved: %1").arg(QFileInfo(path).fileName()));
}

void CleanupWindow::revealPicked()
{
    const QList<Picked> items = pickedItems();
    if (!items.isEmpty()) {
        SystemPaths::revealInFileManager(items.first().path);
    }
}

void CleanupWindow::reviewPath(const QString &path)
{
    // Abre Folders con esa carpeta a la vista: se despliega cada tramo hasta llegar.
    ScanTree::Index target = ScanTree::kNone;
    {
        const auto lock = m_engine.lock();
        const ScanTree &tree = m_engine.tree();
        target = tree.isEmpty() ? ScanTree::kNone : tree.find(path);
        if (target != ScanTree::kNone && target != 0) {
            for (ScanTree::Index index = tree.node(target).parent; index != ScanTree::kNone && index != 0;
                 index = tree.node(index).parent) {
                m_expanded.insert(index);
            }
        }
    }
    for (const ScanTree::Index index : m_expanded) {
        if (index != 0 && !m_listings.contains(index) && !m_capture) {
            QString dirPath;
            {
                const auto lock = m_engine.lock();
                dirPath = m_engine.tree().isAlive(index) ? m_engine.tree().path(index) : QString();
            }
            if (!dirPath.isEmpty()) {
                m_listings.insert(index, ScanEngine::listFiles(dirPath, kFilesPerFolder));
            }
        }
    }
    m_tabs->setCurrent(Folders);
    setTab(Folders);
    if (target != ScanTree::kNone && target != 0) {
        m_folders->setSelectedIds({QStringLiteral("d%1").arg(target)});
    }
}

void CleanupWindow::deletePicked(bool toTrash)
{
    if (busy() || m_scanState != ScanState::Complete) {
        return;
    }
    // Lo que cuelga de otra carpeta elegida ya viaja con ella.
    const QList<Picked> top = topPickedItems();
    if (top.isEmpty()) {
        return;
    }
    if (!toTrash) {
        QList<QPair<QString, QString>> lines;
        qint64 files = 0;
        bool trashAllowed = true;
        int folders = 0;
        for (const Picked &item : top) {
            files += item.files;
            folders += item.isDir ? 1 : 0;
            if (lines.size() < 6) {
                lines.append({item.path, DiskSpace::formatSize(item.bytes)});
            }
            trashAllowed = trashAllowed && m_guard.check(item.path, DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::Ok;
        }
        const QPointer<CleanupWindow> self(this);
        const QString rootBefore = m_root;
        const CleanupDialogs::DeleteKind kind = folders == top.size() ? CleanupDialogs::DeleteKind::Folders
                                                                      : (folders == 0 ? CleanupDialogs::DeleteKind::Files
                                                                                      : CleanupDialogs::DeleteKind::Items);
        QDialog *dialog = CleanupDialogs::confirmDelete(this, lines, int(top.size() - lines.size()), files, trashAllowed, kind);
        const int choice = dialog->exec();
        delete dialog;
        // (Otro disco: el aviso de disco bajo pudo cambiar la ventana de unidad con el cartel abierto.)
        if (!self || choice == CleanupDialogs::Cancel || busy() || m_root != rootBefore || m_scanState != ScanState::Complete) {
            return;
        }
        toTrash = choice == CleanupDialogs::ToRecycleBin;
    }
    m_requests.clear();
    for (int i = 0; i < top.size(); ++i) {
        CleanupJob::Request request;
        request.id = i;
        request.label = top.at(i).path;
        request.path = top.at(i).path;
        request.action = Cleanup::Action::Entire;
        request.toTrash = toTrash;
        m_requests.append(request);
    }
    m_jobItems = top;
    m_freeBeforeJob = m_drive.freeBytes;
    if (!m_job.start(m_requests, m_guard)) {
        return;
    }
    m_jobKind = JobKind::Manual;
    m_notice.clear();
    for (SizeListView *list : {m_folders, m_files}) {
        list->setInteractive(false);
    }
    m_poll->start();
    refreshHeader();
    refreshActionBar();
}

void CleanupWindow::runCleanup()
{
    if (busy() || m_scanState != ScanState::Complete) {
        return;
    }
    struct Line
    {
        QString name;
        qint64 bytes = 0;
    };
    QList<Line> lines;
    QList<CleanupJob::Request> requests;
    qint64 total = 0;
    for (const Cleanup::Category &category : m_categories) {
        if (category.info) {
            continue;
        }
        for (const Cleanup::Item &item : category.items) {
            if (!item.checked || item.blocked) {
                continue;
            }
            lines.append(Line{category.single ? category.title : item.name, item.bytes});
            total += item.bytes;
            for (const Cleanup::Target &target : item.targets) {
                CleanupJob::Request request;
                request.id = int(requests.size());
                request.label = category.single ? category.title : QStringLiteral("%1 · %2").arg(category.title, item.name);
                request.path = target.path;
                request.action = target.action;
                request.minAgeDays = target.minAgeDays;
                request.blockers = item.blockers;
                request.blockerLabel = item.blockerLabel;
                requests.append(request);
            }
        }
    }
    if (requests.isEmpty()) {
        return;
    }
    std::stable_sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.bytes > b.bytes; });
    QList<QPair<QString, QString>> shown;
    QList<QPair<QString, QString>> rest;
    qint64 moreBytes = 0;
    for (int i = 0; i < lines.size(); ++i) {
        if (i < 3) {
            shown.append({lines.at(i).name, DiskSpace::formatSize(lines.at(i).bytes)});
        } else {
            rest.append({lines.at(i).name, DiskSpace::formatSize(lines.at(i).bytes)});
            moreBytes += lines.at(i).bytes;
        }
    }
    const QPointer<CleanupWindow> self(this);
    const QString rootBefore = m_root;
    QDialog *dialog = CleanupDialogs::confirmCleanup(this, m_drive.label, DiskSpace::formatSize(total), shown, rest, int(rest.size()),
                                                     DiskSpace::formatSize(moreBytes), DiskSpace::formatSize(m_drive.freeBytes),
                                                     DiskSpace::formatSize(m_drive.freeBytes + total));
    const int choice = dialog->exec();
    delete dialog;
    if (!self || choice != QDialog::Accepted || busy() || m_root != rootBefore || m_scanState != ScanState::Complete) {
        return;
    }
    m_requests = requests;
    m_jobItems.clear();
    m_freeBeforeJob = m_drive.freeBytes;
    if (!m_job.start(m_requests, m_guard)) {
        return;
    }
    m_jobKind = JobKind::CleanUp;
    m_banner.clear();
    m_cleanPane->setBanner(QString());
    m_cleanPane->setSkipped(QString(), {});
    m_cleanPane->setInteractive(false);
    m_poll->start();
    refreshHeader();
    refreshActionBar();
}

void CleanupWindow::onJobFinished()
{
    const JobKind kind = m_jobKind;
    m_jobKind = JobKind::None;
    const QList<CleanupJob::Outcome> outcomes = m_job.outcomes();
    reloadDrive();

    QList<ScanTree::Index> rescan;
    qint64 counted = 0;
    qint64 skippedFiles = 0;
    qint64 skippedBytes = 0;
    QList<CleanupPane::SkippedLine> skippedLines;
    int failed = 0;
    int done = 0;
    bool trashed = false;

    for (const CleanupJob::Outcome &outcome : outcomes) {
        const CleanupJob::Request request = m_requests.value(outcome.id);
        counted += outcome.freedBytes;
        if (!outcome.blockedBy.isEmpty()) {
            skippedLines.append({outcome.label, I18n::tr("%1 is open").arg(outcome.blockedBy)});
        } else if (outcome.skippedFiles > 0) {
            skippedFiles += outcome.skippedFiles;
            skippedBytes += outcome.skippedBytes;
            skippedLines.append({outcome.label, outcome.skippedFiles == 1 ? I18n::tr("1 file in use")
                                                                          : I18n::tr("%1 files in use").arg(grouped(outcome.skippedFiles))});
        } else if (outcome.refused != DeleteGuard::Verdict::Ok && outcome.refused != DeleteGuard::Verdict::Missing) {
            skippedLines.append({outcome.label, I18n::tr("Protected, not deleted")});
        } else if (!outcome.error.isEmpty()) {
            skippedLines.append({outcome.label, I18n::tr("Could not be deleted")});
        }
        const bool ok = outcome.refused == DeleteGuard::Verdict::Ok && outcome.error.isEmpty() && outcome.blockedBy.isEmpty();
        if (kind == JobKind::Manual) {
            const Picked item = m_jobItems.value(outcome.id);
            if (ok && (outcome.trashed || outcome.removedEntirely)) {
                ++done;
                trashed = trashed || outcome.trashed;
                if (item.isDir) {
                    m_engine.forgetDir(item.dir);
                } else {
                    m_engine.forgetFile(item.dir, item.name, quint64(item.bytes));
                }
            } else {
                ++failed;
                // Si se borro una parte, esa carpeta se vuelve a leer.
                if (outcome.touched() && item.isDir) {
                    rescan.append(item.dir);
                }
            }
            if (!item.isDir) {
                m_listings.remove(item.dir);
            }
            continue;
        }
        // Clean up: la carpeta que se toco se vuelve a leer del disco (queda con lo que no se pudo borrar).
        if (request.action == Cleanup::Action::RecycleBin || !outcome.touched()) {
            continue;
        }
        ScanTree::Index node = ScanTree::kNone;
        {
            const auto lock = m_engine.lock();
            node = m_engine.tree().find(outcome.path);
        }
        if (node == ScanTree::kNone) {
            continue;
        }
        if (request.action == Cleanup::Action::Entire && outcome.removedEntirely) {
            m_engine.forgetDir(node);
        } else {
            rescan.append(node);
        }
    }

    if (kind == JobKind::Manual) {
        // Los archivos sueltos de las carpetas tocadas se vuelven a listar.
        const QList<ScanTree::Index> open = m_expanded.values();
        for (const ScanTree::Index index : open) {
            if (!m_listings.contains(index)) {
                QString path;
                {
                    const auto lock = m_engine.lock();
                    path = m_engine.tree().isAlive(index) ? m_engine.tree().path(index) : QString();
                }
                if (!path.isEmpty()) {
                    m_listings.insert(index, ScanEngine::listFiles(path, kFilesPerFolder));
                }
            }
        }
        if (!m_listings.contains(0)) {
            m_listings.insert(0, ScanEngine::listFiles(m_scanRoot, kFilesPerFolder));
        }
        if (failed > 0) {
            m_notice = I18n::tr("%1 of %2 could not be deleted: in use, protected or not allowed.").arg(failed).arg(failed + done);
        } else if (trashed) {
#ifdef Q_OS_MACOS
            m_notice = I18n::tr("Moved to the Trash. The space is freed when the Trash is emptied, in Clean up.");
#else
            m_notice = I18n::tr("Moved to the Recycle Bin. The space is freed when the bin is emptied, in Clean up.");
#endif
        } else {
            m_notice = I18n::tr("Deleted. %1 has %2 free.").arg(m_drive.label, DiskSpace::formatSize(m_drive.freeBytes));
        }
        const QList<ScanEngine::TopFile> top = m_engine.topFiles();
        m_fileRows.clear();
        {
            const auto lock = m_engine.lock();
            for (const ScanEngine::TopFile &file : top) {
                m_fileRows.append(FileRow{file, m_engine.tree().path(file.dir)});
            }
        }
        m_folders->clearSelection();
        m_files->clearSelection();
    } else {
        // Lo liberado de verdad es lo que crecio el espacio libre (los enlaces duros hacen que la suma
        // de archivos borrados diga de mas); si otro programa escribio mientras tanto, lo contado.
        const qint64 measured = m_drive.freeBytes - m_freeBeforeJob;
        const qint64 freed = measured > 0 ? measured : counted;
        m_banner = freed > 0 ? I18n::tr("Freed %1. %2 has %3 free.")
                                   .arg(DiskSpace::formatSize(freed), m_drive.label, DiskSpace::formatSize(m_drive.freeBytes))
                             : QString();
        m_cleanPane->setBanner(m_banner);
        // Las listas se rearman con lo que quedo: lo elegido a mano en Folders y Largest files se suelta.
        m_folders->clearSelection();
        m_files->clearSelection();
        const QString summary = skippedFiles > 0 ? I18n::tr("%1 files · %2").arg(grouped(skippedFiles), DiskSpace::formatSize(skippedBytes))
                                                 : (skippedLines.size() == 1 ? I18n::tr("1 item") : I18n::tr("%1 items").arg(skippedLines.size()));
        m_cleanPane->setSkipped(summary, skippedLines);
    }

    // Se vuelve a leer solo lo tocado; con nada para releer, se refresca en el acto.
    if (!rescan.isEmpty() && m_engine.rescan(rescan)) {
        m_fullScan = false;
        m_scanState = ScanState::Scanning;
        m_poll->start();
        refreshHeader();
        refreshFolders();
        refreshActionBar();
        return;
    }
    // Las reglas se miden de nuevo contra el arbol ya corregido.
    remeasure();
    for (SizeListView *list : {m_folders, m_files}) {
        list->setInteractive(true);
    }
    m_cleanPane->setInteractive(true);
    refreshHeader();
    refreshCategories();
    refreshFolders();
    refreshFiles();
    refreshActionBar();
}

// ------------------------------------------------------------------ reglas de carpetas del usuario

QList<Cleanup::FolderRule> CleanupWindow::loadFolderRules() const
{
    QList<Cleanup::FolderRule> rules;
    if (!m_context) {
        return rules;
    }
    const int count = m_context->value(QStringLiteral("cleanup/rules/size"), 0).toInt();
    for (int i = 1; i <= count; ++i) {
        Cleanup::FolderRule rule;
        const QString prefix = QStringLiteral("cleanup/rules/%1/").arg(i);
        rule.base = m_context->value(prefix + QStringLiteral("folder")).toString();
        rule.match = m_context->value(prefix + QStringLiteral("match")).toString();
        if (!rule.base.isEmpty()) {
            rules.append(rule);
        }
    }
    return rules;
}

void CleanupWindow::saveFolderRules(const QList<Cleanup::FolderRule> &rules)
{
    if (!m_context) {
        return;
    }
    // La lista se reescribe entera, sin indices viejos (como los discos vigilados).
    const int before = m_context->value(QStringLiteral("cleanup/rules/size"), 0).toInt();
    for (int i = 1; i <= before; ++i) {
        const QString prefix = QStringLiteral("cleanup/rules/%1/").arg(i);
        m_context->removeValue(prefix + QStringLiteral("folder"));
        m_context->removeValue(prefix + QStringLiteral("match"));
    }
    m_context->setValue(QStringLiteral("cleanup/rules/size"), int(rules.size()));
    for (int i = 0; i < rules.size(); ++i) {
        const QString prefix = QStringLiteral("cleanup/rules/%1/").arg(i + 1);
        m_context->setValue(prefix + QStringLiteral("folder"), rules.at(i).base);
        m_context->setValue(prefix + QStringLiteral("match"), rules.at(i).match);
    }
}

void CleanupWindow::addFolderRule()
{
    if (busy()) {
        return;
    }
    const QPointer<CleanupWindow> self(this);
    FolderRuleDialog dialog(m_scanRoot, this);
    const DeleteGuard guard = m_guard;
    dialog.setValidator([guard](const Cleanup::FolderRule &rule) {
        const QString real = FileSystemOps::canonicalPath(rule.base);
        if (real.isEmpty() || FileSystemOps::kind(real) != FileSystemOps::Kind::Dir || !DeleteGuard::isInside(real, guard.volumeRoot)) {
            return false;
        }
        if (rule.match.isEmpty()) {
            // La carpeta entera: tiene que poder borrarse.
            return guard.check(real, DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::Ok;
        }
        // Carpetas con un nombre adentro de ella: cada una pasa despues por la guarda; aca solo se
        // descarta lo que nunca daria nada (Windows, programas, una carpeta de la nube).
        const DeleteGuard::Verdict verdict = guard.check(real, DeleteGuard::Scope::Children, false);
        return verdict != DeleteGuard::Verdict::ProtectedTree && verdict != DeleteGuard::Verdict::CloudFolder;
    });
    if (dialog.exec() != QDialog::Accepted || !self) {
        return;
    }
    QList<Cleanup::FolderRule> rules = loadFolderRules();
    rules.append(dialog.rule());
    saveFolderRules(rules);
    m_cleanPane->setOpen(QStringLiteral("rule:%1").arg(rules.size() - 1), true);
    if (m_scanState == ScanState::Complete) {
        remeasure();
    }
    refreshCategories();
}

void CleanupWindow::removeFolderRule(const QString &categoryId)
{
    if (busy()) {
        return;
    }
    const int index = categoryId.mid(5).toInt();
    QList<Cleanup::FolderRule> rules = loadFolderRules();
    if (index < 0 || index >= rules.size()) {
        return;
    }
    rules.removeAt(index);
    saveFolderRules(rules);
    // Los ids "rule:N" se corren: lo tildado de las reglas se suelta, que es lo prudente.
    QList<Cleanup::Category> kept;
    for (const Cleanup::Category &category : m_categories) {
        if (!category.id.startsWith(QLatin1String("rule:"))) {
            kept.append(category);
        }
    }
    m_categories = kept;
    if (m_scanState == ScanState::Complete) {
        remeasure();
    }
    refreshCategories();
}

// ------------------------------------------------------------------ ventana

void CleanupWindow::showEvent(QShowEvent *event)
{
    if (!m_shownOnce && !m_capture) {
        // Solo la primera vez: al volver de minimizada se queda como el usuario la dejo.
        m_shownOnce = true;
        // La bandera va ANTES de apply(), como en MainWindow (WM_NCCALCSIZE llega en el acto).
        m_nativeFrameApplied = true;
        m_nativeFrameApplied = WindowFrame::apply(this, true);
        refreshAccessStrip();
        restoreSize();
        if (const QScreen *screen = this->screen() ? this->screen() : QGuiApplication::primaryScreen()) {
            const QRect area = screen->availableGeometry();
            // Centrada, pero nunca con la barra de titulo arriba del borde: con un tamano de interfaz
            // grande (core/UiScale.h) en una pantalla chica la ventana puede no entrar entera.
            const QPoint centered = area.center() - QPoint(width() / 2, height() / 2);
            move(qMax(area.left(), centered.x()), qMax(area.top(), centered.y()));
        }
    }
    QWidget::showEvent(event);
}

void CleanupWindow::closeEvent(QCloseEvent *event)
{
    // Cerrar la ventana la destruye: con ella se van el arbol, el motor y el timer.
    event->accept();
    deleteLater();
}

bool CleanupWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (m_nativeFrameApplied && WindowFrame::handleNativeEvent(message, result)) {
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

// ------------------------------------------------------------------ QA

QStringList CleanupWindow::fixtureStates()
{
    return CleanupFixture::states();
}

bool CleanupWindow::applyFixture(const QString &state)
{
    if (!m_capture || !CleanupFixture::states().contains(state)) {
        return false;
    }
    CleanupFixture::Data data = CleanupFixture::build(state, m_engine);
    m_root = data.drive.root;
    m_scanRoot = data.guard.volumeRoot;
    m_drive = data.drive;
    m_driveKnown = true;
    m_guard = data.guard;
    m_scanState = data.scanning ? ScanState::Scanning : ScanState::Complete;
    m_fullScan = true;
    m_scannedAt = QDateTime(QDate::currentDate(), QTime(12, 41));
    m_fixtureCounting = data.counting;
    m_progress = data.progress;
    m_categories = data.categories;
    m_expanded = data.expanded;
    m_listings = data.listings;
    m_baseline = data.baseline;
    m_baselines = data.baseline.isValid() ? QList<ScanSnapshot>{data.baseline} : QList<ScanSnapshot>();
    m_changes = data.changes;
    m_usedDelta = data.usedDelta;
    m_fileRows.clear();
    {
        const QList<ScanEngine::TopFile> top = m_engine.topFiles();
        const auto lock = m_engine.lock();
        for (const ScanEngine::TopFile &file : top) {
            m_fileRows.append(FileRow{file, m_engine.tree().path(file.dir)});
        }
    }
    for (const QString &id : data.openCategories) {
        m_cleanPane->setOpen(id, true);
    }
    if (!data.openCategories.contains(QStringLiteral("python"))) {
        m_cleanPane->setOpen(QStringLiteral("python"), false);
    }
    m_cleanPane->setBanner(data.banner);
    m_cleanPane->setSkipped(data.skippedSummary, data.skipped);
    m_tabs->setCurrent(data.tab);
    m_stack->setCurrentIndex(data.tab);
    refreshHeader();
    refreshCategories();
    refreshFolders();
    refreshFiles();
    refreshChanges();
    // Lo elegido va despues de armar las filas.
    QStringList picked;
    for (auto it = m_folderRows.constBegin(); it != m_folderRows.constEnd(); ++it) {
        if (data.selectedPaths.contains(it.value().path)) {
            picked.append(it.key());
        }
    }
    m_folders->setSelectedIds(picked);
    refreshActionBar();
    return true;
}
