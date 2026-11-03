#include "modules/desktopicons/DesktopIconsPanel.h"

#include "core/I18n.h"
#include "modules/desktopicons/DesktopIconsModule.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Miniatura del escritorio del snapshot: la pantalla y un punto por icono. Misma caja que la miniatura
// de Window Layouts (47 x 31, borde fuerte, fondo de ventana, radio 3).
class MiniDesktop : public QWidget
{
public:
    MiniDesktop(const DesktopIconView::Layout &layout, QWidget *parent) : QWidget(parent), m_layout(layout)
    {
        setFixedSize(47, 31);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(Theme::color(Theme::kBorderStrong));
        painter.setBrush(Theme::color(Theme::kWindow));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
        if (m_layout.screen.isEmpty()) return;
        const QRectF inner = QRectF(rect()).adjusted(4, 4, -5, -5);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::color(Theme::kLink));
        painter.setClipRect(inner.adjusted(0, 0, 1, 1));
        for (const DesktopIconView::Icon &icon : m_layout.icons) {
            const QPointF at(inner.left() + inner.width() * icon.position.x() / m_layout.screen.width(),
                             inner.top() + inner.height() * icon.position.y() / m_layout.screen.height());
            painter.drawRect(QRectF(at, QSizeF(1.6, 1.6)));
        }
    }

private:
    DesktopIconView::Layout m_layout;
};

using DesktopSnapshots::screenText;

QString iconsText(int count)
{
    return count == 1 ? I18n::tr("1 icon") : I18n::tr("%1 icons").arg(count);
}

QString kindText(SnapshotKind kind)
{
    switch (kind) {
    case SnapshotKind::Automatic: return I18n::trc("snapshot", "Daily");
    case SnapshotKind::BeforeRestore: return I18n::trc("snapshot", "Before restore");
    case SnapshotKind::Manual: break;
    }
    return I18n::trc("snapshot", "Manual");
}

} // namespace

