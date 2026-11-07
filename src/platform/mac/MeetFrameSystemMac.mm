#include "platform/MeetFrameSystem.h"

#include "platform/AppToggleSystem.h"

#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QWidget>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <dlfcn.h>
#include <functional>
#include <memory>
#include <unistd.h>

// Meet Frame en macOS. ScreenCaptureKit captura la region del monitor con un filtro que deja afuera lo que se
// tapa: el filtro incluye las apps que corren MENOS las tapadas, el Centro de notificaciones y esta misma app (el
// marco y la ventana compartida nunca salen), y exceptua las ventanas del escritorio (nivel negativo: fondo e
// iconos) y las del navegador cuyo titulo coincide. El filtro se rearma cada segundo con las apps que corren:
// una app que se abre entre medio NO sale hasta el proximo rearmado (tapa de mas, nunca de menos).
//
// La ventana compartida es una ventana Qt comun («Meet Frame») que dibuja el ultimo cuadro. Como en Windows, vive
// al fondo y no recibe el mouse (ignoresMouseEvents): un click que pasa por adentro del marco nunca la trae al
// frente (se veia la region repetida, con un segundo cursor). Si la app se activa (la ventana principal, Cmd-Tab),
// vuelve al fondo. Lo tapado no se pinta de un color: simplemente no esta, y se ve lo de abajo o el color de fondo.
//
// El marco (prepareFrame) no activa la app al hacerle click, como WS_EX_NOACTIVATE en Windows. Con la app
// inactiva Qt descarta el movimiento del mouse y no pone cursores, y en mac no implementa startSystemResize: el
// marco se estira y se mueve a mano (MeetFrameOverlay) y el cursor lo pone trackFrameMouse/setFrameCursor.
// trackFrameMouse mira el mouse con monitores de eventos (los del resto de las apps y los propios) y, adentro de
// la region, pone ignoresMouseEvents: la ventana del marco cubre toda la region y asi ningun click ni arrastre
// queda en ella, pasa entero a la app de abajo. En el borde y la barrita lo vuelve a sacar.
//
// Para el log (takeStats, takeFrameMouseStats): cuadros que llegan, se descartan y se dibujan, cuanto tarda
// cada cosa en el hilo principal, y cuantas veces se reconfigura la captura.

@interface LGAMeetFrameSink : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, copy) void (^onFrame)(CVPixelBufferRef buffer);
@property(nonatomic, copy) void (^onStop)(NSError *error);
@end

@implementation LGAMeetFrameSink
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    Q_UNUSED(stream);
    if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sampleBuffer)) return;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, false);
    if (attachments && CFArrayGetCount(attachments) > 0) {
        NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
        NSNumber *status = info[SCStreamFrameInfoStatus];
        if (status && status.integerValue != SCFrameStatusComplete) return; // sin cambios o en pausa del sistema
    }
    CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (buffer && self.onFrame) self.onFrame(buffer);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    Q_UNUSED(stream);
    if (self.onStop) self.onStop(error);
}
@end

