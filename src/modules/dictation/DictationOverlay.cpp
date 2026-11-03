#include "modules/dictation/DictationOverlay.h"

#include "core/I18n.h"
#include "platform/AudioCapture.h"
#include "platform/OverlayWindow.h"
#include "ui/Theme.h"

#include <QCursor>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTextLayout>

#include <cmath>

namespace {

constexpr int kShadow = 8;     // margen alrededor de la tarjeta para la sombra
constexpr int kPadX = 14;
constexpr int kPadY = 11;
constexpr int kRadius = 8;     // el de las tarjetas de la app (QFrame#card)
constexpr int kDot = 8;
constexpr int kDotGap = 7;
constexpr int kBars = 4;
constexpr int kBarWidth = 3;
constexpr int kBarGap = 2;
constexpr int kBarMax = 14;
constexpr int kIndicator = kDot + kDotGap + kBars * kBarWidth + (kBars - 1) * kBarGap;
constexpr int kTextGap = 12;
constexpr int kMaxTextWidth = 420;
constexpr int kMaxLines = 3;
constexpr int kBottomMargin = 56; // sobre la barra de tareas
constexpr int kFadeMs = 140;
// Alto relativo de cada barra: con el mismo nivel no suben parejas, como una voz.
constexpr float kBarShape[kBars] = {0.6f, 1.0f, 0.8f, 0.5f};
const QChar kEllipsis(0x2026);

} // namespace

DictationOverlay::DictationOverlay(QWidget *parent)
    : QWidget(parent)
{
    if (!parent) {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus
                       | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setFont(Theme::uiFont(14));
    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, [this]() { fadeTo(0.0); });
    m_fade = new QPropertyAnimation(this, "windowOpacity", this);
    m_fade->setDuration(kFadeMs);
    connect(m_fade, &QPropertyAnimation::finished, this, [this]() {
        if (m_hiding) hide();
    });
    relayout();
}

DictationOverlay::~DictationOverlay() = default;

void DictationOverlay::setState(State state, const QString &message)
{
    m_state = state;
    m_message = message;
    relayout();
}

void DictationOverlay::setLevel(float level)
{
    m_level = qMax(qBound(0.0f, level, 1.0f), m_level * 0.7f);
    update();
}

void DictationOverlay::setText(const QString &confirmed, const QString &provisional)
{
    m_confirmed = confirmed.simplified();
    m_provisional = provisional.simplified();
    relayout();
}

QSize DictationOverlay::sizeHint() const
{
    const int width = kPadX + indicatorWidth() + kTextGap + m_textSize.width() + kPadX;
    const int height = kPadY + qMax(m_textSize.height(), kBarMax) + kPadY;
    return QSize(width + 2 * kShadow, height + 2 * kShadow);
}

void DictationOverlay::relayout()
{
    // El texto entero y dónde empieza lo provisional (-1: todo de un color).
    QString text;
    int provisionalAt = -1;
    QColor color = Theme::color(Theme::kTextBright);
    if (m_state == State::Error) {
        text = m_message.simplified();
        color = Theme::color(Theme::kError);
    } else if (!m_confirmed.isEmpty() || !m_provisional.isEmpty()) {
        text = m_confirmed;
        if (!m_provisional.isEmpty()) {
            if (!text.isEmpty()) text += QLatin1Char(' ');
            provisionalAt = text.size();
            text += m_provisional;
        }
    } else {
        text = m_state == State::Loading        ? I18n::tr("Loading model…")
             : m_state == State::Transcribing ? I18n::tr("Transcribing…")
                                                : I18n::tr("Listening…");
        color = Theme::color(Theme::kTextMuted);
    }

    // Lo último dicho a la vista: si no entra en kMaxLines, se corta desde el principio y se marca con «…».
    const qreal lineHeight = QFontMetricsF(font(), this).lineSpacing();
    int offset = 0;
    for (int pass = 0;; ++pass) {
        const QString shown = offset > 0 ? kEllipsis + text.mid(offset) : text;
        const int prefix = offset > 0 ? 1 : 0;
        QList<QTextLayout::FormatRange> formats;
        QTextLayout::FormatRange body;
        body.start = 0;
        body.length = int(shown.size());
        body.format.setForeground(color);
        if (provisionalAt >= 0) {
            const int at = qMax(0, provisionalAt - offset) + prefix;
            body.length = at;
            QTextLayout::FormatRange provisional;
            provisional.start = at;
            provisional.length = int(shown.size()) - at;
            provisional.format.setForeground(Theme::color(Theme::kTextMuted));
            formats << provisional;
        }
        formats.prepend(body);

        // Medido con el DPI de este widget, el mismo con el que se pinta (sin el, con --ui-scale el texto no entra).
        auto layout = std::make_unique<QTextLayout>(shown, font(), this);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout->setTextOption(option);
        layout->setFormats(formats);
        layout->beginLayout();
        qreal y = 0.0;
        qreal width = 0.0;
        for (QTextLine line = layout->createLine(); line.isValid(); line = layout->createLine()) {
            line.setLineWidth(kMaxTextWidth);
            line.setPosition(QPointF(0.0, y));
            y += lineHeight;
            width = qMax(width, line.naturalTextWidth());
        }
        layout->endLayout();
        const int lines = layout->lineCount();
        if (lines <= kMaxLines || pass == 8) {
            m_layout = std::move(layout);
            m_textSize = QSize(int(std::ceil(width)), int(std::ceil(y)));
            break;
        }
        // Desde el principio de la antepenúltima línea, sin los espacios del corte.
        int next = offset + layout->lineAt(lines - kMaxLines).textStart() - prefix;
        next = qMax(next, offset + 1);
        while (next < text.size() && text.at(next).isSpace()) ++next;
        offset = next;
    }
    // Fija: ni un layout ni el tope de 2/3 de pantalla de adjustSize() la achican y cortan el texto.
    setFixedSize(sizeHint());
    if (isWindow() && isVisible()) place();
    update();
}

