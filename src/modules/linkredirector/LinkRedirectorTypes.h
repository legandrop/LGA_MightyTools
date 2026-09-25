#ifndef MIGHTYTOOLS_LINKREDIRECTOR_TYPES_H
#define MIGHTYTOOLS_LINKREDIRECTOR_TYPES_H

#include <QString>

// Un navegador instalado, detectado por la plataforma (BrowserDetection).
struct DetectedBrowser
{
    QString name;      ///< nombre visible ("Google Chrome", "Firefox")
    QString exePath;    ///< ruta absoluta al ejecutable real (Windows) o al binario dentro del .app (mac)
    QString handlerId; ///< identificador del handler de http: ProgId (Windows) o bundle id (mac)
};

#endif // MIGHTYTOOLS_LINKREDIRECTOR_TYPES_H