namespace MeetFrameSystem {

namespace {

// La ventana que se elige en la reunion: dibuja el ultimo cuadro, nada mas.
class SharedWindow : public QWidget
{
public:
    explicit SharedWindow(const QString &title)
    {
        setWindowTitle(title);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_ShowWithoutActivating);
        // No recibe el mouse: los clicks que pasan por adentro del marco nunca la traen al frente. Con el flag
        // (y no con ignoresMouseEvents a mano) Qt lo mantiene si algo vuelve a aplicar los flags.
        setWindowFlag(Qt::WindowTransparentForInput, true);
    }
    void setFrame(const QImage &image)
    {
        m_image = image;
        // El cuadro viene en pixeles reales: si coincide con la ventana en la densidad de su pantalla, se dibuja
        // 1:1 sin escalar (lo comun). Si no (otra pantalla, o mientras se estira), se escala suavizado.
        const qreal dpr = devicePixelRatioF();
        m_exact = m_image.size() == QSize(qRound(width() * dpr), qRound(height() * dpr));
        if (m_exact) m_image.setDevicePixelRatio(dpr);
        update();
    }
    void setFill(const QColor &fill) { m_fill = fill; }
    std::function<void()> onClose;
    // Para el log: dibujos, ms en total y el mas lento desde la llamada anterior.
    void takePaintStats(int *count, double *totalMs, double *maxMs)
    {
        *count = m_paints;
        *totalMs = m_paintMs;
        *maxMs = m_paintMaxMs;
        m_paints = 0;
        m_paintMs = m_paintMaxMs = 0;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QElapsedTimer clock;
        clock.start();
        QPainter painter(this);
        const bool exact = m_exact && !m_image.isNull();
        if (!exact) painter.fillRect(rect(), m_fill);
        if (exact) {
            painter.drawImage(QPointF(0, 0), m_image);
        } else if (!m_image.isNull()) {
            // Mientras se estira, el cuadro todavia es del tamaño anterior.
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(QRectF(rect()), m_image);
        }
        const double ms = clock.nsecsElapsed() / 1e6;
        ++m_paints;
        m_paintMs += ms;
        m_paintMaxMs = std::max(m_paintMaxMs, ms);
    }
    void closeEvent(QCloseEvent *event) override
    {
        event->ignore();
        if (onClose) onClose();
    }

private:
    QImage m_image;
    bool m_exact = false;
    QColor m_fill = Qt::black;
    int m_paints = 0;
    double m_paintMs = 0;
    double m_paintMaxMs = 0;
};

// Contadores que tocan los hilos de ScreenCaptureKit (atomicos) y el principal.
struct SinkStats
{
    std::atomic<int> arrived{0};
    std::atomic<int> dropped{0};
    std::atomic<long long> copyUs{0};
    std::atomic<long long> copyMaxUs{0};
};

QString lower(NSString *text)
{
    return text ? QString::fromNSString(text).toLower() : QString();
}

class SessionMac : public Session
{
public:
    SessionMac() : m_alive(std::make_shared<std::atomic<bool>>(true)) {}

    ~SessionMac() override
    {
        *m_alive = false;
        for (id observer in m_activeObservers) [[NSNotificationCenter defaultCenter] removeObserver:observer];
        m_refresh.stop();
        stopStream();
        delete m_shared.data();
        if (m_background) CGColorRelease(m_background);
        for (CGColorRef old : m_oldBackgrounds) CGColorRelease(old);
    }

    bool open(const QString &title, const QRect &region, const Options &options, QString *error)
    {
        Q_UNUSED(error);
        m_options = options;
        m_region = region;
        updateBackground();
        m_queue = dispatch_queue_create("lga.meetframe.frames", DISPATCH_QUEUE_SERIAL);
        auto *shared = new SharedWindow(title);
        shared->setFill(options.fill);
        QPointer<SessionMac> guard(this);
        shared->onClose = [guard]() {
            if (guard) QTimer::singleShot(0, guard, [guard]() { if (guard) emit guard->closeRequested(); });
        };
        m_shared = shared;
        placeShared();
        shared->show();
        if (NSView *view = (__bridge NSView *)reinterpret_cast<void *>(shared->winId())) {
            [view.window orderBack:nil];
        }
        // Al activarse o desactivarse la app, Qt trae al frente sus ventanas que no se esconden (lo hace por
        // cualquier ventana con WA_MacAlwaysShowToolWindow, como el aviso de Disk Space): la compartida vuelve
        // al fondo despues.
        void (^toBack)(NSNotification *) = ^(NSNotification *) {
            if (!guard || !guard->m_shared) return;
            if (NSView *v = (__bridge NSView *)reinterpret_cast<void *>(guard->m_shared->winId())) {
                [v.window orderBack:nil];
            }
        };
        NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
        for (NSNotificationName name : {NSApplicationDidBecomeActiveNotification, NSApplicationDidResignActiveNotification}) {
            [m_activeObservers addObject:[center addObserverForName:name object:nil queue:[NSOperationQueue mainQueue] usingBlock:toBack]];
        }
        m_refresh.setInterval(refreshInterval());
        QObject::connect(&m_refresh, &QTimer::timeout, this, [this]() { refreshContent(); });
        m_refresh.start();
        refreshContent();
        return true;
    }

    void setRegion(const QRect &region) override
    {
        if (region == m_region || region.isEmpty()) return;
        const bool resized = region.size() != m_region.size();
        m_region = region;
        if (resized) placeShared();
        const CGRect next = CGRectMake(region.x(), region.y(), region.width(), region.height());
        ++m_regionCalls;
        if (m_stream && CGRectContainsRect(m_displayFrame, next)) {
            // Mismo monitor: solo cambia el recorte. El filtro (que apps salen) sigue con su refresco propio.
            reconfigure();
            return;
        }
        m_filterKey.clear(); // otro monitor: se rearma con el proximo contenido
        refreshContent();
    }