void DictationOverlay::place()
{
    QScreen *screen = m_screen ? m_screen.data() : QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect area = screen->availableGeometry();
    const QSize size = sizeHint();
    // La tarjeta (sin el margen de la sombra) queda a kBottomMargin del borde de abajo del área útil.
    move(area.center().x() - size.width() / 2, area.bottom() + 1 - kBottomMargin - (size.height() - kShadow));
}

void DictationOverlay::showFor(quintptr nativeWindow)
{
    m_hideTimer.stop();
    m_screen = OverlayWindow::screenOf(nativeWindow);
    if (!m_screen) m_screen = QGuiApplication::screenAt(QCursor::pos());
    if (isWindow() && !m_nativeReady) {
        winId(); // la ventana nativa existe antes de mostrarla: se la marca para que nunca se active
        OverlayWindow::makeNonActivating(this);
        m_nativeReady = true;
    }
    place();
    if (!isVisible()) {
        setWindowOpacity(0.0);
        show();
    }
    fadeTo(1.0);
}

void DictationOverlay::hideSoon(int delayMs)
{
    if (isVisible()) m_hideTimer.start(qMax(0, delayMs));
}

void DictationOverlay::fadeTo(qreal opacity)
{
    m_fade->stop();
    m_hiding = opacity <= 0.0;
    m_fade->setStartValue(windowOpacity());
    m_fade->setEndValue(opacity);
    m_fade->start();
}

void DictationOverlay::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(kShadow + 0.5, kShadow + 0.5, -kShadow - 0.5, -kShadow - 0.5);
    QPainterPath shape;
    shape.addRoundedRect(card, kRadius, kRadius);

    // Sombra en trazos concéntricos, como el tooltip propio, un poco más marcada: flota sobre cualquier app.
    for (int i = 0; i < 4; ++i) {
        painter.setPen(QPen(QColor(0, 0, 0, 22 - i * 5), (i + 1) * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
    }
    painter.setPen(QPen(Theme::color(Theme::kBorderStrong), 1.0));
    painter.setBrush(Theme::color(Theme::kTile));
    painter.drawPath(shape);

    // Punto y medidor, centrados en el alto de la tarjeta.
    const qreal middle = card.center().y();
    qreal x = card.left() + kPadX;
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(m_state == State::Transcribing ? Theme::kTextFaint : Theme::kError));
    painter.drawEllipse(QPointF(x + kDot / 2.0, middle), kDot / 2.0, kDot / 2.0);
    x += kDot + kDotGap;
    if (m_state != State::Error) {
        painter.setBrush(Theme::color(recording() ? Theme::kTextStrong : Theme::kBorderStrong));
        for (int i = 0; i < kBars; ++i) {
            const qreal height = recording() ? qMax<qreal>(3.0, std::round(m_level * kBarMax * kBarShape[i])) : 3.0;
            painter.drawRoundedRect(QRectF(x, middle - height / 2.0, kBarWidth, height), 1.5, 1.5);
            x += kBarWidth + kBarGap;
        }
    }

    if (m_layout) {
        // Centrado por el alto de línea: medido en la captura, la mayúscula queda centrada con el punto.
        const QPointF origin(card.left() - 0.5 + kPadX + indicatorWidth() + kTextGap, middle - m_textSize.height() / 2.0);
        m_layout->draw(&painter, origin);
    }
}

int DictationOverlay::indicatorWidth() const
{
    return m_state == State::Error ? kDot : kIndicator; // un error no muestra el medidor
}

QStringList DictationOverlay::captureStates()
{
    return {QStringLiteral("overlay-listening"), QStringLiteral("overlay-live"),         QStringLiteral("overlay-long"),
            QStringLiteral("overlay-loading"),   QStringLiteral("overlay-transcribing"), QStringLiteral("overlay-error")};
}

DictationOverlay *DictationOverlay::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (!captureStates().contains(state)) {
        return nullptr;
    }
    // Lo dictado es del usuario, no de la interfaz: el mismo texto en los dos idiomas.
    const QString confirmed = QStringLiteral("Che, ¿vos tenés el render de la compo de ayer?");
    const QString longText = QStringLiteral(
        "Te paso el plano 040 con el roto terminado y la compo casi lista. Falta ajustar el grano del fondo y revisar "
        "el borde del pelo en los primeros cuadros, que en Nuke se ve bien pero en el render final aparece un halo. "
        "Si podés, mirá también la versión anterior para comparar, porque el cliente pidió que el color quede igual "
        "al de la toma");
    auto *overlay = new DictationOverlay(parent);
    if (state == QLatin1String("overlay-listening")) {
        overlay->setLevel(0.35f);
    } else if (state == QLatin1String("overlay-live")) {
        overlay->setText(confirmed, QStringLiteral("Lo necesito para mandarlo"));
        overlay->setLevel(0.7f);
    } else if (state == QLatin1String("overlay-long")) {
        overlay->setText(longText, QStringLiteral("que aprobaron la semana pasada"));
        overlay->setLevel(0.55f);
    } else if (state == QLatin1String("overlay-loading")) {
        overlay->setState(State::Loading);
        overlay->setLevel(0.5f);
    } else if (state == QLatin1String("overlay-transcribing")) {
        overlay->setState(State::Transcribing);
        overlay->setText(confirmed, QString());
    } else {
        overlay->setState(State::Error, AudioCapture::errorText(AudioCapture::Error::AccessDenied));
    }
    return overlay;
}
