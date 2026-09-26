#include "app/TrayMenu.h"

#include "app/ModuleHost.h"

#include <QAction>
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
    return QStringLiteral("Mighty Tools · %1 of %2 on").arg(host->runningCount()).arg(host->descriptors().size());
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

    a.settings = menu->addAction(QStringLiteral("Settings..."));
    a.updates = menu->addAction(QStringLiteral("Check for Updates..."));
#ifndef Q_OS_WIN
    a.updates->setVisible(false);
#endif
    menu->addSeparator();
    a.quit = menu->addAction(QStringLiteral("Quit"));
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
