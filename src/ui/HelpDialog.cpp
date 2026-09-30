#include "ui/HelpDialog.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"
#include "core/I18n.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

// Medidas del canvas (seccion 6): 520 de ancho con el borde, 20 x 24 de relleno, 14 entre bloques.
// El contenido arranca a 25/21 del borde de afuera (1 de borde + el relleno).
constexpr int DIALOG_WIDTH = 520;
constexpr int kPadX = 25;
constexpr int kPadY = 21;
// Cada paso es una `.row` de 22 de alto con 6 entre pasos (28 de paso a paso), 18 px por linea, el
// numero en 20 y 5 de separacion hasta el texto.
constexpr int kStepRow = 22;
constexpr int kStepGap = 6;
constexpr int kStepLine = 18;
constexpr int kNumberWidth = 20;
constexpr int kNumberGap = 5;
// Aire que se deja arriba y abajo cuando el dialogo se acota a la pantalla.
constexpr int kScreenMargin = 24;

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
    setWindowTitle(I18n::tr("Help"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(DIALOG_WIDTH);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPadX, kPadY, kPadX, kPadY);
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
    close->setToolTip(I18n::tr("Close"));
    titleRow->addWidget(close, 0, Qt::AlignVCenter);
    header->addLayout(titleRow);
    // En el canvas el bloque de abajo sube 8 sobre los 14 de separacion (quedan 6) sobre una fila de
    // 24; aca la fila mide 26 por el boton de cerrar: 4.
    header->addSpacing(4);
    header->addWidget(Ui::label(I18n::tr("Developed by Lega Pugliese"), "helpDeveloped", this));
    auto *link = new LinkLabel(QStringLiteral("github.com/legandrop"), QStringLiteral("https://github.com/legandrop"), this);
    link->setObjectName(QStringLiteral("helpLink"));
    header->addWidget(link);
    layout->addLayout(header);

    auto *rule = new QFrame(this);
    rule->setObjectName(QStringLiteral("helpRule"));
    layout->addWidget(rule);

    // Las secciones (una por herramienta, prendida o apagada: la de una apagada dice que hace antes de
    // prenderla) y la nota del final, en su propio scroll.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("helpScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setObjectName(QStringLiteral("helpViewport"));
    auto *body = new QWidget(m_scroll);
    body->setObjectName(QStringLiteral("helpContent"));
    body->setAutoFillBackground(false);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(14);
    for (const HelpSection &section : sections) {
        auto *block = new QVBoxLayout();
        block->setSpacing(kStepGap);
        QLabel *title = Ui::label(section.title, "helpToolTitle", body);
        title->setFixedHeight(16); // la linea de 13.5 px del canvas
        block->addWidget(title);
        for (int i = 0; i < section.steps.size(); ++i) {
            // Numero en su propia columna de ancho fijo: con "1." y "2." en el mismo texto, el ancho
            // distinto de las cifras corria el comienzo de cada paso.
            // La fila es un widget de 22 de alto minimo (`.row` del canvas): un puntal dentro del
            // layout sumaria otra separacion de 5 y le quitaria ancho al texto.
            auto *rowWidget = new QWidget(body);
            rowWidget->setMinimumHeight(kStepRow);
            auto *row = new QHBoxLayout(rowWidget);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(kNumberGap);
            auto *number = new RichLineLabel(QStringLiteral("%1.").arg(i + 1), kStepLine, body);
            number->setObjectName(QStringLiteral("helpBody"));
            number->setFixedWidth(kNumberWidth);
            row->addWidget(number, 0, Qt::AlignTop);
            auto *step = new RichLineLabel(section.steps.at(i), kStepLine, body);
            step->setObjectName(QStringLiteral("helpBody"));
            row->addWidget(step, 1); // sin alineacion: con ella el layout no le pregunta el alto por ancho
            block->addWidget(rowWidget);
        }
        if (!section.note.isEmpty()) {
            auto *note = new CaptionLabel(section.note, body, 17);
            note->setObjectName(QStringLiteral("helpNote"));
            block->addWidget(note);
        }
        bodyLayout->addLayout(block);
    }
#ifdef Q_OS_MACOS
    const QString closeNote = I18n::tr("Closing the window keeps LGA Mighty Tools running in the menu bar.");
#else
    const QString closeNote = I18n::tr("Closing the window keeps LGA Mighty Tools running in the tray.");
#endif
    auto *note = Ui::label(closeNote, "helpNote", body);
    note->setWordWrap(true);
    bodyLayout->addWidget(note);
    bodyLayout->addStretch(1);
    m_scroll->setWidget(body);
    layout->addWidget(m_scroll, 1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *closeButton = Ui::button(I18n::tr("Close"), QString(), QString(), this);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    // Conexiones internas del dialogo (cerrar): ninguna escribe estado.
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void HelpDialog::fitHeight(int maxHeight)
{
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>()) {
        child->ensurePolished();
    }
    // Alto natural: lo fijo (encabezado, regla, "Close", rellenos) mas todo el cuerpo sin scroll.
    QWidget *body = m_scroll->widget();
    const int innerWidth = DIALOG_WIDTH - 2 * kPadX;
    body->layout()->invalidate();
    body->layout()->activate();
    const int bodyHeight = body->layout()->hasHeightForWidth() ? body->layout()->totalHeightForWidth(innerWidth)
                                                               : body->layout()->totalSizeHint().height();
    m_scroll->setMinimumHeight(0);
    m_scroll->setFixedHeight(bodyHeight);
    layout()->invalidate();
    layout()->activate();
    const int natural = layout()->totalSizeHint().height();
    int height = natural;
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (maxHeight > 0 && natural > maxHeight) {
        // No entra: el cuerpo scrollea y lo demas queda a la vista. La barra tiene su lugar desde el
        // principio: el texto se parte ya contando su ancho y no queda debajo de ella.
        height = maxHeight;
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        m_scroll->setFixedHeight(qMax(80, bodyHeight - (natural - maxHeight)));
        m_scroll->verticalScrollBar()->setValue(0);
    }
    setFixedHeight(height);
    resize(DIALOG_WIDTH, height);
}

int HelpDialog::execOver(QWidget *window)
{
    auto *scrim = new Scrim(window);
    scrim->show();
    // Acotado a la pantalla donde esta la ventana.
    const QScreen *screen = window->screen();
    const QRect area = screen ? screen->availableGeometry() : QRect();
    fitHeight(area.isValid() ? area.height() - 2 * kScreenMargin : 0);
    QPoint topLeft = window->mapToGlobal(window->rect().center()) - QPoint(width() / 2, height() / 2);
    if (area.isValid()) {
        topLeft.setX(qBound(area.left(), topLeft.x(), area.right() - width()));
        topLeft.setY(qBound(area.top() + kScreenMargin, topLeft.y(), area.bottom() - kScreenMargin - height()));
    }
    move(topLeft);
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
