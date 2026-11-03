#ifndef MIGHTYTOOLS_DICTATIONTEXT_H
#define MIGHTYTOOLS_DICTATIONTEXT_H

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Funciones puras del texto del dictado (sin estado, con self-test).
namespace DictationText {

// Texto en vivo partido en lo que ya no cambia y lo que todavia puede cambiar.
struct Split
{
    QString confirmed;   ///< nunca se retira ni cambia: la ventanita lo pinta fijo
    QString provisional; ///< la cola de la ultima pasada, sin los "..." finales
    QString display() const;
};

// De dos pasadas seguidas (`previous`, `next`), las palabras del principio que se repiten (comparadas sin
// mayusculas ni puntuacion) pasan a confirmadas. `confirmed` es lo confirmado hasta ahora (el
// `confirmed` del Split anterior; vacio al empezar): esas palabras quedan tal cual aunque la pasada
// nueva las escriba con otra mayuscula u otro punto, y las nuevas confirmadas se toman de `next`.
// Entre pasadas Whisper cambia mayusculas, puntos y "..." finales con las mismas palabras: asi la
// ventanita no parpadea.
Split stabilize(const QString &confirmed, const QString &previous, const QString &next);

// Una palabra para comparar: minusculas, sin puntuacion ni signos ("¿Qué?" -> "qué").
QString normalizedWord(const QString &word);

// Una pasada en vivo con poco audio suele devolver el contexto que se le dio y no lo que se dijo: el
// motor le pasa el vocabulario a Whisper como "Glossary: Nuke Studio, ...", y salen "Glossary" o
// "Glossary, Nuke, Nuke, Nuke". True si todas las palabras de `hypothesis` estan en ese contexto (o si
// no tiene ninguna). Solo para las pasadas en vivo: un final de verdad puede ser "Nuke Studio".
bool echoesContext(const QString &hypothesis, const QStringList &vocabulary, const QString &prompt);

// Tabla de reemplazos (pares "si escribe X, poner Y"), en orden, sobre palabras o frases completas y sin
// distinguir mayusculas: {"Jairo", "Hiero"} cambia "jairo" pero no "Jairos". Y se pone tal cual.
using Replacements = QList<QPair<QString, QString>>;
QString applyReplacements(const QString &text, const Replacements &replacements);

} // namespace DictationText

#endif // MIGHTYTOOLS_DICTATIONTEXT_H
