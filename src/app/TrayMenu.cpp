#include "app/TrayMenu.h"
#include "core/I18n.h"

#include "app/ModuleHost.h"

#include <QAction>
#include <QIconEngine>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QWidgetAction>

namespace {

// Titulo de la seccion de una herramienta (`.mi.sec` del canvas): 22 de alto, 11 px, mayusculas.
QAction *sectionTitle(QMenu *menu, const QString &title)
{
#ifdef Q_OS_MACOS
    QAction *action = menu->addAction(title);
    action->setEnabled(false);
    return action;
#else
    auto *action = new QWidgetAction(menu);
    auto *label = new QLabel(title.toUpper());
    label->setObjectName(QStringLiteral("traySection"));
    label->ensurePolished();
    QFont spaced = label->font();
    spaced.setLetterSpacing(QFont::AbsoluteSpacing, 0.66); // .06em a 11 px
    label->setFont(spaced);
    action->setDefaultWidget(label);
    action->setEnabled(false);
    menu->addAction(action);
    return action;
#endif
}

} // namespace

QString trayHeaderText(const ModuleHost *host)
{
    return I18n::tr("Mighty Tools · %1 of %2 on").arg(host->runningCount()).arg(host->descriptors().size());
}

QString trayTooltip(const ModuleHost *host)
{
    QStringList lines{QStringLiteral("LGA Mighty Tools")};
    lines += host->trayTooltipLines();
    return lines.join(QLatin1Char('\n'));
}

TrayMenuActions fillTrayMenu(QMenu *menu, ModuleHost *host)
{
    // Estilo: bloque QMenu de Theme (paleta de LGA_Base_QT_C_Py/docs/Doc_MenuContextual.md).
    menu->clear();
#ifndef Q_OS_MACOS
    // El ancho del canvas (`.menu`: 252 con el borde). En mac el menu es nativo.
    menu->setFixedWidth(252);
#endif
    TrayMenuActions a;
    QAction *header = menu->addAction(trayHeaderText(host));
    header->setEnabled(false);
    menu->addSeparator();

    // Una seccion por herramienta prendida; una sin entradas no aparece. El orden es el del canvas
    // (seccion 4): primero lo que se aprieta (pausar, calibrar) y al final las lineas de disco bajo.
    // Una herramienta que no esta en la lista va despues, en el orden del registro.
    static const QStringList kCanvasOrder = {QStringLiteral("nukeShortcuts"), QStringLiteral("folderSwitch"),
                                             QStringLiteral("openInNukeX"), QStringLiteral("linkRedirector"),
                                             QStringLiteral("diskSpace")};
    QStringList ordered;
    for (const QString &id : kCanvasOrder) {
        if (host->isRunning(id)) {
            ordered.append(id);
        }
    }
    for (const QString &id : host->runningIds()) {
        if (!ordered.contains(id)) {
            ordered.append(id);
        }
    }
    for (const QString &id : ordered) {
        Module *module = host->module(id);
        QAction *title = sectionTitle(menu, host->descriptor(id)->title);
        const int before = int(menu->actions().size());
        module->fillTrayMenu(menu);
        if (int(menu->actions().size()) == before) {
            menu->removeAction(title);
            delete title;
        }
    }
    if (host->runningCount() > 0 && menu->actions().constLast()->isSeparator() == false) {
        menu->addSeparator();
    }

    a.settings = menu->addAction(I18n::tr("Settings..."));
    a.updates = menu->addAction(I18n::tr("Check for Updates..."));
#ifndef Q_OS_WIN
    a.updates->setVisible(false);
#endif
    menu->addSeparator();
    a.quit = menu->addAction(I18n::tr("Quit"));
    return a;
}

QIcon trayIcon(bool dimmed)
{
    // Un PNG por tamano (tools/icono/armar_tray.ps1): las planchas caen en pixeles enteros en cada uno
    // y QIcon elige el que corresponde a la escala de la pantalla. La version monocroma para la barra
    // de menu de macOS (modo template) sale del sistema de iconos LGA: pendiente en el roadmap.
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48}) {
        const QPixmap source(QStringLiteral(":/icons/tray/tray_%1.png").arg(size));
        if (!dimmed) {
            icon.addPixmap(source);
            continue;
        }
        QPixmap faded(source.size());
        faded.fill(Qt::transparent);
        QPainter painter(&faded);
        painter.setOpacity(0.4);
        painter.drawPixmap(0, 0, source);
        painter.end();
        icon.addPixmap(faded);
    }
    return icon;
}

namespace {

// Ver systemTrayIcon() en TrayMenu.h. QIcon::pixmap(size) con DPR > 1 llama a scaledPixmap(size * dpr,
// ..., dpr) y antes actualSize(size * dpr): dividir por la escala recupera el tamano que pidio Windows.
class NativeSizeIconEngine : public QIconEngine
{
public:
    explicit NativeSizeIconEngine(const QIcon &source) : m_source(source) {}

    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override { return size; }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override { return pick(size, mode, state); }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        const qreal s = scale > 0 ? scale : 1.0;
        return pick(QSize(qRound(size.width() / s), qRound(size.height() / s)), mode, state);
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        painter->drawPixmap(rect, pick(rect.size(), mode, state));
    }
    QIconEngine *clone() const override { return new NativeSizeIconEngine(m_source); }

private:
    // El PNG de ese tamano si existe (16, 20, 24, 32, 40, 48); si no, el mas cercano achicado.
    QPixmap pick(const QSize &size, QIcon::Mode mode, QIcon::State state) const
    {
        QPixmap pixmap = m_source.pixmap(size, 1.0, mode, state);
        if (pixmap.size() != size && !pixmap.isNull()) {
            pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        pixmap.setDevicePixelRatio(1.0);
        return pixmap;
    }

    QIcon m_source;
};

} // namespace

QIcon systemTrayIcon(bool dimmed)
{
#ifdef Q_OS_WIN
    return QIcon(new NativeSizeIconEngine(trayIcon(dimmed)));
#else
    // macOS: la barra de menu pide el tamano con su escala bien; no hace falta.
    return trayIcon(dimmed);
#endif
}
