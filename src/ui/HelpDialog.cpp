#include "ui/HelpDialog.h"
#include "core/Shortcut.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int DIALOG_WIDTH = 400;
constexpr auto kGithubUrl = "https://github.com/legandrop";

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

// Link a GitHub como el GitHubLinkLabel del Help de FileManager S3: texto plano subrayado que
// cambia de color con el mouse encima. Un <a> dentro de un QLabel no tiene hover, por eso es un
// label propio. El color es mas claro que el de las otras apps, que casi no se lee sobre oscuro.
class GithubLink : public QLabel
{
public:
    explicit GithubLink(QWidget *parent) : QLabel(QStringLiteral("github.com/legandrop"), parent)
    {
        setObjectName(QStringLiteral("helpLink"));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(QLatin1String(kGithubUrl));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
            Ui::setStyleProperty(this, "hover", event->type() == QEvent::HoverEnter);
        }
        return QLabel::event(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            QDesktopServices::openUrl(QUrl(QLatin1String(kGithubUrl)));
        }
        QLabel::mouseReleaseEvent(event);
    }
};

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

HelpDialog::HelpDialog(const Shortcut &addKeyframe, const Shortcut &frameDopeSheet, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("helpDialog"));
    setWindowTitle(QStringLiteral("Help"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(DIALOG_WIDTH);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(12);

    // Encabezado igual al Help de las otras apps LGA (HelpTab de FileManager S3): nombre en violeta,
    // version en gris claro, "Developed by" y el link, todo pegado sin aire entre lineas.
    auto *header = new QVBoxLayout();
    header->setSpacing(0);
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    titleRow->addWidget(label(QStringLiteral("LGA Mighty Tools"), "helpTitle", this), 0, Qt::AlignBaseline);
    titleRow->addWidget(label(QStringLiteral("v" MIGHTYTOOLS_VERSION), "helpVersion", this), 0, Qt::AlignBaseline);
    titleRow->addStretch(1);
    auto *close = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    Ui::setIcon(close, Icon::X, Theme::color(Theme::kIcon));
    close->setToolTip(QStringLiteral("Close"));
    titleRow->addWidget(close, 0, Qt::AlignVCenter);
    header->addLayout(titleRow);
    header->addWidget(label(QStringLiteral("Developed by Lega Pugliese"), "helpDeveloped", this));
    header->addWidget(new GithubLink(this));
    layout->addLayout(header);

    auto *rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("helpRule"));
    layout->addWidget(rule);

    layout->addWidget(label(QStringLiteral("How it works"), "helpSection", this));
    const QStringList steps = {
        QStringLiteral("Put the pointer over a knob in Nuke and press %1 to set a key.")
            .arg(strong(addKeyframe.displayText())),
        QStringLiteral("Calibrate the %1 once: one click on an empty spot.").arg(strong(QStringLiteral("Dope Sheet"))),
        QStringLiteral("Press %1 to select every key in the Dope Sheet and frame them.")
            .arg(strong(frameDopeSheet.displayText())),
        QStringLiteral("Under %1, add the drives to watch: you get a notification when one runs low.")
            .arg(strong(QStringLiteral("Disk space"))),
    };
    auto *stepsBox = new QVBoxLayout();
    stepsBox->setSpacing(4);
    for (int i = 0; i < steps.size(); ++i) {
        // Numero en su propia columna de ancho fijo: con "1." y "2." en el mismo texto, el ancho
        // distinto de las cifras corria el comienzo de cada paso.
        auto *row = new QHBoxLayout();
        row->setSpacing(0);
        auto *number = label(QStringLiteral("%1.").arg(i + 1), "helpBody", this);
        number->setFixedWidth(20);
        row->addWidget(number, 0, Qt::AlignTop);
        auto *l = label(steps.at(i), "helpBody", this);
        l->setTextFormat(Qt::RichText);
        l->setWordWrap(true);
        row->addWidget(l, 1);
        stepsBox->addLayout(row);
    }
    layout->addLayout(stepsBox);

#ifdef Q_OS_MACOS
    const QString where = QStringLiteral("the menu bar");
#else
    const QString where = QStringLiteral("the tray");
#endif
    auto *note = label(QStringLiteral("The shortcuts only work while Nuke is in front; other apps keep these keys. "
                                      "Closing the window keeps Nuke Shortcuts running in %1.")
                           .arg(where),
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
