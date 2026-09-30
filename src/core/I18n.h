#ifndef MIGHTYTOOLS_I18N_H
#define MIGHTYTOOLS_I18N_H

#include <QHash>
#include <QString>

// Idioma de la interfaz: ingles (el de siempre, por defecto) o espanol.
//
// El texto en ingles del codigo ES la clave: I18n::tr("Check now") devuelve la traduccion si el
// idioma elegido es espanol y la tabla la tiene, y si no el mismo texto en ingles. Un texto que falta
// en la tabla queda en ingles, nunca vacio. La tabla vive en I18nSpanish.cpp.
//
// Reglas para los textos armados:
//  - Con datos, el placeholder va adentro de la clave: I18n::tr("%1 is low").arg(drive). Asi el
//    espanol puede cambiar el orden ("Poco espacio en %1").
//  - Nunca se arma una frase pegando traducciones sueltas: cada oracion es una clave.
//  - Plurales: una clave por forma ("1 tool", "%1 tools").
//  - El mismo texto en ingles con dos sentidos (una herramienta apagada y un atajo apagado dicen
//    "%1 is off"): I18n::trc("shortcut", "%1 is off") busca "shortcut|%1 is off" en la tabla. En
//    ingles, o si la tabla no la tiene, devuelve el texto en ingles.
//
// En espanol tambien se instala la traduccion de Qt (qtbase_es.qm, en los recursos): menu contextual de los
// campos, botones estandar de los cuadros y selector de archivos de Qt salen en espanol. setLanguage() la
// instala y la saca.
//
// Cambiar el idioma no repinta nada solo: quien lo cambia (AppController) rearma la ventana. Lo que
// se arma al vuelo (menu de la bandeja, avisos, ayuda) ya sale en el idioma nuevo la proxima vez.
namespace I18n {

enum class Language { English, Spanish };

Language language();
void setLanguage(Language language);

// "en" / "es": el valor guardado en settings.ini (app/language). Cualquier otro valor es ingles.
QString code(Language language);
Language fromCode(const QString &code);
// El nombre de cada idioma en su propio idioma ("English", "Español"), para el selector.
QString nativeName(Language language);

QString tr(const QString &english);
inline QString tr(const char *english)
{
    return tr(QString::fromUtf8(english));
}
QString trc(const char *context, const char *english);

// La tabla ingles -> espanol (I18nSpanish.cpp). Expuesta para el self-test.
const QHash<QString, QString> &spanishTable();

} // namespace I18n

#endif // MIGHTYTOOLS_I18N_H
