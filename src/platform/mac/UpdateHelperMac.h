#ifndef MIGHTYTOOLS_UPDATEHELPERMAC_H
#define MIGHTYTOOLS_UPDATEHELPERMAC_H

#include <QString>
#include <QStringList>

// Script auxiliar del updater de macOS: reemplaza el bundle instalado por el de un ZIP y vuelve a
// abrir la app. Corre como proceso aparte (bash) porque la app tiene que estar cerrada para
// reemplazarse. Se expone aparte de UpdateInstaller para que el --self-test lo corra de verdad
// sobre bundles de mentira dentro de su carpeta de pruebas.
//
// Codigos de salida del script:
//   0 actualizada · 2 argumentos invalidos · 3 la app no cerro · 4 destino fuera de la raiz
//   permitida (modo test) · 5 el ZIP no se pudo descomprimir · 6 el bundle no es el esperado
//   (identificador, version o forma) · 7 firma invalida o de otra identidad · 8 la carpeta de
//   instalacion no se pudo escribir · 1 interrumpida.
// En cualquier salida distinta de 0 el bundle instalado queda como estaba.
namespace MacUpdateHelper {

struct Job {
    QString zipPath;
    QString targetBundle; // el .app instalado
    QString bundleId;     // el que tienen que tener el instalado y el nuevo
    QString version;      // CFBundleShortVersionString que tiene que traer el nuevo
    qint64 waitPid = 0;   // proceso que tiene que terminar antes del reemplazo (0: no espera)
    QString logPath;
    // Modo test: no abre la app, no avisa, no registra nada en el sistema, y el script exige que
    // el destino, el ZIP y el log esten dentro de allowedRoot.
    bool testMode = false;
    QString allowedRoot;
    QString noticeTitle;
    QString noticeBody;
};

QString scriptText();

// Escribe el script en `dir` con un nombre unico. Devuelve su ruta, o vacio si no pudo.
QString writeScript(const QString &dir);

// Argumentos para `/bin/bash` (el primero es el script).
QStringList arguments(const QString &scriptPath, const Job &job);

// Guarda de las corridas automatizadas: ahi solo se acepta un trabajo en modo test, con otro
// identificador que el de la app real y con el destino, el ZIP y el log dentro de la carpeta de
// pruebas registrada (AutomatedRun::sandbox()). Fuera de una corrida automatizada, siempre true.
bool allowed(const Job &job, QString *why = nullptr);

} // namespace MacUpdateHelper

#endif // MIGHTYTOOLS_UPDATEHELPERMAC_H
