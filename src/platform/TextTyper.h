#ifndef MIGHTYTOOLS_TEXTTYPER_H
#define MIGHTYTOOLS_TEXTTYPER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

// Pone el texto del dictado en la ventana de otra app. Una implementación por plataforma:
//  - Windows (platform/win/TextTyperWin.cpp): «pegado sin historial» (decisión de Lega, 2026-11-03). Tipear con
//    SendInput + KEYEVENTF_UNICODE fallaba en el Bloc de notas de Windows 11: atrasado, toma el carácter de una
//    tecla al procesarla y repetía la letra siguiente («que» → «uue»), con cualquier pausa. Ahora: se guarda lo
//    que hay en el portapapeles (todos los formatos copiables), se pone el texto con render diferido y las marcas
//    que lo dejan fuera del historial de Win+V, de la nube y de los programas que vigilan el portapapeles, se
//    manda Ctrl+V y, cuando la ventana ya leyó el texto, se devuelve lo de antes (también sin historial). Si
//    otra app copió algo mientras tanto, no se pisa. Los saltos de línea van como \r\n.
//  - macOS (platform/mac/TextTyperMac.mm): eventos de teclado con el texto adentro, de a pedazos, sin el
//    portapapeles. El destino es la app del frente (su PID). Pide el permiso de Accesibilidad.
//
// Las teclas llevan la marca propia (InputOrigin.h): los ganchos de la app no las toman como del usuario. En una
// corrida automatizada no se toca nada: type() solo anota en el log y termina con LoggedOnly.
class TextTyper : public QObject
{
    Q_OBJECT

public:
    enum class Result {
        Typed,        ///< escrito entero
        LoggedOnly,   ///< corrida automatizada: solo se anotó
        FocusChanged, ///< el frente ya no es la ventana de destino: no se escribió, o se cortó a la mitad
        Elevated,     ///< la ventana corre como administrador y esta app no (UIPI): no se escribió nada
        NoTarget,     ///< la ventana ya no existe (o nunca hubo una)
        Failed,       ///< no se pegó: portapapeles ocupado, teclas rechazadas o la ventana nunca pidió el texto
        Stopped,      ///< stop(): el usuario lo cortó con Esc (en Windows antes de pegar; en mac puede ser a la mitad)
        NoPermission, ///< macOS: falta el permiso de Accesibilidad; no se escribió nada
    };
    Q_ENUM(Result)

    explicit TextTyper(QObject *parent = nullptr);
    ~TextTyper() override;

    // La ventana del frente ahora (la de nivel superior). Se toma al empezar a grabar y se pasa a type().
    static quintptr foregroundWindow();

    // Pega `text` en `target`. Antes mira que `target` siga al frente y espera hasta 1 s a que se suelten los
    // modificadores del atajo; si siguen apretados, los suelta (con la tecla muda, para que soltar Win no abra
    // Inicio). El resultado llega por finished(), siempre después de volver. False si ya está pegando otro texto.
    bool type(quintptr target, const QString &text);
    bool isBusy() const;
    // Corta antes del Ctrl+V: finished(Stopped, 0) llega después de volver. Ya pegado, no hace nada (falta solo
    // devolver lo que había en el portapapeles).
    void stop();

    // Lo que se haría: el texto como se pega, el formato, las marcas y las teclas. No toca nada.
    static QStringList describe(const QString &text);

    struct Private; // público solo para el procedimiento de la ventana de Win32 (TextTyperWin.cpp)

signals:
    // `typed`: cuánto del texto (en unidades de QString) se pegó: todo o nada.
    void finished(TextTyper::Result result, int typed);

private:
    std::unique_ptr<Private> d;
};

#endif // MIGHTYTOOLS_TEXTTYPER_H
