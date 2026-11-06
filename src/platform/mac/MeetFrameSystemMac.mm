#include "platform/MeetFrameSystem.h"

#include "platform/AppToggleSystem.h"

#include <QCloseEvent>
#include <QDebug>
#include <QDir>
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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <unistd.h>

// Meet Frame en macOS. ScreenCaptureKit captura la region del monitor con un filtro que deja afuera lo que se
// tapa: el filtro incluye las apps que corren MENOS las tapadas, el Centro de notificaciones y esta misma app (el
// marco y la ventana compartida nunca salen), y exceptua las ventanas del escritorio (nivel negativo: fondo e
// iconos) y las del navegador cuyo titulo coincide. El filtro se rearma cada segundo con las apps que corren:
// una app que se abre entre medio NO sale hasta el proximo rearmado (tapa de mas, nunca de menos).
//
// La ventana compartida es una ventana Qt comun («Meet Frame») que dibuja el ultimo cuadro; se la manda al
// fondo. Lo tapado no se pinta de un color: simplemente no esta, y se ve lo de abajo o el color de fondo.

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
    }
    void setFrame(const QImage &image)
    {
        m_image = image;
        update();
    }
    void setFill(const QColor &fill) { m_fill = fill; }
    std::function<void()> onClose;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_fill);
        if (!m_image.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawImage(QRectF(rect()), m_image);
        }
    }
    void closeEvent(QCloseEvent *event) override
    {
        event->ignore();
        if (onClose) onClose();
    }

private:
    QImage m_image;
    QColor m_fill = Qt::black;
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
        if (m_stream && CGRectContainsRect(m_displayFrame, next)) {
            // Mismo monitor: solo cambia el recorte. El filtro (que apps salen) sigue con su refresco propio.
            [m_stream updateConfiguration:configuration() completionHandler:nil];
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
            [m_stream updateConfiguration:configuration() completionHandler:nil];
        }
        refreshContent();
    }

    void setIgnoredWindows(const QList<QWidget *> &) override {} // las ventanas propias ya estan fuera del filtro
    int hiddenWindows() const override { return m_hidden; }
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
            [m_stream updateConfiguration:configuration() completionHandler:nil];
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
        SessionMac *raw = this;
        sink.onFrame = ^(CVPixelBufferRef buffer) {
            // Un cuadro por vez: si el hilo principal todavia no dibujo el anterior, este se descarta.
            if (!*alive || pending->exchange(true)) return;
            CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            const int width = int(CVPixelBufferGetWidth(buffer));
            const int height = int(CVPixelBufferGetHeight(buffer));
            const int stride = int(CVPixelBufferGetBytesPerRow(buffer));
            QImage image;
            if (void *base = CVPixelBufferGetBaseAddress(buffer)) {
                image = QImage(static_cast<const uchar *>(base), width, height, stride, QImage::Format_RGB32).copy();
            }
            CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
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
        if (m_shared) static_cast<SharedWindow *>(m_shared.data())->setFrame(image);
    }

    std::shared_ptr<std::atomic<bool>> m_alive;
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

void prepareFrame(QWidget *frame)
{
    excludeFromCapture(frame);
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
