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
// ventanita no parpadea. La primera palabra del dictado va siempre con mayuscula.
Split stabilize(const QString &confirmed, const QString &previous, const QString &next);

// Una palabra para comparar: minusculas, sin puntuacion ni signos ("¿Qué?" -> "qué").
QString normalizedWord(const QString &word);

// Una pasada en vivo con poco audio suele devolver el contexto que se le dio y no lo que se dijo: el
// motor le pasa el vocabulario a Whisper como "Glossary: Nuke Studio, ...", y salen "Glossary" o
// "Glossary, Nuke, Nuke, Nuke". True si todas las palabras de `hypothesis` estan en ese contexto (o si
// no tiene ninguna); la ultima puede venir cortada ("... Nuke... Nu"). Solo para las pasadas en vivo: un
// final de verdad puede ser "Nuke Studio" o "el roto anda bien" (todas palabras de la frase de contexto).
bool echoesContext(const QString &hypothesis, const QStringList &vocabulary, const QString &prompt);

// La version para la pasada final, mas estricta: el contexto repetido y no algo que se pudo decir. True si
// dice "Glossary", si son solo terminos del vocabulario con alguno repetido ("Nuke, Nuke, Nuke"), o si son
// `minPromptRun` palabras o mas seguidas de la frase de contexto (Whisper la copia sobre audio sin voz: 8
// con voz de sobra; 4 cuando hubo menos de ~1 s de voz, porque "Fijate si el roto anda bien." sale de un
// click).
bool finalEchoesContext(const QString &hypothesis, const QStringList &vocabulary, const QString &prompt, int minPromptRun = 8);

// Tiene alguna letra o algun digito: un final que es solo "." o "¡!" no se escribe.
bool hasWords(const QString &text);

// Whisper a veces devuelve en ingles (traducido) lo que se dijo en castellano, aunque se le pida "es". True si
// el texto tiene al menos 3 palabras funcionales inglesas ("the", "and", "you"...) y mas del doble que
// castellanas ("de", "que", "la"...). Los terminos tecnicos ("Nuke Studio", "comp") no cuentan.
bool looksEnglish(const QString &text);

// Tabla de reemplazos (pares "si escribe X, poner Y"), en orden, sobre palabras o frases completas y sin
// distinguir mayusculas: {"Jairo", "Hiero"} cambia "jairo" pero no "Jairos". Y se pone tal cual.
using Replacements = QList<QPair<QString, QString>>;
QString applyReplacements(const QString &text, const Replacements &replacements);

// Saca los signos de apertura ¿ y ¡ (opcion «Only closing marks»): quedan solo los de cierre ? y !.
QString stripOpeningMarks(const QString &text);

} // namespace DictationText

#endif // MIGHTYTOOLS_DICTATIONTEXT_H
