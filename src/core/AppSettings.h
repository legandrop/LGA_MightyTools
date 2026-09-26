#ifndef MIGHTYTOOLS_APPSETTINGS_H
#define MIGHTYTOOLS_APPSETTINGS_H

#include <QSettings>
#include <QString>

#include <memory>

// Donde vive la configuracion: settings.ini en la carpeta de la app dentro de AppData, igual que
// las otras apps LGA.
//  - Windows: %APPDATA%\LGA\LGA_MightyTools\settings.ini (el desinstalador borra la carpeta).
//  - macOS:   ~/Library/Application Support/LGA/LGA_MightyTools/settings.ini (nunca adentro
//             del .app: escribir en el bundle invalida la firma).
//
// Las corridas automatizadas (--self-test, --simulate-action, --ui-shot, --ui-probe, la medicion)
// llaman a useMemoryOnly() al arrancar: desde ahi open() devuelve un QSettings que empieza vacio y no
// lee ni escribe ningun archivo, asi una prueba nunca ve ni toca el settings.ini del usuario.
// --simulate-action acepta --settings-file <ruta> para leer un archivo elegido a proposito.
namespace AppSettings {

QString filePath();
// Para leer. No crea la carpeta: leer un settings.ini que todavia no existe no deja nada en disco.
std::unique_ptr<QSettings> open();
// Para escribir: crea la carpeta si falta.
std::unique_ptr<QSettings> openForWrite();

// Fuente de las corridas automatizadas (ver arriba). Se llaman antes de cualquier open().
void useMemoryOnly();
void useFile(const QString &path);
bool memoryOnly();

} // namespace AppSettings

#endif // MIGHTYTOOLS_APPSETTINGS_H