    void setOptions(const Options &options) override
    {
        const bool restart = options.showCursor != m_options.showCursor || options.fps != m_options.fps;
        m_options = options;
        updateBackground();
        m_refresh.setInterval(refreshInterval());
        if (m_shared) static_cast<SharedWindow *>(m_shared.data())->setFill(options.fill);
        m_filterKey.clear();
        if (restart && m_stream) {
            reconfigure();
        }
        refreshContent();
    }

    void setIgnoredWindows(const QList<QWidget *> &) override {} // las ventanas propias ya estan fuera del filtro
    int hiddenWindows() const override { return m_hidden; }

    QString takeStats() override
    {
        int paints = 0;
        double paintMs = 0, paintMaxMs = 0;
        if (m_shared) static_cast<SharedWindow *>(m_shared.data())->takePaintStats(&paints, &paintMs, &paintMaxMs);
        const int arrived = m_sinkStats->arrived.exchange(0);
        const int dropped = m_sinkStats->dropped.exchange(0);
        const long long copyUs = m_sinkStats->copyUs.exchange(0);
        const long long copyMaxUs = m_sinkStats->copyMaxUs.exchange(0);
        const int copied = std::max(1, arrived - dropped);
        const QString text = QStringLiteral("cuadros %1 llegaron, %2 descartados (el hilo principal seguia con el anterior), %3 mostrados; "
                                            "copiar med %4 ms max %5 ms (hilo de captura); dibujar %6 veces, med %7 ms max %8 ms; "
                                            "region %9 cambios, %10 reconfiguraciones (respuesta max %11 ms), %12 filtros nuevos, %13 arranques")
                                 .arg(arrived).arg(dropped).arg(m_shown)
                                 .arg(copyUs / 1000.0 / copied, 0, 'f', 2).arg(copyMaxUs / 1000.0, 0, 'f', 1)
                                 .arg(paints).arg(paints ? paintMs / paints : 0.0, 0, 'f', 2).arg(paintMaxMs, 0, 'f', 1)
                                 .arg(m_regionCalls).arg(m_reconfigs).arg(m_reconfigMaxMs, 0, 'f', 0).arg(m_filterUpdates).arg(m_starts);
        m_shown = m_regionCalls = m_reconfigs = m_filterUpdates = m_starts = 0;
        m_reconfigMaxMs = 0;
        return text;
    }
    QString error() const override { return m_error; }
    QString engine() const override { return QStringLiteral("screencapturekit"); }

private:
    // Un rincon de alguna pantalla que no pise la region (en mac no se captura a si misma, pero asi no tapa lo
    // que el usuario ve dentro del marco).
    void placeShared()
    {
        if (!m_shared) return;
        const QSize size = m_region.size();
        const QRect avoid = m_region.adjusted(-48, -48, 48, 48);
        QPoint at = m_region.topLeft();
        for (QScreen *screen : QGuiApplication::screens()) {
            const QRect area = screen->availableGeometry();
            const QList<QPoint> corners = {area.topLeft(), QPoint(area.right() + 1 - size.width(), area.top()),
                                           QPoint(area.left(), area.bottom() + 1 - size.height()),
                                           QPoint(area.right() + 1 - size.width(), area.bottom() + 1 - size.height())};
            bool found = false;
            for (const QPoint &corner : corners) {
                const QRect candidate(corner, size);
                if (area.contains(candidate) && !candidate.intersects(avoid)) {
                    at = corner;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        m_shared->setGeometry(QRect(at, size));
    }

    SCStreamConfiguration *configuration() const
    {
        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        const CGRect source = CGRectMake(m_region.x() - m_displayFrame.origin.x, m_region.y() - m_displayFrame.origin.y, m_region.width(),
                                         m_region.height());
        config.sourceRect = source;
        config.width = size_t(std::max(1.0, std::round(m_region.width() * m_scale)));
        config.height = size_t(std::max(1.0, std::round(m_region.height() * m_scale)));
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.showsCursor = m_options.showCursor;
        config.minimumFrameInterval = CMTimeMake(1, std::clamp(m_options.fps, 5, 60));
        config.queueDepth = 5;
        if (m_background) config.backgroundColor = m_background; // la propiedad no retiene el color: vive en la sesion
        return config;
    }

    // Con reglas por titulo (WhatsApp Web) se mira mas seguido: el titulo cambia al cambiar de pestaña.
    int refreshInterval() const { return m_options.rules.browserTitles.isEmpty() ? 1000 : 250; }

    void updateBackground()
    {
        // Una configuracion en vuelo puede seguir usando el color anterior: se libera al cerrar la sesion.
        if (m_background) m_oldBackgrounds.append(m_background);
        const QColor fill = m_options.fill.isValid() ? m_options.fill : QColor(Qt::black);
        m_background = CGColorCreateGenericRGB(fill.redF(), fill.greenF(), fill.blueF(), 1.0);
    }

    // Cambia el recorte o el tamaño de la captura y mide cuanto tarda ScreenCaptureKit en aceptarlo.
    void reconfigure()
    {
        if (!m_stream) return;
        ++m_reconfigs;
        auto alive = m_alive;
        SessionMac *raw = this;
        QElapsedTimer clock;
        clock.start();
        [m_stream updateConfiguration:configuration()
                    completionHandler:^(NSError *error) {
                        const double ms = clock.nsecsElapsed() / 1e6;
                        NSString *message = error ? error.localizedDescription : nil;
                        dispatch_async(dispatch_get_main_queue(), ^{
                            if (!*alive) return;
                            raw->m_reconfigMaxMs = std::max(raw->m_reconfigMaxMs, ms);
                            if (message) qWarning().noquote() << "[meetFrame] updateConfiguration fallo:" << QString::fromNSString(message);
                        });
                    }];
    }

    void stopStream()
    {
        if (m_stream) {
            [m_stream stopCaptureWithCompletionHandler:^(NSError *) {}];
        }
        m_stream = nil;
        m_sink = nil;
        m_filterKey.clear();
    }

    void refreshContent()
    {
        auto alive = m_alive;
        SessionMac *raw = this;
        [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                   onScreenWindowsOnly:YES
                                                     completionHandler:^(SCShareableContent *content, NSError *error) {
                                                         dispatch_async(dispatch_get_main_queue(), ^{
                                                             if (!*alive) return; // se mira y se borra en el hilo principal
                                                             raw->applyContent(content, error);
                                                         });
                                                     }];
    }

    void setError(const QString &error)
    {
        if (error == m_error) return;
        m_error = error;
        emit changed();
    }

    void applyContent(SCShareableContent *content, NSError *error)
    {
        if (!content || error) {
            qWarning().noquote() << "[meetFrame] ScreenCaptureKit no entrego el contenido:"
                                 << (error ? QString::fromNSString(error.localizedDescription) : QString());
            setError(QStringLiteral("Could not capture the screen."));
            return;
        }
        // El monitor con mas region.
        SCDisplay *display = nil;
        qreal bestArea = 0;
        const CGRect region = CGRectMake(m_region.x(), m_region.y(), m_region.width(), m_region.height());
        for (SCDisplay *candidate in content.displays) {
            const CGRect common = CGRectIntersection(candidate.frame, region);
            const qreal area = CGRectIsNull(common) ? 0 : common.size.width * common.size.height;
            if (area > bestArea) {
                bestArea = area;
                display = candidate;
            }
        }
        if (!display) {
            setError(QStringLiteral("Could not capture the screen."));
            return;
        }
        const MeetFrameRules::Rules &rules = m_options.rules;
        const QStringList notifications = MeetFrameRules::notificationApps();
        const pid_t self = getpid();
        NSMutableArray<SCRunningApplication *> *included = [NSMutableArray array];
        QSet<QString> hiddenApps;
        QStringList key;
        for (SCRunningApplication *app in content.applications) {
            const QString id = lower(app.bundleIdentifier);
            const bool hidden = app.processID == self || (!id.isEmpty() && rules.apps.contains(id))
                                || (rules.hideNotifications && notifications.contains(id));
            if (hidden) {
                hiddenApps.insert(QString::number(app.processID));
                continue;
            }
            [included addObject:app];
            key << QString::number(app.processID);
        }
        NSMutableArray<SCWindow *> *excepted = [NSMutableArray array];
        int hiddenCount = 0;
        for (SCWindow *window in content.windows) {
            SCRunningApplication *owner = window.owningApplication;
            const QString ownerPid = owner ? QString::number(owner.processID) : QString();
            const bool inRegion = CGRectIntersectsRect(window.frame, region);
            if (owner && hiddenApps.contains(ownerPid)) {
                if (inRegion && window.windowLayer >= 0 && owner.processID != self && window.onScreen) ++hiddenCount;
                continue;
            }
            bool except = false;
            if (rules.hideDesktop && window.windowLayer < 0) except = true; // fondo de pantalla e iconos del escritorio
            if (!except && owner && !rules.browserTitles.isEmpty() && rules.browsers.contains(lower(owner.bundleIdentifier))) {
                const QString title = lower(window.title);
                for (const QString &word : rules.browserTitles) {
                    if (title.contains(word)) {
                        except = true;
                        if (inRegion) ++hiddenCount;
                        break;
                    }
                }
            }
            if (except) {
                [excepted addObject:window];
                key << QStringLiteral("w%1").arg(window.windowID);
            }
        }
        if (hiddenCount != m_hidden) {
            m_hidden = hiddenCount;
            emit changed();
        }
        std::sort(key.begin(), key.end());
        key.prepend(QStringLiteral("d%1").arg(display.displayID));
        const QString filterKey = key.join(QLatin1Char('|'));
        if (m_stream && filterKey == m_filterKey) {
            return;
        }
        SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display includingApplications:included exceptingWindows:excepted];
        const bool sameDisplay = m_stream && display.displayID == m_displayId;
        m_displayFrame = display.frame;
        m_displayId = display.displayID;
        m_scale = 1.0;
        if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID)) {
            const size_t points = CGDisplayModeGetWidth(mode);
            if (points > 0) m_scale = qreal(CGDisplayModeGetPixelWidth(mode)) / qreal(points);
            CGDisplayModeRelease(mode);
        }
        m_filterKey = filterKey;
        if (sameDisplay) {
            [m_stream updateContentFilter:filter completionHandler:nil];
            ++m_filterUpdates;
            reconfigure();
            return;
        }
        stopStream();
        m_filterKey = filterKey;
        startStream(filter);
    }

