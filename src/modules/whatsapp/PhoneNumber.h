#ifndef MIGHTYTOOLS_PHONENUMBER_H
#define MIGHTYTOOLS_PHONENUMBER_H

#include <QList>
#include <QString>
#include <QStringList>

// Lectura de un telefono en un texto copiado, para abrir su chat de WhatsApp (WhatsApp Chat, D-78).
//
// WhatsApp pide el numero internacional sin el «+» (5491145678901). Lo que la gente copia viene de mil
// formas: «011 15-4567-8901», «(0351) 15 123-4567», «+54 9 11 4567 8901», «Cel: 11 4567-8901», un link
// wa.me. Este modulo:
//  - busca candidatos en el texto (secuencias de digitos con separadores tipicos) y descarta lo que se
//    parece a otra cosa (CUIT/CUIL, fechas, IPs, tarjetas, numeros pegados a letras o a una URL);
//  - normaliza cada candidato con el pais por defecto. Lo que viene con + o 00 se toma tal cual (salvo
//    Argentina, que se arregla: sin 0, sin 15 y con el 9 de los celulares);
//  - si todos los candidatos dan el mismo numero, ese es; si dan dos distintos, no elige.
//
// Argentina sin tabla de areas: los codigos de area son libres de prefijo y, con el numero, suman 10
// digitos. El unico de 2 digitos es 11; los demas empiezan con 2 o 3 y tienen 3 o 4. El «15» va
// justo despues del area, y en 12 digitos no puede aparecer a la vez en la posicion 3 y en la 4: la
// posicion es unica sin saber el area.
//
// Sin efectos y sin leer nada del sistema: lo prueba el --self-test.
namespace PhoneNumber {

struct Country
{
    QString id;          ///< «AR», estable (va a settings.ini)
    QString name;        ///< en ingles; la UI lo traduce
    QString code;        ///< codigo de pais: «54»
    QList<int> lengths;  ///< largos del numero nacional sin el 0 de larga distancia
};

// Los paises del desplegable, Argentina primero. «none» = sin pais: el numero tiene que venir con +.
const QList<Country> &countries();
const Country *country(const QString &id);
QString defaultCountry(); ///< «AR»

enum class Status {
    Ok,
    None,       ///< no hay ningun telefono en el texto
    Several,    ///< hay dos o mas telefonos distintos
    Incomplete, ///< parece un telefono pero le falta algo (el area, el codigo de pais)
};

struct Result
{
    Status status = Status::None;
    QString digits;     ///< internacional sin +: «5491145678901»
    QString display;    ///< para mostrar: «+54 9 11 4567-8901»
    QStringList found;  ///< Several: los numeros distintos encontrados (display)
    // Incomplete: el motivo como codigo; el texto lo arma la UI. «area» (numero local sin el area),
    // «argentina» (no es un numero argentino completo), «country-code» (sin pais por defecto y sin +),
    // «incomplete» (con + pero muy corto o muy largo), «incomplete-for» (no da el largo del pais por defecto).
    QString problem;
    bool fromLink = false;
};

// Busca y normaliza. `countryId` vacio o desconocido = «none».
Result parse(const QString &text, const QString &countryId);

// Un candidato suelto (sin buscar en un texto). Devuelve los digitos internacionales o vacio con el motivo.
QString normalize(const QString &candidate, const QString &countryId, QString *problem = nullptr);

// «+54 9 11 4567-8901» a partir de los digitos internacionales.
QString format(const QString &digits);

// Para el log: «13 digitos, +54…», nunca el numero entero (regla de los links).
QString logText(const QString &digits);

} // namespace PhoneNumber

#endif // MIGHTYTOOLS_PHONENUMBER_H
