#include "ui/HelpDialog.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Medidas del canvas (seccion 6): 520 de ancho, 20 x 24 de relleno, 14 entre bloques.
constexpr int DIALOG_WIDTH = 520;

} // namespace

// ------------------------------------------------------------------ Scrim

Scrim::Scrim(QWidget *parent)
    : QWidget(parent)
{
    setGeometry(parent->rect());
    parent->installEventFilter(this);
}

void Scrim::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(8, 8, 8, 184));
}

bool Scrim::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parent() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

// ------------------------------------------------------------------ HelpDialog

HelpDialog::HelpDialog(const QList<HelpSection> &sections, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("helpDialog"));
    setWindowTitle(QStringLiteral("Help"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(DIALOG_WIDTH);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    // Encabezado igual al Help de las otras apps LGA (HelpTab de FileManager S3): nombre en violeta,
    // version en gris claro, "Developed by" y el link, todo pegado sin aire entre lineas.
    auto *header = new QVBoxLayout();
    header->setSpacing(0);
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    titleRow->addWidget(Ui::label(QStringLiteral("LGA Mighty Tools"), "helpTitle", this), 0, Qt::AlignBaseline);
    titleRow->addWidget(Ui::label(QStringLiteral("v" MIGHTYTOOLS_VERSION), "helpVersion", this), 0, Qt::AlignBaseline);
    titleRow->addStretch(1);
    auto *close = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    Ui::setIcon(close, Icon::X, Theme::color(Theme::kIcon));
    close->setToolTip(QStringLiteral("Close"));
    titleRow->addWidget(close, 0, Qt::AlignVCenter);
    header->addLayout(titleRow);
    header->addWidget(Ui::label(QStringLiteral("Developed by Lega Pugliese"), "helpDeveloped", this));
    auto *link = new LinkLabel(QStringLiteral("github.com/legandrop"), QStringLiteral("https://github.com/legandrop"), this);
    link->setObjectName(QStringLiteral("helpLink"));
    header->addWidget(link);
    layout->addLayout(header);

    auto *rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("helpRule"));
    layout->addWidget(rule);

    // Una seccion por herramienta, prendida o apagada: la de una apagada dice que hace antes de
    // prenderla.
    for (const HelpSection &section : sections) {
        auto *block = new QVBoxLayout();
        block->setSpacing(6);
        block->addWidget(Ui::label(section.title, "helpToolTitle", this));
        for (int i = 0; i < section.steps.size(); ++i) {
            // Numero en su propia columna de ancho fijo: con "1." y "2." en el mismo texto, el ancho
            // distinto de las cifras corria el comienzo de cada paso.
            auto *row = new QHBoxLayout();
            row->setSpacing(0);
            auto *number = Ui::label(QStringLiteral("%1.").arg(i + 1), "helpBody", this);
            number->setFixedWidth(20);
            row->addWidget(number, 0, Qt::AlignTop);
            auto *step = Ui::label(section.steps.at(i), "helpBody", this);
            step->setTextFormat(Qt::RichText);
            step->setWordWrap(true);
            row->addWidget(step, 1);
            block->addLayout(row);
        }
        if (!section.note.isEmpty()) {
            auto *note = Ui::label(section.note, "helpNote", this);
            note->setWordWrap(true);
            block->addWidget(note);
        }
        layout->addLayout(block);
    }

#ifdef Q_OS_MACOS
    const QString where = QStringLiteral("the menu bar");
#else
    const QString where = QStringLiteral("the tray");
#endif
    auto *note = Ui::label(QStringLiteral("Closing the window keeps LGA Mighty Tools running in %1.").arg(where),
                           "helpNote", this);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *closeButton = Ui::button(QStringLiteral("Close"), QString(), QString(), this);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    // Conexiones internas del dialogo (cerrar): ninguna escribe estado.
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void HelpDialog::fitHeight()
{
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>()) {
        child->ensurePolished();
    }
    layout()->invalidate();
    layout()->activate();
    const int needed = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(DIALOG_WIDTH)
                                                     : layout()->totalSizeHint().height();
    setMinimumHeight(needed);
    resize(DIALOG_WIDTH, needed);
}

int HelpDialog::execOver(QWidget *window)
{
    auto *scrim = new Scrim(window);
    scrim->show();
    fitHeight();
    const QPoint center = window->mapToGlobal(window->rect().center());
    move(center - QPoint(width() / 2, height() / 2));
    const int result = exec();
    delete scrim;
    return result;
}

void HelpDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Theme::color(Theme::kBorder), 1.0));
    painter.setBrush(Theme::color(Theme::kDialog));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}