    void startStream(SCContentFilter *filter)
    {
        LGAMeetFrameSink *sink = [[LGAMeetFrameSink alloc] init];
        // Los bloques corren en otros hilos: solo copian `alive` (atomico) y el puntero; se miran en el hilo
        // principal, que es donde se destruye la sesion.
        auto alive = m_alive;
        auto pending = m_pending;
        auto stats = m_sinkStats;
        SessionMac *raw = this;
        sink.onFrame = ^(CVPixelBufferRef buffer) {
            if (!*alive) return;
            ++stats->arrived;
            // Un cuadro por vez: si el hilo principal todavia no dibujo el anterior, este se descarta.
            if (pending->exchange(true)) {
                ++stats->dropped;
                return;
            }
            QElapsedTimer copyClock;
            copyClock.start();
            CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            const int width = int(CVPixelBufferGetWidth(buffer));
            const int height = int(CVPixelBufferGetHeight(buffer));
            const int stride = int(CVPixelBufferGetBytesPerRow(buffer));
            QImage image;
            if (void *base = CVPixelBufferGetBaseAddress(buffer)) {
                image = QImage(static_cast<const uchar *>(base), width, height, stride, QImage::Format_RGB32).copy();
            }
            CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            const long long us = copyClock.nsecsElapsed() / 1000;
            stats->copyUs += us;
            long long seen = stats->copyMaxUs.load();
            while (us > seen && !stats->copyMaxUs.compare_exchange_weak(seen, us)) {
            }
            if (image.isNull()) {
                *pending = false;
                return;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                *pending = false;
                if (!*alive) return;
                raw->onFrame(image);
            });
        };
        sink.onStop = ^(NSError *error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!*alive) return;
                qWarning().noquote() << "[meetFrame] La captura se detuvo:" << (error ? QString::fromNSString(error.localizedDescription) : QString());
                raw->m_stream = nil;
                raw->m_filterKey.clear();
                raw->setError(QStringLiteral("Could not capture the screen."));
            });
        };
        SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:configuration() delegate:sink];
        NSError *addError = nil;
        if (![stream addStreamOutput:sink type:SCStreamOutputTypeScreen sampleHandlerQueue:m_queue error:&addError]) {
            qWarning().noquote() << "[meetFrame] addStreamOutput fallo:" << QString::fromNSString(addError.localizedDescription);
            setError(QStringLiteral("Could not capture the screen."));
            return;
        }
        m_stream = stream;
        m_sink = sink;
        ++m_starts;
        [stream startCaptureWithCompletionHandler:^(NSError *error) {
            if (!error) return;
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!*alive) return;
                qWarning().noquote() << "[meetFrame] startCapture fallo:" << QString::fromNSString(error.localizedDescription);
                raw->m_stream = nil;
                raw->m_filterKey.clear();
                raw->setError(QStringLiteral("Could not capture the screen."));
            });
        }];
        qInfo().noquote() << "[meetFrame] Captura ScreenCaptureKit del monitor" << m_displayId;
    }

    void onFrame(const QImage &image)
    {
        if (m_options.paused) return; // se queda la ultima imagen
        if (!m_error.isEmpty()) setError(QString());
        ++m_shown;
        if (m_shared) static_cast<SharedWindow *>(m_shared.data())->setFrame(image);
    }

    std::shared_ptr<std::atomic<bool>> m_alive;
    NSMutableArray *m_activeObservers = [NSMutableArray array];
    QRect m_region;
    Options m_options;
    QPointer<QWidget> m_shared;
    QTimer m_refresh;
    dispatch_queue_t m_queue = nullptr;
    SCStream *m_stream = nil;
    LGAMeetFrameSink *m_sink = nil;
    QString m_filterKey;
    CGRect m_displayFrame = CGRectZero;
    CGDirectDisplayID m_displayId = 0;
    qreal m_scale = 1.0;
    int m_hidden = 0;
    QString m_error;
    CGColorRef m_background = nullptr;
    QList<CGColorRef> m_oldBackgrounds;
    std::shared_ptr<std::atomic<bool>> m_pending = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<SinkStats> m_sinkStats = std::make_shared<SinkStats>();
    int m_shown = 0;
    int m_regionCalls = 0;
    int m_reconfigs = 0;
    double m_reconfigMaxMs = 0;
    int m_filterUpdates = 0;
    int m_starts = 0;
};

