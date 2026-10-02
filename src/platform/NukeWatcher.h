#ifndef MIGHTYTOOLS_NUKEWATCHER_H
#define MIGHTYTOOLS_NUKEWATCHER_H

#include <QDateTime>
#include <QObject>
#include <QPoint>
#include <QRect>

class ForegroundWatcher;

// Sabe si Nuke esta al frente y donde esta su ventana principal. No observa el sistema por su
// cuenta: escucha el ForegroundWatcher compartido del host (plan 4.4, un solo hook para todas las
// herramientas) y decide por el nombre del ejecutable (NukeWatcherCommon.cpp). Lo que queda por
// plataforma son las consultas puntuales:
//  - Windows (platform/win/NukeWatcherWin.cpp): la ventana del frente AHORA y el marco de la ventana
//    principal de Nuke (DWM).
//  - macOS   (platform/mac/NukeWatcherMac.mm): la app activa AHORA (NSWorkspace) y el API de
//    Accesibilidad para el marco de la ventana principal.
//
// Nuke se reconoce por el PROCESO, nunca por la clase de ventana: la version AutoHotkey miraba
// `Qt5QWindowIcon`, que deja de existir con Nuke 16 (Qt 6).
//
// Coordenadas NATIVAS de pantalla, las mismas que usa InputInjector: pixeles fisicos en Windows
// (la app es per-monitor DPI aware) y puntos con origen arriba a la izquierda en macOS. Nunca se
// mezclan con las coordenadas logicas de Qt.
class NukeWatcher : public QObject
{
    Q_OBJECT

public:
    // `foreground` es el servicio del host (ModuleContext::foreground()); vive mas que este objeto.
    explicit NukeWatcher(ForegroundWatcher *foreground, QObject *parent = nullptr);
    ~NukeWatcher() override = default;

    // Lo ultimo que aviso el servicio (llega encolado, unos milisegundos despues del cambio).
    bool nukeInFront() const { return m_nukeInFront; }
    // Pregunta AHORA cual es la ventana del frente, sin esperar el aviso. Lo usa el atajo antes de
    // actuar: si el usuario acaba de salir de Nuke, el aviso todavia puede no haber llegado.
    bool isNukeInFrontNow() const;

    // El proceso de Nuke que esta al frente AHORA: pid y hora de arranque (UTC). pid 0 si el frente
    // no es Nuke. La hora sirve para no confundir un pid reciclado con el Nuke que lo uso antes.
    struct FrontProcess {
        qint64 pid = 0;
        QDateTime started;
    };
    FrontProcess frontNukeProcess() const;

    // Marco de la ventana principal de Nuke que esta al frente. Vacio si Nuke no esta al frente.
    QRect frontNukeFrame() const;
    // Marco de la ventana principal de Nuke que contiene `nativePoint`. Vacio si en ese punto no
    // hay una ventana de Nuke. Lo usa el calibrador despues del click.
    QRect nukeFrameAt(const QPoint &nativePoint) const;

    // Nombre del ejecutable de un proceso de Nuke: "Nuke15.1.exe", "Nuke16.0.exe" (Windows) o
    // "Nuke15.1" (macOS). NukeX y Nuke Studio son el mismo ejecutable con otro argumento.
    static bool isNukeExecutable(const QString &fileName);

signals:
    void nukeInFrontChanged(bool inFront);
    // Paso directo de una ventana de un Nuke a la de otro proceso de Nuke.
    void frontNukeSwitched();

private:
    void setNukeInFront(bool inFront);

    bool m_nukeInFront = false;
    quint32 m_frontPid = 0;
};

#endif // MIGHTYTOOLS_NUKEWATCHER_H
