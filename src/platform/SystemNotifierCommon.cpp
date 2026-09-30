#include "platform/SystemNotifier.h"

#include "core/AppSettings.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

QString SystemNotifier::iconTempPath(bool automatedRun)
{
    // Nombre propio: el temp es de todas las apps, y PipeSync usa el suyo.
    return QDir(QDir::tempPath())
        .absoluteFilePath(automatedRun ? QStringLiteral("LGA_MightyTools_notification_icon_qa.png")
                                       : QStringLiteral("LGA_MightyTools_notification_icon.png"));
}

QString SystemNotifier::stableIconPath()
{
    return QFileInfo(AppSettings::filePath()).absoluteDir().filePath(QStringLiteral("notification_icon.png"));
}

QString SystemNotifier::toastXml(const Notice &notice, const QString &imagePath)
{
    const auto esc = [](const QString &text) { return text.toHtmlEscaped(); };
    const bool hasChoice = !notice.choices.isEmpty();
    QString xml = QStringLiteral("<toast");
    if (!notice.launch.isEmpty()) {
        xml += QStringLiteral(" launch=\"%1\"").arg(esc(notice.launch));
    }
    // "reminder" sin un boton propio lo ignora Windows: solo con el desplegable.
    if (notice.persistent && hasChoice) {
        xml += QStringLiteral(" scenario=\"reminder\"");
    }
    xml += QStringLiteral("><visual><binding template=\"ToastGeneric\"><text>%1</text><text>%2</text>")
               .arg(esc(notice.title), esc(notice.body));
    if (!imagePath.isEmpty()) {
        xml += QStringLiteral("<image placement=\"appLogoOverride\" src=\"%1\"/>").arg(esc(imagePath));
    }
    xml += QStringLiteral("</binding></visual>");
    if (hasChoice) {
        xml += QStringLiteral("<actions><input id=\"%1\" type=\"selection\" title=\"%2\" defaultInput=\"%3\">")
                   .arg(choiceInputId(), esc(notice.choiceLabel), esc(notice.choiceDefault));
        for (const auto &choice : notice.choices) {
            xml += QStringLiteral("<selection id=\"%1\" content=\"%2\"/>").arg(esc(choice.first), esc(choice.second));
        }
        xml += QStringLiteral("</input><action content=\"%1\" arguments=\"%2\" activationType=\"foreground\"/>")
                   .arg(esc(notice.button), esc(notice.buttonArguments));
        xml += QStringLiteral("<action content=\"Dismiss\" arguments=\"dismiss\" activationType=\"system\"/></actions>");
    }
    xml += QStringLiteral("</toast>");
    return xml;
}

QString SystemNotifier::escapeForScript(const QString &text)
{
    // PowerShell tambien cierra una cadena entre comillas simples con las tipograficas (U+2018 a
    // U+201B): se duplican igual que la recta.
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar c : text) {
        escaped += c;
        if (c == QLatin1Char('\'') || (c.unicode() >= 0x2018 && c.unicode() <= 0x201B)) {
            escaped += c;
        }
    }
    return escaped;
}

SystemNotifier::IconFile SystemNotifier::prepareIcon(const QString &icoPath, const QString &pngPath)
{
    IconFile file;
    // Un PNG de menos de una hora se reusa (PipeSync): no se decodifica el .ico en cada aviso.
    const QFileInfo existing(pngPath);
    if (existing.exists() && existing.lastModified().secsTo(QDateTime::currentDateTime()) < 3600) {
        const QImage cached(pngPath);
        if (!cached.isNull()) {
            file.path = pngPath;
            file.size = cached.size();
            file.reused = true;
            return file;
        }
    }
    // El .ico crudo no sirve: el decoder de Windows toma el PRIMER frame (16x16) y lo estira. Se
    // elige a mano el frame mas grande.
    QImageReader reader(icoPath);
    const int frameCount = qMax(1, reader.imageCount());
    QImage best;
    for (int i = 0; i < frameCount; ++i) {
        if (i > 0 && !reader.jumpToImage(i)) {
            break;
        }
        const QImage frame = reader.read();
        if (!frame.isNull() && frame.width() > best.width()) {
            best = frame;
        }
    }
    file.frames = frameCount;
    if (best.isNull()) {
        // Causa tipica: falta el plugin imageformats\qico.dll. El toast sale igual, sin imagen.
        qWarning() << "[SystemNotifier] No se pudo decodificar ningun frame de" << icoPath << ":" << reader.errorString()
                   << "- el aviso sale sin icono";
        return file;
    }
    if (!best.save(pngPath, "PNG")) {
        qWarning() << "[SystemNotifier] No se pudo escribir el PNG temporal" << pngPath << "- el aviso sale sin icono";
        return file;
    }
    file.path = pngPath;
    file.size = best.size();
    return file;
}
