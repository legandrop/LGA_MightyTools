#ifndef MIGHTYTOOLS_UPDATEMANIFEST_H
#define MIGHTYTOOLS_UPDATEMANIFEST_H

#include <QByteArray>
#include <QString>
#include <QUrl>

// Lectura del manifiesto de LGA_Updates (versions.json, schema 1) para esta app. Qt puro: lo usa
// UpdateService y lo prueba el --self-test con JSON de ejemplo, en cualquier plataforma.
//
// Cada plataforma tiene su paquete, y los dos viven en el mismo repo de releases:
//   Windows  LGA_MightyTools_Setup_v<version>.exe   (instalador de Inno)
//   macOS    LGA_MightyTools_Mac_v<version>.zip     (el bundle, hecho con ditto)
//
// El paquete se busca primero en `assetLatest`, que trae el ultimo de CADA familia de nombre con
// su propio tag y digest: asi un release que todavia tiene solo la otra plataforma no esconde el
// ultimo paquete de esta. Sin `assetLatest` (manifiesto viejo) se cae a `tag` + `assets`.
namespace UpdateManifest {

enum class Platform { Windows, Mac };

Platform currentPlatform();

struct ReleaseInfo {
    // Version sin la "v". Vacia: el manifiesto no se pudo leer o el repo no tiene nada publicado
    // para esta plataforma (no es un error: es "no hay update").
    QString version;
    // Tag del release que trae el paquete, tal cual (con la "v").
    QString tag;
    // Vacio: hay un release con esa version pero sin paquete para esta plataforma.
    QString assetName;
    // SHA-256 en hexadecimal minuscula; vacio si el manifiesto no trae uno valido.
    QString assetDigest;
};

ReleaseInfo parse(const QByteArray &payload, Platform platform);

// URL de prueba escrita en un flag de debug: solo vale si es http y apunta a ESTA maquina
// (127.0.0.1 o localhost). Cualquier otra cosa devuelve una URL invalida: un archivo de flags no
// puede mandar el updater a otro servidor.
QUrl loopbackUrl(const QString &text);

// "sha256:<64 hex>" o los 64 hex pelados -> 64 hex en minuscula. Cualquier otra cosa, vacio.
QString normalizedSha256Digest(QString digest);

} // namespace UpdateManifest

#endif // MIGHTYTOOLS_UPDATEMANIFEST_H