NSWindow *nativeWindow(QWidget *widget)
{
    if (!widget) return nil;
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    return view ? view.window : nil;
}

} // namespace

std::unique_ptr<Session> start(const QString &windowTitle, const QRect &region, const Options &options, QString *error)
{
    if (!CGPreflightScreenCaptureAccess()) {
        if (error) *error = QStringLiteral("screen recording permission");
        return nullptr;
    }
    auto session = std::make_unique<SessionMac>();
    if (!session->open(windowTitle, region, options, error)) {
        return nullptr;
    }
    return session;
}

QRect captureRegion(QWidget *frame, const QMargins &margins)
{
    return frame ? frame->geometry().marginsRemoved(margins) : QRect();
}

QSize pixelSize(QWidget *frame, const QRect &region)
{
    const qreal dpr = frame ? frame->devicePixelRatioF() : 1.0;
    return QSize(qRound(region.width() * dpr), qRound(region.height() * dpr));
}

namespace {

// Que la app pueda poner el cursor aunque no este activa: macOS lo ignora si no. Propiedad privada de la
// conexion con el WindowServer (la usan otras apps de overlay); se busca en tiempo de ejecucion y, si no
// existe, el marco anda igual, con la flecha.
void setsCursorInBackground(bool on)
{
    using ConnectionFn = int (*)();
    using SetPropertyFn = int (*)(int, int, CFStringRef, CFTypeRef);
    static const auto connection = reinterpret_cast<ConnectionFn>(dlsym(RTLD_DEFAULT, "CGSMainConnectionID"));
    static const auto setProperty = reinterpret_cast<SetPropertyFn>(dlsym(RTLD_DEFAULT, "CGSSetConnectionProperty"));
    static bool current = false;
    if (!connection || !setProperty || current == on) return;
    current = on;
    const int cid = connection();
    setProperty(cid, cid, CFSTR("SetsCursorInBackground"), on ? kCFBooleanTrue : kCFBooleanFalse);
}

// Las flechas de estirar ventanas del sistema no son publicas: las mismas que usa Qt (QCocoaCursor).
NSCursor *systemResizeCursor(NSString *name, NSCursor *fallback)
{
    SEL selector = NSSelectorFromString(name);
    if ([NSCursor respondsToSelector:selector]) {
        if (NSCursor *cursor = ((NSCursor * (*)(id, SEL))objc_msgSend)([NSCursor class], selector)) return cursor;
    }
    return fallback;
}

NSCursor *nsCursor(Qt::CursorShape shape)
{
    switch (shape) {
    case Qt::SizeHorCursor: return systemResizeCursor(@"_windowResizeEastWestCursor", [NSCursor resizeLeftRightCursor]);
    case Qt::SizeVerCursor: return systemResizeCursor(@"_windowResizeNorthSouthCursor", [NSCursor resizeUpDownCursor]);
    case Qt::SizeFDiagCursor: return systemResizeCursor(@"_windowResizeNorthWestSouthEastCursor", [NSCursor crosshairCursor]);
    case Qt::SizeBDiagCursor: return systemResizeCursor(@"_windowResizeNorthEastSouthWestCursor", [NSCursor crosshairCursor]);
    case Qt::SizeAllCursor: return [NSCursor openHandCursor];
    case Qt::ClosedHandCursor: return [NSCursor closedHandCursor];
    default: return [NSCursor arrowCursor];
    }
}

} // namespace

