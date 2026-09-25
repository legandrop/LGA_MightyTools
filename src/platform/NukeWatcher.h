#ifndef MIGHTYTOOLS_NUKEWATCHER_H
#define MIGHTYTOOLS_NUKEWATCHER_H

#include <QObject>
#include <QPoint>
#include <QRect>

#include <memory>

// Sabe si Nuke esta al frente y donde esta su ventana principal. Una implementacion por plataforma:
//  - Windows (platform/win/NukeWatcherWin.cpp): SetWinEventHook(EVENT_SYSTEM_FOREGROUND) y el
//    nombre del exe del proceso de la ventana.
//  - macOS   (platform/mac/NukeWatcherMac.mm): NSWorkspace (app activa) y el API de Accesibilidad
//    para el marco de la ventana principal.
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
    explicit NukeWatcher(QObject *parent = nullptr);
    ~NukeWatcher() override;

    // Lo ultimo que aviso el sistema (llega encolado, unos milisegundos despues del cambio).
    bool nukeInFront() const { return m_nukeInFront; }
    // Pregunta AHORA cual es la ventana del frente, sin esperar el aviso. Lo usa el atajo antes de
    // actuar: si el usuario acaba de salir de Nuke, el aviso todavia puede no haber llegado.
    bool isNukeInFrontNow() const;

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

private:
    void setNukeInFront(bool inFront);

    struct Private;
    std::unique_ptr<Private> d;
    bool m_nukeInFront = false;
};

#endif // MIGHTYTOOLS_NUKEWATCHER_H
