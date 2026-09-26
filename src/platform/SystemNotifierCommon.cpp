#include "platform/SystemNotifier.h"

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

QString SystemNotifier::escapeForScript(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
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