void prepareFrame(QWidget *frame)
{
    excludeFromCapture(frame);
    NSWindow *ns = nativeWindow(frame);
    if (!ns) return;
    // Un click en el marco no activa la app (no le saca el foco a la reunion ni a lo que se esta usando). El
    // estilo NonactivatingPanel no se puede cambiar despues de crear la ventana: AppKit lo aplica con este metodo.
    if ([ns isKindOfClass:[NSPanel class]]) ns.styleMask |= NSWindowStyleMaskNonactivatingPanel;
    SEL prevents = NSSelectorFromString(@"_setPreventsActivation:");
    if ([ns respondsToSelector:prevents]) ((void (*)(id, SEL, BOOL))objc_msgSend)(ns, prevents, YES);
    // Qt::Tool se esconde cuando la app deja de estar activa; el marco se queda. Aca y no con
    // WA_MacAlwaysShowToolWindow, que hace que Qt traiga al frente las ventanas de la app al (des)activarse.
    ns.hidesOnDeactivate = NO;
    // En todos los escritorios (Spaces): el marco sigue a la region aunque se cambie de escritorio.
    ns.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces;
}

namespace {

// Lo que paso con el mouse sobre el marco, para el log.
struct FrameMouseStats
{
    int passThrough = 0;   // veces que la ventana paso a dejar pasar el mouse (entro a la region)
    int takeBack = 0;      // veces que lo volvio a tomar (borde o barrita)
    int clicksFrame = 0;   // clicks que recibio el marco en el borde o la barrita
    int clicksInside = 0;  // clicks adentro de la region que igual recibio el marco (no deberia pasar)
    double handlerMaxMs = 0;
};
FrameMouseStats g_mouseStats;
// La decision del marco abierto (hay uno solo a la vez); nil sin marco. Con evento nil decide con el mouse donde esta.
void (^g_frameUpdate)(NSEvent *) = nil;

} // namespace

