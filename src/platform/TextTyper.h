#ifndef MIGHTYTOOLS_TEXTTYPER_H
#define MIGHTYTOOLS_TEXTTYPER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

// Escribe el texto del dictado en la ventana de otra app como si se tipeara, SIN el portapapeles (regla de
// Lega: ni el historial de Win+V ni lo último copiado). Una implementación por plataforma:
//  - Windows (platform/win/TextTyperWin.cpp): SendInput con KEYEVENTF_UNICODE, que no depende de la
//    distribución del teclado. Un emoji (par sustituto) va entero en el mismo lote. Un salto de línea va como
//    Shift+Enter: en los chats (Telegram, Spark) Enter solo manda el mensaje. Lotes chicos con una pausa entre
//    lotes, para no saturar apps lentas.
//
// Cada tecla lleva la marca propia (InputOrigin.h): los ganchos de la app no la toman como del usuario. En una
// corrida automatizada no se manda nada: type() solo anota en el log y termina con LoggedOnly.
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
        Failed,       ///< Windows no tomó las teclas (pantalla de bloqueo, aviso de UAC)
    };
    Q_ENUM(Result)

    explicit TextTyper(QObject *parent = nullptr);
    ~TextTyper() override;

    // La ventana del frente ahora (la de nivel superior). Se toma al empezar a grabar y se pasa a type().
    static quintptr foregroundWindow();

    // Escribe `text` en `target`. Antes espera hasta 1 s a que se suelten los modificadores del atajo; si siguen
    // apretados, los suelta (con la tecla muda, para que soltar Win no abra Inicio). Antes de cada lote mira que
    // `target` siga al frente. El resultado llega por finished(), siempre después de volver. False si ya está
    // escribiendo otro texto.
    bool type(quintptr target, const QString &text);
    bool isBusy() const;

    // Las teclas que se mandarían, una línea por evento y "--" entre lotes. No manda nada.
    static QStringList describe(const QString &text);

signals:
    // `typed`: cuánto del texto (en unidades de QString) llegó a escribirse; con FocusChanged a la mitad, el
    // resto es text.mid(typed).
    void finished(TextTyper::Result result, int typed);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif // MIGHTYTOOLS_TEXTTYPER_H
