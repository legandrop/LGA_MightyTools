#include "modules/diskspace/DiskCard.h"
#include "core/I18n.h"

#include "modules/diskspace/DiskState.h"
#include "platform/SystemPaths.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QLabel *label(const QString &text, const char *name, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QLatin1String(name));
    return l;
}

QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("divider"));
    return line;
}

// Linea divisoria con el mismo aire arriba y abajo que en las otras tarjetas.
void addDivider(QVBoxLayout *layout, QWidget *parent)
{
    layout->addSpacing(8);
    layout->addWidget(divider(parent));
    layout->addSpacing(8);
}

constexpr qint64 kGiB = qint64(1024) * 1024 * 1024; // el mismo GB de DiskSpace (thresholdBytes)

double fraction(qint64 part, qint64 total)
{
    return total <= 0 ? 0.0 : qBound(0.0, double(part) / double(total), 1.0);
}

} // namespace

// ------------------------------------------------------------------ UsageBar

UsageBar::UsageBar(QWidget *parent)
    : QWidget(parent)
{
    // 10 px de alto: el riel mide 4 y la marca del umbral sobresale 3 arriba y 3 abajo.
    setFixedHeight(10);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void UsageBar::setDraggable(bool draggable)
{
    m_draggable = draggable;
    refreshInteraction();
}

void UsageBar::refreshInteraction()
{
    // Desenchufado no hay marca que mover: ni cursor de arrastre ni tooltip.
    const bool active = m_draggable && m_connected;
    setAttribute(Qt::WA_TransparentForMouseEvents, !active);
    // Sin seguimiento, Qt no entrega los movimientos sin boton apretado: justo los que delatan un soltar
    // perdido (mouseMoveEvent corta el arrastre con ellos).
    setMouseTracking(active);
    if (active) {
        setCursor(Qt::SizeHorCursor);
        CustomTooltip::instance()->setToolTip(this, I18n::tr("Drag the mark to change the limit"));
    } else {
        unsetCursor();
        CustomTooltip::instance()->setToolTip(this, QString());
    }
}

double UsageBar::fractionAt(double x) const
{
    return width() <= 0 ? 0.0 : qBound(0.0, x / double(width()), 1.0);
}

void UsageBar::mousePressEvent(QMouseEvent *event)
{
    if (!m_draggable || !m_connected || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    emit markDragged(fractionAt(event->position().x()));
    event->accept();
}

void UsageBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    // El soltar se perdio (otra ventana tomo el mouse): el arrastre termina donde estaba.
    if (!(event->buttons() & Qt::LeftButton)) {
        cancelDrag();
        return;
    }
    emit markDragged(fractionAt(event->position().x()));
    event->accept();
}

void UsageBar::cancelDrag()
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    emit dragCanceled();
}

bool UsageBar::event(QEvent *event)
{
    // La ventana se oculta o pierde el frente en medio del arrastre: Windows puede no mandar el soltar. Sin
    // esto la fila quedaba "editando" para siempre y dejaba de mostrar el umbral guardado.
    if (event->type() == QEvent::Hide || event->type() == QEvent::WindowDeactivate) {
        cancelDrag();
    }
    return QWidget::event(event);
}

void UsageBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    emit markReleased(fractionAt(event->position().x()));
    event->accept();
}

QSize UsageBar::sizeHint() const
{
    return QSize(200, 10);
}

void UsageBar::set(bool connected, double used, double mark, bool low)
{
    if (connected != m_connected) {
        m_connected = connected;
        refreshInteraction();
    }
    m_used = used;
    m_mark = mark;
    m_low = low;
    update();
}

void UsageBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    const QRectF track(0, 3, width(), 4);
    painter.setOpacity(m_connected ? 1.0 : 0.4);
    painter.setBrush(Theme::color(Theme::kTile));
    painter.drawRoundedRect(track, 2, 2);
    if (!m_connected) {
        return;
    }
    if (m_used > 0.0) {
        QRectF fill = track;
        fill.setWidth(qMax(4.0, track.width() * m_used));
        painter.setBrush(Theme::color(m_low ? Theme::kWarn : Theme::kBarFill));
        painter.drawRoundedRect(fill, 2, 2);
    }
    // Sin marca (un disco que no se vigila, en la ventana de limpieza).
    if (m_mark < 0.0) {
        return;
    }
    // Marca del umbral: 2 x 10, dentro del ancho aunque el umbral caiga en un extremo.
    const double x = qBound(1.0, track.width() * m_mark, track.width() - 1.0);
    painter.setBrush(Theme::color(m_low ? Theme::kWarnMark : Theme::kTextMuted));
    painter.drawRoundedRect(QRectF(x - 1.0, 0, 2, 10), 1, 1);
}

// ------------------------------------------------------------------ DriveRow

DriveRow::DriveRow(const QString &root, QWidget *parent)
    : QWidget(parent)
    , m_root(root)
{
    setObjectName(QStringLiteral("driveRow"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(5);

    auto *top = new QHBoxLayout();
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(8);
    m_keycap = new Chip(this);
    top->addWidget(m_keycap, 0, Qt::AlignVCenter);
    // El nombre ocupa lo que mide (y se recorta con "..." si no entra): "Not connected" va pegado a
    // el, y el aire sobrante queda antes de los controles.
    m_name = new ElidedLabel(this);
    m_name->setObjectName(QStringLiteral("driveName"));
    m_name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    top->addWidget(m_name, 0, Qt::AlignVCenter);
    m_missing = label(I18n::tr("Not connected"), "meta", this);
    top->addWidget(m_missing, 0, Qt::AlignVCenter);
    top->addStretch(1);
    // Fila de 26 de alto como la del canvas (`.row` con min-height 26).
    auto *topStrut = new QWidget(this);
    topStrut->setFixedSize(0, 26);
    top->addWidget(topStrut);

    auto *controls = new QHBoxLayout();
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(6);
    // El unico campo de texto de la app junto con el grabador de atajos: un numero se escribe.
    // keyboardTracking false: el valor se guarda al apretar Enter o al salir del campo (o con la
    // rueda), no con cada tecla.
    m_value = new QSpinBox(this);
    m_value->setObjectName(QStringLiteral("threshold"));
    m_value->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_value->setKeyboardTracking(false);
    m_value->setFixedSize(52, 24);
    CustomTooltip::instance()->setToolTip(m_value, I18n::tr("Warn when free space is under this"));
    controls->addWidget(m_value, 0, Qt::AlignVCenter);

    auto *segment = new QHBoxLayout();
    segment->setContentsMargins(0, 0, 0, 0);
    segment->setSpacing(0);
    m_gb = Ui::button(QStringLiteral("GB"), QString(), QString(), this);
    m_percent = Ui::button(QStringLiteral("%"), QString(), QString(), this);
    for (QPushButton *b : {m_gb, m_percent}) {
        b->setObjectName(QStringLiteral("segButton"));
        b->setCheckable(true);
        segment->addWidget(b, 0, Qt::AlignVCenter);
    }
    m_gb->setProperty("pos", QStringLiteral("left"));
    m_percent->setProperty("pos", QStringLiteral("right"));
    CustomTooltip::instance()->setToolTip(m_gb, I18n::tr("Warn under an amount of free space"));
    CustomTooltip::instance()->setToolTip(m_percent, I18n::tr("Warn under a share of the drive"));
    controls->addLayout(segment);

    // "Que ocupa este disco": abre la ventana de limpieza en la lista de carpetas. Solo donde la
    // limpieza esta habilitada (hoy, Windows).
    m_explore = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    m_explore->setObjectName(QStringLiteral("exploreDrive"));
    Ui::setIcon(m_explore, Icon::List, Theme::color(Theme::kIcon), 13);
    CustomTooltip::instance()->setToolTip(m_explore, I18n::tr("See what is using this drive"));
    m_explore->setAccessibleName(I18n::tr("See what is using this drive"));
    m_explore->setVisible(SystemPaths::cleanupSupported());
    controls->addWidget(m_explore, 0, Qt::AlignVCenter);

    m_remove = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    m_remove->setObjectName(QStringLiteral("removeDrive"));
    Ui::setIcon(m_remove, Icon::X, Theme::color(Theme::kIcon), 12);
    CustomTooltip::instance()->setToolTip(m_remove, I18n::tr("Stop watching"));
    m_remove->setAccessibleName(I18n::tr("Stop watching"));
    controls->addWidget(m_remove, 0, Qt::AlignVCenter);
    top->addLayout(controls);
    column->addLayout(top);

    m_bar = new UsageBar(this);
    column->addWidget(m_bar);
    connect(m_bar, &UsageBar::markDragged, this, &DriveRow::onMarkDragged);
    connect(m_bar, &UsageBar::markReleased, this, &DriveRow::onMarkReleased);
    connect(m_bar, &UsageBar::dragCanceled, this, &DriveRow::showSavedValue);

    auto *foot = new QHBoxLayout();
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(8);
    m_free = label(QString(), "caption", this);
    m_free->setTextFormat(Qt::RichText);
    foot->addWidget(m_free, 1);
    m_threshold = label(QString(), "meta", this);
    foot->addWidget(m_threshold, 0);
    // Con el disco bajo, en el lugar de "warn under": el camino corto a liberar espacio.
    m_cleanup = Ui::button(I18n::tr("Free up space"), QString(), QString(), this);
    m_cleanup->setObjectName(QStringLiteral("linkButton"));
    m_cleanup->hide();
    foot->addWidget(m_cleanup, 0, Qt::AlignVCenter);
    // La linea de abajo es una `.row` de 22 de alto en el canvas.
    auto *footStrut = new QWidget(this);
    footStrut->setFixedSize(0, 22);
    foot->addWidget(footStrut);
    column->addLayout(foot);

    connect(m_value, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { emit thresholdChanged(m_root, value, m_watch.unit); });
    connect(m_gb, &QPushButton::clicked, this, [this]() { onUnitClicked(DiskWatch::Unit::GB); });
    connect(m_percent, &QPushButton::clicked, this, [this]() { onUnitClicked(DiskWatch::Unit::Percent); });
    connect(m_remove, &QPushButton::clicked, this, [this]() { emit removeRequested(m_root); });
    connect(m_explore, &QPushButton::clicked, this, [this]() { emit exploreRequested(m_root); });
    connect(m_cleanup, &QPushButton::clicked, this, [this]() { emit cleanupRequested(m_root); });

    // Enter confirma el numero (keyboardTracking false: recien ahi se guarda) y suelta el campo, como
    // un "aceptar": sin esto el cursor queda titilando adentro. Salir del campo por cualquier otro
    // camino tambien emite editingFinished y confirma.
    connect(m_value, &QAbstractSpinBox::editingFinished, this, [this]() { m_value->clearFocus(); });
    // Escape se escucha en el spin box y en su campo interno: segun la version de Qt, la tecla le
    // llega a uno o al otro.
    m_value->installEventFilter(this);
    if (QLineEdit *edit = m_value->findChild<QLineEdit *>()) {
        edit->installEventFilter(this);
        // Solo por click, nunca por Tab: al activarse una ventana sin foco, Qt se lo da al primer
        // control que acepta Tab, y en esta app el unico seria este campo. Con StrongFocus, abrir
        // Settings dejaba el cursor titilando en el umbral del primer disco.
        edit->setFocusPolicy(Qt::ClickFocus);
    }
}

bool DriveRow::eventFilter(QObject *watched, QEvent *event)
{
    // Escape: vuelve al umbral guardado y suelta el campo sin guardar lo escrito.
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_value->blockSignals(true);
        m_value->setValue(m_watch.value);
        m_value->blockSignals(false);
        m_value->clearFocus();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

bool DriveRow::isEditing() const
{
    return m_bar->isDragging() || m_value->hasFocus()
           || (m_value->findChild<QLineEdit *>() && m_value->findChild<QLineEdit *>()->hasFocus());
}

void DriveRow::setInteractive(bool interactive)
{
    m_bar->setDraggable(interactive);
}

int DriveRow::valueAtMark(double fraction) const
{
    // La marca esta donde lo usado llega al umbral: a su derecha queda el espacio libre del umbral.
    const double freeShare = 1.0 - fraction;
    if (m_watch.unit == DiskWatch::Unit::Percent) {
        return qBound(1, qRound(freeShare * 100.0), DiskSpace::kMaxPercent);
    }
    const int totalGb = int(qMin<qint64>(m_drive.totalBytes / kGiB, DiskSpace::kMaxGb));
    return qBound(1, qRound(freeShare * double(m_drive.totalBytes) / double(kGiB)), qMax(1, totalGb));
}

void DriveRow::onMarkDragged(double fraction)
{
    if (!m_connected) {
        return;
    }
    // Mientras se arrastra solo cambia lo que se ve: se guarda una vez, al soltar.
    m_dragValue = valueAtMark(fraction);
    m_value->blockSignals(true);
    m_value->setValue(m_dragValue);
    m_value->blockSignals(false);
    render(m_dragValue);
}

void DriveRow::showSavedValue()
{
    m_value->blockSignals(true);
    m_value->setValue(m_watch.value);
    m_value->blockSignals(false);
    render(m_watch.value);
}

void DriveRow::onMarkReleased(double fraction)
{
    // El disco se desenchufo en medio del arrastre: no se guarda nada y se vuelve a lo guardado.
    if (!m_connected) {
        showSavedValue();
        return;
    }
    const int value = valueAtMark(fraction);
    if (value != m_watch.value) {
        emit thresholdChanged(m_root, value, m_watch.unit);
    } else {
        showSavedValue();
    }
}

void DriveRow::onUnitClicked(DiskWatch::Unit unit)
{
    if (unit == m_watch.unit) {
        // Un click en la unidad que ya estaba no la destilda.
        m_gb->setChecked(unit == DiskWatch::Unit::GB);
        m_percent->setChecked(unit == DiskWatch::Unit::Percent);
        return;
    }
    // Cambiar de unidad arranca de un valor razonable: 50 GB no significan 50 %.
    emit thresholdChanged(m_root, unit == DiskWatch::Unit::GB ? DiskSpace::kDefaultGb : DiskSpace::kDefaultPercent, unit);
}

void DriveRow::update(const DiskWatch &watch, const DriveInfo *drive)
{
    m_watch = watch;
    const bool connected = drive != nullptr;
    m_connected = connected;
    m_drive = connected ? *drive : DriveInfo();

    // Desenchufado: el keycap punteado y gris (`.kc.dim` del canvas).
    m_keycap->set(connected ? QStringLiteral("key") : QStringLiteral("keyDim"),
                  connected ? drive->label : DiskSpace::labelForRoot(watch.root, watch.name));
    const QString name = connected ? drive->name : watch.name;
    m_name->setText(DiskSpace::displayName(name));
    Ui::setStyleProperty(m_name, "dim", !connected);
    m_missing->setVisible(!connected);

    if (!isEditing()) {
        m_value->blockSignals(true);
        m_value->setRange(1, watch.unit == DiskWatch::Unit::Percent ? DiskSpace::kMaxPercent : DiskSpace::kMaxGb);
        m_value->setValue(watch.value);
        m_value->blockSignals(false);
    }
    m_gb->setChecked(watch.unit == DiskWatch::Unit::GB);
    m_percent->setChecked(watch.unit == DiskWatch::Unit::Percent);
    // Desenchufado en medio del arrastre: la barra deja de tomar el mouse y el soltar puede no llegar, asi
    // que el arrastre se corta aca (vuelve a lo guardado). Con la marca agarrada y el disco enchufado, una
    // lectura nueva no la devuelve al valor guardado.
    if (!connected && m_bar->isDragging()) {
        m_bar->cancelDrag();
    }
    render(m_bar->isDragging() ? m_dragValue : watch.value);
}

void DriveRow::render(int value)
{
    DiskWatch watch = m_watch;
    watch.value = value;
    const bool connected = m_connected;
    const DriveInfo *drive = connected ? &m_drive : nullptr;
    const bool low = connected && DiskSpace::isLow(watch, *drive);

    m_explore->setVisible(connected && SystemPaths::cleanupSupported());
    if (!connected) {
        m_bar->set(false, 0, 0, false);
        m_free->setText(I18n::tr("Skipped until it is plugged in."));
        Ui::setStyleProperty(m_free, "tone", QString());
        m_threshold->setText(I18n::tr("warn under %1").arg(DiskSpace::thresholdText(watch)));
        m_threshold->setVisible(true);
        m_cleanup->setVisible(false);
        return;
    }
    const qint64 threshold = DiskSpace::thresholdBytes(watch, drive->totalBytes);
    m_bar->set(true, fraction(drive->totalBytes - drive->freeBytes, drive->totalBytes),
               fraction(drive->totalBytes - threshold, drive->totalBytes), low);
    // Dos claves completas (no una frase pegada): el espanol puede mover el umbral. Lo libre va en
    // negrita (pedido de Lega): la marca esta en la clave, asi cada idioma resalta su "libres". Peso 600
    // y no <b>: la app trae Inter SemiBold y no Bold, y un 700 sale sintetizado.
    const QString freeBytes = DiskSpace::formatBytes(drive->freeBytes);
    const QString totalBytes = DiskSpace::formatBytes(drive->totalBytes);
    const QString freeText = low ? I18n::tr("<span style=\"font-weight:600\">%1 free</span> of %2 · under %3")
                                       .arg(freeBytes, totalBytes, DiskSpace::thresholdText(watch))
                                 : I18n::tr("<span style=\"font-weight:600\">%1 free</span> of %2").arg(freeBytes, totalBytes);
    m_free->setText(freeText);
    Ui::setStyleProperty(m_free, "tone", low ? QStringLiteral("warn") : QString());
    m_threshold->setText(I18n::tr("warn under %1").arg(DiskSpace::thresholdText(watch)));
    m_threshold->setVisible(!low);
    m_cleanup->setVisible(low && SystemPaths::cleanupSupported());
}

// ------------------------------------------------------------------ DiskCard

DiskCard::DiskCard(DiskState *state, bool interactive, QWidget *parent)
    : QFrame(parent)
    , m_state(state)
    , m_interactive(interactive)
{
    setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(0);

    auto *head = new QHBoxLayout();
    head->setSpacing(6);
    // `.head` del canvas: el titulo ocupa 22 de alto.
    QLabel *title = label(I18n::tr("Watched drives"), "cardTitle", this);
    title->setMinimumHeight(22);
    head->addWidget(title, 1);
    m_chip = new Chip(this);
    head->addWidget(m_chip, 0, Qt::AlignVCenter);
    layout->addLayout(head);
    layout->addSpacing(8);

    // Sin discos: una linea que explica para que sirve, y "Add drive...".
    m_emptyCaption = label(I18n::tr("Get a notification when a drive runs low. Nothing is watched yet."), "caption", this);
    m_emptyCaption->setWordWrap(true);
    m_emptyCaption->setContentsMargins(0, 0, 0, 8);
    layout->addWidget(m_emptyCaption);

    // Con discos: cada cuanto se repite el aviso arriba, una fila por disco, y "Add drive..." abajo.
    m_listBlock = new QWidget(this);
    auto *list = new QVBoxLayout(m_listBlock);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(0);
    m_remindRow = new QWidget(m_listBlock);
    m_remindRow->setMinimumHeight(26); // `.row` de 26 del canvas
    auto *remind = new QHBoxLayout(m_remindRow);
    remind->setContentsMargins(0, 0, 0, 0);
    remind->setSpacing(8);
    remind->addWidget(label(I18n::tr("Remind me every"), "optionLabel", m_remindRow), 0, Qt::AlignVCenter);
    m_remindButton = Ui::button(QString(), QString(), QString(), m_remindRow);
    m_remindButton->setObjectName(QStringLiteral("fieldButton"));
    Ui::setDropdownArrow(m_remindButton);
    CustomTooltip::instance()->setToolTip(m_remindButton, I18n::tr("While a drive stays low. Drives are checked every %1 min.")
                                                              .arg(DiskSpace::kCheckMinutes));
    remind->addWidget(m_remindButton, 0, Qt::AlignVCenter);
    remind->addStretch(1);
    m_lastCheck = label(QString(), "meta", m_remindRow);
    remind->addWidget(m_lastCheck, 0, Qt::AlignVCenter);
    list->addWidget(m_remindRow);
    addDivider(list, m_listBlock);
    auto *rows = new QWidget(m_listBlock);
    m_rowsLayout = new QVBoxLayout(rows);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(0);
    list->addWidget(rows);
    addDivider(list, m_listBlock);
    layout->addWidget(m_listBlock);

    m_addButton = Ui::button(I18n::tr("Add drive..."), QString(), QString(), this);
    m_addButton->setObjectName(QStringLiteral("linkButton"));
    Ui::setIcon(m_addButton, Icon::Plus, Theme::color(Theme::kLink), 10);
    auto *addRow = new QHBoxLayout();
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->addWidget(m_addButton, 0, Qt::AlignLeft | Qt::AlignVCenter);
    addRow->addStretch(1);
    layout->addLayout(addRow);

    if (m_interactive) {
        // Los demas controles no toman foco (Theme), asi que un click afuera no se lo saca al campo
        // del umbral: lo hace este filtro. Solo mira clicks.
        qApp->installEventFilter(this);
        connect(m_addButton, &QPushButton::clicked, this, &DiskCard::showAddMenu);
        connect(m_remindButton, &QPushButton::clicked, this, &DiskCard::showRemindMenu);
    }
    refresh();
}

bool DiskCard::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget *focused = QApplication::focusWidget();
        auto *target = qobject_cast<QWidget *>(watched);
        // Un click fuera del campo que tiene el teclado (en la tarjeta, en otra tarjeta o en la barra
        // de titulo) lo confirma y lo suelta. Un click adentro del mismo campo lo deja escribiendo.
        // El foco puede quedar en el QSpinBox o en su QLineEdit interno: el campo es el spin box.
        QWidget *field = focused;
        if (focused && qobject_cast<QLineEdit *>(focused) && qobject_cast<QAbstractSpinBox *>(focused->parentWidget())) {
            field = focused->parentWidget();
        }
        if (field && target && isAncestorOf(field) && qobject_cast<QAbstractSpinBox *>(field) && target != field
            && !field->isAncestorOf(target)) {
            field->clearFocus();
        }
    }
    return QFrame::eventFilter(watched, event);
}

void DiskCard::rebuildRows(const QList<DiskWatch> &watches)
{
    QStringList roots;
    for (const DiskWatch &watch : watches) {
        roots.append(watch.root);
    }
    // Las filas se rearman SOLO si cambio la lista de discos: si no, un refresco (cada chequeo, cada
    // cambio de umbral) le sacaria el teclado al campo que el usuario esta escribiendo.
    if (roots == m_rowRoots) {
        return;
    }
    m_rowRoots = roots;
    // deleteLater y no delete: el rearmado puede venir del click en la X de una de estas filas
    // (removeDiskWatch -> changed -> refresh), y borrarla en el acto la destruiria dentro de su
    // propia senal.
    while (QLayoutItem *item = m_rowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_rows.clear();
    // Filas y divisores nacen hijos del contenedor y se muestran en el acto: con la ventana abierta,
    // un hijo nuevo se muestra recien en el proximo ciclo de eventos, y hasta entonces el layout no
    // lo cuenta al medir el alto.
    QWidget *container = m_rowsLayout->parentWidget();
    for (int i = 0; i < watches.size(); ++i) {
        if (i > 0) {
            m_rowsLayout->addSpacing(8);
            QFrame *line = divider(container);
            m_rowsLayout->addWidget(line);
            line->show();
            m_rowsLayout->addSpacing(8);
        }
        auto *row = new DriveRow(watches.at(i).root, container);
        row->setInteractive(m_interactive);
        m_rowsLayout->addWidget(row);
        row->show();
        m_rows.insert(row->root(), row);
        if (m_interactive) {
            connect(row, &DriveRow::thresholdChanged, m_state, &DiskState::setDiskThreshold);
            connect(row, &DriveRow::removeRequested, m_state, &DiskState::removeDiskWatch);
            connect(row, &DriveRow::exploreRequested, this, &DiskCard::exploreRequested);
            connect(row, &DriveRow::cleanupRequested, this, &DiskCard::cleanupRequested);
        }
    }
    // El layout de la tarjeta guarda el alto del contenedor de filas de cuando estaba vacio, y el
    // aviso de que cambio llega recien con el proximo ciclo de eventos. el panel mide su alto en el
    // mismo refresco: sin esto, las filas nuevas quedan aplastadas.
    m_rowsLayout->invalidate();
    m_rowsLayout->parentWidget()->updateGeometry();
}

void DiskCard::refresh()
{
    const QList<DiskWatch> watches = m_state->diskWatches();
    const bool empty = watches.isEmpty();
    m_emptyCaption->setVisible(empty);
    m_listBlock->setVisible(!empty);

    rebuildRows(watches);
    for (const DiskWatch &watch : watches) {
        DriveInfo drive;
        const bool connected = m_state->driveReading(watch.root, &drive);
        m_rows.value(watch.root)->update(watch, connected ? &drive : nullptr);
    }

    m_remindButton->setText(DiskSpace::intervalText(m_state->remindMinutes()));
    const QDateTime last = m_state->lastDiskCheck();
    m_lastCheck->setText(last.isValid() ? I18n::tr("Last check %1").arg(last.toString(QStringLiteral("HH:mm")))
                                        : QString());

    // Chip y borde de la tarjeta: ambar si algun disco enchufado esta bajo su umbral.
    const QList<DiskWatch> low = m_state->lowWatches();
    QString tone;
    if (empty) {
        m_chip->set(QStringLiteral("src"), I18n::trc("chip", "Off"));
    } else if (low.isEmpty()) {
        m_chip->set(QStringLiteral("ok"), I18n::tr("All good"));
    } else {
        tone = QStringLiteral("warn");
        if (low.size() == 1) {
            DriveInfo drive;
            m_state->driveReading(low.first().root, &drive);
            m_chip->set(tone, I18n::tr("%1 is low").arg(drive.label));
        } else {
            m_chip->set(tone, I18n::tr("%1 drives are low").arg(low.size()));
        }
    }
    Ui::setStyleProperty(this, "tone", tone);
}

void DiskCard::fillAddMenu(QMenu *menu) const
{
    auto *header = menu->addAction(I18n::tr("Local drives"));
    header->setEnabled(false);
    int added = 0;
    for (const DriveInfo &drive : m_state->drives()) {
        if (m_state->isWatched(drive.root)) {
            continue;
        }
        QString text = drive.label;
        if (!drive.name.isEmpty()) {
            text += QStringLiteral("  ") + DiskSpace::displayName(drive.name);
        }
        text += QStringLiteral("  ·  ") + I18n::tr("%1 free").arg(DiskSpace::formatBytes(drive.freeBytes));
        QAction *action = menu->addAction(text);
        action->setData(drive.root);
        action->setProperty("driveName", drive.name.isEmpty() ? drive.label : drive.name);
        ++added;
    }
    if (added == 0) {
        header->setText(I18n::tr("Every local drive is already watched"));
    }
}

void DiskCard::showAddMenu()
{
    emit drivesRefreshRequested();
    QMenu menu(this);
    fillAddMenu(&menu);
    QAction *chosen = menu.exec(m_addButton->mapToGlobal(QPoint(0, m_addButton->height() + 2)));
    if (chosen && chosen->data().isValid()) {
        m_state->addDiskWatch(chosen->data().toString(), chosen->property("driveName").toString());
    }
}

void DiskCard::fillRemindMenu(QMenu *menu) const
{
    for (const int minutes : DiskSpace::remindChoices()) {
        QAction *action = menu->addAction(DiskSpace::intervalText(minutes));
        action->setData(minutes);
        action->setCheckable(true);
        action->setChecked(minutes == m_state->remindMinutes());
    }
}

void DiskCard::showRemindMenu()
{
    QMenu menu(this);
    fillRemindMenu(&menu);
    QAction *chosen = menu.exec(m_remindButton->mapToGlobal(QPoint(0, m_remindButton->height() + 2)));
    if (chosen) {
        m_state->setRemindMinutes(chosen->data().toInt());
    }
}