void trackFrameMouse(QWidget *frame, std::function<bool(const QPoint &pos)> onMove)
{
    NSView *strongView = frame ? (__bridge NSView *)reinterpret_cast<void *>(frame->winId()) : nil;
    if (!strongView || !onMove) return;
    __weak NSView *weakView = strongView;
    // Donde esta el mouse ahora, en coordenadas del marco; (-1, -1) fuera de el.
    QPoint (^locate)(void) = ^QPoint {
        NSView *view = weakView;
        if (!view || !view.window) return QPoint(-1, -1);
        NSPoint point = [view convertPoint:[view.window convertPointFromScreen:[NSEvent mouseLocation]] fromView:nil];
        if (!view.isFlipped) point.y = view.bounds.size.height - point.y;
        if (!NSPointInRect(point, view.bounds)) return QPoint(-1, -1);
        return QPoint(int(std::floor(point.x)), int(std::floor(point.y)));
    };
    // Elige el cursor y decide si la ventana toma el mouse. Nunca cambia con un boton apretado: un arrastre
    // (propio o de la app de abajo) termina donde empezo.
    void (^update)(NSEvent *) = ^(NSEvent *event) {
        NSView *view = weakView;
        if (!view || !view.window) return;
        if (event && event.type == NSEventTypeLeftMouseUp && event.window == view.window) {
            // Al soltar un arrastre del propio marco, este monitor corre antes que mouseReleaseEvent (el marco
            // todavia se cree arrastrando): se decide despues, con el arrastre ya terminado.
            dispatch_async(dispatch_get_main_queue(), ^{
                if (g_frameUpdate) g_frameUpdate(nil);
            });
            return;
        }
        if ((!event || event.type != NSEventTypeLeftMouseUp) && NSEvent.pressedMouseButtons != 0) return;
        QElapsedTimer clock;
        clock.start();
        const bool wants = onMove(locate());
        NSWindow *window = view.window;
        if (window.ignoresMouseEvents == wants) {
            window.ignoresMouseEvents = !wants;
            ++(wants ? g_mouseStats.takeBack : g_mouseStats.passThrough);
        }
        g_mouseStats.handlerMaxMs = std::max(g_mouseStats.handlerMaxMs, clock.nsecsElapsed() / 1e6);
    };
    const NSEventMask moves = NSEventMaskMouseMoved | NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp;
    id global = [NSEvent addGlobalMonitorForEventsMatchingMask:moves handler:update];
    id local = [NSEvent addLocalMonitorForEventsMatchingMask:moves | NSEventMaskLeftMouseDown
                                                     handler:^NSEvent *(NSEvent *event) {
                                                         NSView *view = weakView;
                                                         if (event.type == NSEventTypeLeftMouseDown) {
                                                             if (view && event.window == view.window) {
                                                                 const QPoint pos = locate();
                                                                 if (onMove(pos)) {
                                                                     ++g_mouseStats.clicksFrame;
                                                                 } else if (++g_mouseStats.clicksInside <= 5) {
                                                                     qWarning().noquote() << "[meetFrame] Un click adentro de la region lo recibio el marco en"
                                                                                          << pos.x() << pos.y() << "(tendria que pasar a la app de abajo)";
                                                                 }
                                                             }
                                                             return event;
                                                         }
                                                         update(event);
                                                         return event;
                                                     }];
    setsCursorInBackground(true);
    g_mouseStats = FrameMouseStats();
    g_frameUpdate = update;
    QObject::connect(frame, &QObject::destroyed, [global, local]() {
        g_frameUpdate = nil;
        [NSEvent removeMonitor:global];
        [NSEvent removeMonitor:local];
        [[NSCursor arrowCursor] set]; // antes de apagar la propiedad: si no, con la app inactiva se ignora
        setsCursorInBackground(false);
    });
}