DesktopIconsPanel::DesktopIconsPanel(DesktopIconsModule *module, bool interactive, QWidget *parent)
    : QWidget(parent), m_module(module), m_interactive(interactive)
{
    setObjectName(QStringLiteral("desktopIconsPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    // El automatico: una opcion de toda la herramienta, primero (como el de Window Layouts).
    auto *dailyCard = Ui::card(this);
    auto *dailyColumn = new QVBoxLayout(dailyCard);
    dailyColumn->setContentsMargins(14, 12, 14, 12);
    dailyColumn->setSpacing(8); // solo cuenta con el aviso a la vista: un widget oculto no suma espacio
    m_daily = new SwitchRow(I18n::tr("Daily snapshot"),
                            I18n::tr("Once a day, after the computer has been awake for an hour. The last 5 are kept."),
                            dailyCard);
    m_daily->setObjectName(QStringLiteral("dailySnapshotRow"));
    dailyColumn->addWidget(m_daily);
    // Hoy no hubo automatico por la resolucion: que se vea (si no, el diario se apagaria en silencio).
    // Sin objectName propio: el color del tono sale de QLabel#caption.
    m_skipNotice = Ui::caption(QString(), dailyCard);
    Ui::setStyleProperty(m_skipNotice, "tone", QStringLiteral("warn"));
    m_skipNotice->setWordWrap(true);
    m_skipNotice->hide();
    dailyColumn->addWidget(m_skipNotice);
    column->addWidget(dailyCard);

    // Resultado de la ultima restauracion (o el error de la ultima accion), con "Undo".
    m_result = new StatusCard(this);
    m_result->hide();
    column->addWidget(m_result);

    auto *saveCard = Ui::card(this);
    auto *saveColumn = new QVBoxLayout(saveCard);
    saveColumn->setContentsMargins(14, 12, 14, 12);
    saveColumn->setSpacing(8);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(2);
    texts->addWidget(Ui::label(I18n::tr("Save the current arrangement"), "cardTitle", saveCard));
    auto *saveCaption = Ui::caption(I18n::tr("Snapshots you save stay until you delete them."), saveCard);
    saveCaption->setWordWrap(true);
    texts->addWidget(saveCaption);
    saveColumn->addLayout(texts);
    auto *saveRow = new QHBoxLayout();
    saveRow->setSpacing(8);
    m_name = new QLineEdit(saveCard);
    m_name->setObjectName(QStringLiteral("ruleField"));
    m_name->setFocusPolicy(Qt::ClickFocus);
    m_name->setPlaceholderText(I18n::tr("Name (optional)"));
    m_name->setAccessibleName(I18n::tr("Snapshot name"));
    m_name->setMaxLength(120);
    m_name->installEventFilter(this);
    saveRow->addWidget(m_name, 1);
    auto *save = Ui::button(I18n::tr("Save snapshot"), QStringLiteral("primary"), QStringLiteral("sm"), saveCard);
    save->setObjectName(QStringLiteral("saveSnapshot"));
    save->setFocusPolicy(Qt::NoFocus);
    saveRow->addWidget(save);
    saveColumn->addLayout(saveRow);
    // El error de guardar va aca, al lado de lo que se apreto: la tarjeta de arriba (con su Undo) no se toca.
    m_saveError = Ui::caption(QString(), saveCard);
    Ui::setStyleProperty(m_saveError, "tone", QStringLiteral("err"));
    m_saveError->setWordWrap(true);
    m_saveError->hide();
    saveColumn->addWidget(m_saveError);
    column->addWidget(saveCard);

    auto *listCard = Ui::card(this);
    auto *listColumn = new QVBoxLayout(listCard);
    listColumn->setContentsMargins(14, 12, 14, 12);
    listColumn->setSpacing(8);
    listColumn->addWidget(Ui::label(I18n::tr("Snapshots"), "cardTitle", listCard));
    m_rows = new QWidget(listCard);
    auto *rows = new QVBoxLayout(m_rows);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    listColumn->addWidget(m_rows);
    auto *hint = Ui::caption(I18n::tr("Icons are matched by their file, not their name. The ones that no longer exist are skipped."),
                             listCard);
    hint->setWordWrap(true);
    m_listHint = hint;
    listColumn->addWidget(hint);
    column->addWidget(listCard);
    column->addStretch();

    if (m_interactive) {
        connect(m_daily, &SwitchRow::clicked, this, [this](bool checked) { m_module->setDailyEnabled(checked); });
        connect(save, &QPushButton::clicked, this, [this]() { runLater([this]() { saveSnapshot(); }); });
        connect(m_name, &QLineEdit::returnPressed, this, [this]() { runLater([this]() { saveSnapshot(); }); });
        connect(m_result->button(), &QPushButton::clicked, this, [this]() {
            const QString undo = m_module->lastResult().undoId;
            runLater([this, undo]() {
                QGuiApplication::setOverrideCursor(Qt::WaitCursor);
                m_module->restore(undo);
                QGuiApplication::restoreOverrideCursor();
                scrollToTop();
            });
        });
    }
    refresh();
}

void DesktopIconsPanel::refresh()
{
    m_daily->setChecked(m_module->dailyEnabled());
    const QString notice = m_module->skipNotice();
    m_skipNotice->setText(notice);
    m_skipNotice->setVisible(!notice.isEmpty());
    const QString saveError = m_module->saveError();
    m_saveError->setText(saveError.isEmpty() ? QString() : I18n::tr("The snapshot was not saved.") + QLatin1Char(' ') + saveError);
    m_saveError->setVisible(!saveError.isEmpty());
    const DesktopIconsModule::Result &result = m_module->lastResult();
    m_result->setVisible(result.visible);
    if (result.visible) {
        const QString undo = result.undoId.isEmpty() ? QString() : I18n::tr("Undo");
        if (!result.ok) {
            m_result->set(QStringLiteral("error"), I18n::tr("The desktop icons were not changed"), result.error, undo, QString(),
                          QStringLiteral("err"), QStringLiteral("sm"));
        } else {
            // "Restored 80 icons · 26 not found · 3 not placed": solo cuenta lo que quedo exacto, releido.
            QString title = result.restored == 1 ? I18n::tr("Restored 1 icon") : I18n::tr("Restored %1 icons").arg(result.restored);
            QStringList details;
            const int missing = int(result.missing.size());
            if (missing > 0) {
                title += QStringLiteral(" · ") + I18n::tr("%1 not found").arg(missing);
                const QStringList shown = result.missing.mid(0, 3);
                const QString names = missing > shown.size()
                    ? I18n::tr("%1 and %2 more").arg(shown.join(QStringLiteral(", "))).arg(missing - shown.size())
                    : shown.join(QStringLiteral(", "));
                details << I18n::tr("Skipped, no longer on the desktop: %1.").arg(names);
            }
            if (result.notPlaced > 0) {
                title += QStringLiteral(" · ") + I18n::tr("%1 not placed").arg(result.notPlaced);
                details << I18n::tr("Windows left %1 in another spot, for example with Auto arrange icons on.")
                               .arg(iconsText(result.notPlaced));
            }
            if (details.isEmpty()) details << I18n::tr("Undo puts them back where they were before restoring.");
            m_result->set(missing > 0 || result.notPlaced > 0 ? QStringLiteral("warn") : QStringLiteral("on"), title,
                          details.join(QLatin1Char(' ')), undo, QString(), QString(), QStringLiteral("sm"));
        }
    }
    rebuildList();
}

void DesktopIconsPanel::rebuildList()
{
    auto *column = static_cast<QVBoxLayout *>(m_rows->layout());
    while (QLayoutItem *item = column->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_deleteSlots.clear();
    const QList<DesktopSnapshot> list = m_module->snapshots();
    m_firstId = list.isEmpty() ? QString() : list.first().id;
    m_listHint->setVisible(!list.isEmpty());
    if (list.isEmpty()) {
        auto *empty = Ui::caption(m_module->dailyEnabled()
                                      ? I18n::tr("No snapshots yet. The first daily one is saved after the computer has been awake for an hour, or save one now.")
                                      : I18n::tr("No snapshots yet. Save one now, or turn on Daily snapshot."),
                                  m_rows);
        empty->setWordWrap(true);
        column->addWidget(empty);
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QSize screen = m_module->currentScreen();
    for (int index = 0; index < list.size(); ++index) {
        const DesktopSnapshot &snapshot = list.at(index);
        auto *row = new QWidget(m_rows);
        row->setObjectName(QStringLiteral("snapshotRow"));
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 10, 0, 10);
        line->setSpacing(9);
        line->addWidget(new MiniDesktop(snapshot.layout, row), 0, Qt::AlignTop);

        auto *texts = new QVBoxLayout();
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(3);
        auto *head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(5);
        const QString when = DesktopSnapshots::whenText(snapshot.when, now);
        // Un nombre largo se corta con "..." (deja a la vista el tipo y los botones) y entero en el tooltip.
        auto *title = new ElidedLabel(row);
        title->setObjectName(QStringLiteral("optionLabel"));
        title->setText(snapshot.name.isEmpty() ? when : snapshot.name);
        title->setAccessibleName(title->text());
        head->addWidget(title, 1, Qt::AlignVCenter);
        // Los tres tipos con el mismo chip de contorno: ninguno parece "activo".
        auto *kind = new Chip(row);
        kind->set(QStringLiteral("src"), kindText(snapshot.kind));
        head->addWidget(kind, 0, Qt::AlignVCenter);
        head->addSpacing(5);

        // "Restore" + tacho, y en su mismo lugar "Confirm delete" mientras se confirma: la fila no salta.
        auto *actions = new QWidget(row);
        auto *slot = new QStackedLayout(actions);
        slot->setContentsMargins(0, 0, 0, 0);
        auto *normal = new QWidget(actions);
        auto *normalLine = new QHBoxLayout(normal);
        normalLine->setContentsMargins(0, 0, 0, 0);
        normalLine->setSpacing(5);
        normalLine->addStretch(1);
        auto *restore = Ui::button(I18n::tr("Restore"), QString(), QStringLiteral("sm"), normal);
        restore->setObjectName(QStringLiteral("restoreSnapshot"));
        restore->setFocusPolicy(Qt::NoFocus);
        CustomTooltip::instance()->setToolTip(restore, I18n::tr("The current arrangement is saved first, so you can undo."));
        normalLine->addWidget(restore, 0, Qt::AlignVCenter);
        auto *remove = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), normal);
        remove->setObjectName(QStringLiteral("deleteSnapshot"));
        remove->setFocusPolicy(Qt::NoFocus);
        Ui::setIcon(remove, Icon::Trash, Theme::color(Theme::kIcon), 14);
        remove->setAccessibleName(I18n::tr("Delete snapshot"));
        CustomTooltip::instance()->setToolTip(remove, I18n::tr("Delete this snapshot. Click again to confirm."));
        normalLine->addWidget(remove, 0, Qt::AlignVCenter);
        auto *armed = new QWidget(actions);
        auto *armedLine = new QHBoxLayout(armed);
        armedLine->setContentsMargins(0, 0, 0, 0);
        auto *confirm = Ui::button(I18n::tr("Confirm delete"), QStringLiteral("danger"), QStringLiteral("sm"), armed);
        confirm->setObjectName(QStringLiteral("confirmDeleteSnapshot"));
        confirm->setFocusPolicy(Qt::NoFocus);
        confirm->setSizePolicy(QSizePolicy::Expanding, confirm->sizePolicy().verticalPolicy());
        armedLine->addWidget(confirm, 1, Qt::AlignVCenter);
        slot->addWidget(normal);
        slot->addWidget(armed);
        head->addWidget(actions, 0, Qt::AlignVCenter);
        m_deleteSlots.insert(snapshot.id, slot);
        texts->addLayout(head);

        QStringList parts;
        if (!snapshot.name.isEmpty()) parts << when;
        parts << iconsText(snapshot.layout.icons.size()) << screenText(snapshot.layout.screen);
        auto *caption = Ui::caption(parts.join(QStringLiteral(" · ")), row);
        caption->setWordWrap(true);
        texts->addWidget(caption);
        // Otra resolucion que la de ahora: se avisa ANTES de restaurar (los iconos pueden quedar fuera).
        if (!screen.isEmpty() && snapshot.layout.screen != screen) {
            auto *warn = Ui::caption(I18n::tr("Other resolution: the screen is %1 now.").arg(screenText(screen)), row);
            Ui::setStyleProperty(warn, "tone", QStringLiteral("warn"));
            warn->setWordWrap(true);
            texts->addWidget(warn);
        }
        line->addLayout(texts, 1);
        column->addWidget(row);
        // Como en Window Layouts: una divisoria despues de cada fila, la ultima la separa de la nota.
        Ui::addDivider(column, m_rows);

        if (m_interactive) {
            const QString id = snapshot.id;
            connect(restore, &QPushButton::clicked, this, [this, id]() {
                armDelete(QString());
                runLater([this, id]() {
                    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
                    m_module->restore(id);
                    QGuiApplication::restoreOverrideCursor();
                    scrollToTop();
                });
            });
            connect(remove, &QPushButton::clicked, this, [this, id]() { armDelete(id); });
            connect(confirm, &QPushButton::clicked, this, [this, id]() {
                runLater([this, id]() { m_module->remove(id); });
            });
        }
    }
}

void DesktopIconsPanel::armDelete(const QString &id)
{
    for (auto it = m_deleteSlots.cbegin(); it != m_deleteSlots.cend(); ++it) {
        it.value()->setCurrentIndex(it.key() == id ? 1 : 0);
    }
}

void DesktopIconsPanel::armFirstDeleteForCapture()
{
    armDelete(m_firstId);
}

void DesktopIconsPanel::saveSnapshot()
{
    if (m_module->saveManual(m_name->text())) {
        m_name->clear();
    }
    m_name->clearFocus();
}

void DesktopIconsPanel::runLater(const std::function<void()> &action)
{
    // El boton que se apreto se borra al rearmar la lista: la accion corre cuando su click ya termino.
    QTimer::singleShot(0, this, action);
}

void DesktopIconsPanel::scrollToTop()
{
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(ancestor)) {
            scroll->verticalScrollBar()->setValue(0);
            return;
        }
    }
}

void DesktopIconsPanel::hideEvent(QHideEvent *event)
{
    armDelete(QString());
    QWidget::hideEvent(event);
}

void DesktopIconsPanel::mousePressEvent(QMouseEvent *event)
{
    // Un click fuera de los botones (llega aca desde las tarjetas y las filas) cancela el borrado armado.
    armDelete(QString());
    QWidget::mousePressEvent(event);
}

bool DesktopIconsPanel::eventFilter(QObject *watched, QEvent *event)
{
    // Escape descarta lo escrito y suelta el campo (regla de la app).
    if (watched == m_name && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_name->clear();
        m_name->clearFocus();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
