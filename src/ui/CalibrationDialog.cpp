#include "ui/CalibrationDialog.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace {

constexpr int kDialogWidth = 440;
// La captura original mide 400x311; se muestra a 392 de ancho con su proporcion.
constexpr int kImageWidth = 392;
constexpr int kImageHeight = 305;

QLabel *label(const QString &text, const char *name, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QLatin1String(name));
    return l;
}

QString strong(const QString &text)
{
    return QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(QLatin1String(Theme::kTextBright), text);
}

// La captura con su borde de 1 px, dibujada a la escala real de la pantalla.
class LayoutImage : public QWidget
{
public:
    explicit LayoutImage(QWidget *parent)
        : QWidget(parent)
        , m_image(QStringLiteral(":/images/DopeSheetPos.png"))
    {
        setFixedSize(kImageWidth, kImageHeight);
        setAccessibleName(QStringLiteral("Nuke layout with a circle on an empty spot of the Dope Sheet"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRect inner = rect().adjusted(1, 1, -1, -1);
        painter.drawPixmap(inner, m_image.pixmap(inner.size(), devicePixelRatioF()));
        painter.setPen(Theme::color(Theme::kBorder));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    QIcon m_image;
};

} // namespace

CalibrationDialog::CalibrationDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("calibrationDialog"));
    setWindowTitle(QStringLiteral("Calibrate Dope Sheet"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kDialogWidth);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(0);
    layout->addWidget(label(QStringLiteral("Calibrate Dope Sheet"), "dialogTitle", this));
    layout->addSpacing(12);

    const QStringList steps = {
        QStringLiteral("Open Nuke with the %1 visible.").arg(strong(QStringLiteral("Dope Sheet"))),
        QStringLiteral("Click Start, then click an %1 inside it.").arg(strong(QStringLiteral("empty spot"))),
        QStringLiteral("The spot is saved relative to the Nuke window."),
    };
    for (int i = 0; i < steps.size(); ++i) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *badge = label(QString::number(i + 1), "stepBadge", this);
        badge->setAlignment(Qt::AlignCenter);
        row->addWidget(badge, 0, Qt::AlignVCenter);
        auto *text = label(steps.at(i), "stepText", this);
        text->setTextFormat(Qt::RichText);
        row->addWidget(text, 1, Qt::AlignVCenter);
        layout->addLayout(row);
        if (i + 1 < steps.size()) {
            layout->addSpacing(6);
        }
    }
    layout->addSpacing(14);
    layout->addWidget(new LayoutImage(this), 0, Qt::AlignLeft);
    layout->addSpacing(18);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    auto *hint = label(QStringLiteral("Esc cancels at any time."), "meta", this);
    buttons->addWidget(hint, 1, Qt::AlignVCenter);
    auto *cancel = Ui::button(QStringLiteral("Cancel"), QString(), QString(), this);
    cancel->setMinimumWidth(80);
    auto *start = Ui::button(QStringLiteral("Start"), QStringLiteral("primary"), QString(), this);
    start->setObjectName(QStringLiteral("startButton"));
    start->setMinimumWidth(80);
    // El boton que acepta va ultimo, a la derecha, y es el unico marcado (regla de Dialogs de la Base).
    buttons->addWidget(cancel);
    buttons->addWidget(start);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(start, &QPushButton::clicked, this, &QDialog::accept);
}

void CalibrationDialog::fitHeight()
{
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>()) {
        child->ensurePolished();
    }
    layout()->invalidate();
    layout()->activate();
    const int needed = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(kDialogWidth)
                                                     : layout()->totalSizeHint().height();
    setFixedHeight(needed);
}

void CalibrationDialog::centerOn(QWidget *anchor)
{
    fitHeight();
    QPoint center;
    if (anchor && anchor->isVisible()) {
        center = anchor->mapToGlobal(anchor->rect().center());
    } else if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        center = screen->availableGeometry().center();
    }
    move(center - QPoint(width() / 2, height() / 2));
}

void CalibrationDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Theme::color(Theme::kBorder), 1.0));
    painter.setBrush(Theme::color(Theme::kDialog));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}