void syncFrameMouse(QWidget *)
{
    if (g_frameUpdate) g_frameUpdate(nil);
}

QString takeFrameMouseStats()
{
    const FrameMouseStats s = g_mouseStats;
    g_mouseStats = FrameMouseStats();
    return QStringLiteral("mouse: dejo pasar %1 veces, lo tomo %2, clicks en borde o barrita %3, clicks adentro tomados %4, "
                          "atender un movimiento max %5 ms")
        .arg(s.passThrough).arg(s.takeBack).arg(s.clicksFrame).arg(s.clicksInside).arg(s.handlerMaxMs, 0, 'f', 2);
}

void setFrameCursor(QWidget *, Qt::CursorShape shape)
{
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) return; // pruebas sin pantalla
    [nsCursor(shape) set];
}

void excludeFromCapture(QWidget *window)
{
    if (NSWindow *ns = nativeWindow(window)) {
        ns.sharingType = NSWindowSharingNone;
    }
}

bool hasCapturePermission()
{
    return CGPreflightScreenCaptureAccess();
}

void requestCapturePermission()
{
    // La primera vez macOS muestra su cartel; despues solo se puede dar en Ajustes del Sistema.
    if (!CGRequestScreenCaptureAccess()) {
        [[NSWorkspace sharedWorkspace]
            openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
    }
}

QList<RunningApp> runningApps()
{
    QList<RunningApp> list;
    QSet<QString> seen;
    const pid_t self = getpid();
    for (NSRunningApplication *app in [NSWorkspace sharedWorkspace].runningApplications) {
        if (app.activationPolicy != NSApplicationActivationPolicyRegular || app.processIdentifier == self || !app.bundleIdentifier) continue;
        RunningApp entry;
        entry.id = lower(app.bundleIdentifier);
        if (seen.contains(entry.id)) continue;
        seen.insert(entry.id);
        entry.name = app.localizedName ? QString::fromNSString(app.localizedName) : entry.id;
        entry.path = app.bundleURL ? QString::fromNSString(app.bundleURL.path) : QString();
        list.append(entry);
    }
    std::sort(list.begin(), list.end(), [](const RunningApp &a, const RunningApp &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    return list;
}

QString appIdForPath(const QString &path)
{
    NSBundle *bundle = [NSBundle bundleWithPath:path.toNSString()];
    return bundle ? lower(bundle.bundleIdentifier) : QString();
}

QList<MeetFrameRules::WindowInfo> listWindows(const QRect &)
{
    return {};
}

} // namespace MeetFrameSystem
