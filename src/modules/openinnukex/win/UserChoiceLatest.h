// UserChoiceLatest: hash de asociaciones de archivos de Windows 11 (UserChoiceLatest) y del
// UserChoice clasico, portado a C++ desde el helper de consola LGA_WinSetFTA.
//
// Derivado de:
//   PS-SFTA - https://github.com/DanysysTeam/PS-SFTA
//     Authors  : Danyfirex & Dany3j
//     Credits  : https://bbs.pediy.com/thread-213954.htm
//                LMongrain - Hash Algorithm PureBasic Version
//     License  : MIT License
//     Copyright: 2022 Danysys. <danysys.com>
//   DefaultApps - https://github.com/araghon007/DefaultApps
//     License  : MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Organizacion del modulo:
//   1. Funciones PURAS: arman el texto a hashear y calculan los hashes. No leen ni escriben nada
//      del sistema; son las que cubren las pruebas de goldens.
//   2. Lectura del sistema: SID, MachineId, cadenas de validacion de
//      Windows.Internal.OpenWithHost.dll, texto de experiencia de shell32.dll, HashVersion.
//   3. Escritura: applyAssociation() hace la secuencia del helper (UserChoice, UserChoiceLatest
//      y aviso al shell; sin sus toasts) con el empaquetado de Windows. Escribe en HKCU de la
//      maquina: nunca desde una prueba.
//
// Hay dos empaquetados del texto hasheado (HashMode):
//   - encodeHashString(): traduccion fiel del helper, con sus fallas: donde el helper tiraba una
//     excepcion, devuelve el mismo tipo de error (HashError). Sirve para comparar con el helper.
//   - encodeHashStringWindows(): el mismo resultado que la funcion de empaquetado de
//     Windows.Internal.OpenWithHost.dll. Es el unico que usa applyAssociation().
// El paso final (hashBytes) es comun a los dos.

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <array>
#include <functional>

namespace UserChoiceLatest {

// Equivalentes de las excepciones del helper original, mas los casos que el empaquetado de
// Windows no permite reproducir.
enum class HashError {
    None,
    IndexOutOfRange,    // IndexOutOfRangeException
    ArgumentOutOfRange, // ArgumentOutOfRangeException
    Argument,           // ArgumentException
    RepeatedWord,       // modo Windows: dos palabras iguales (Windows usaria una referencia hacia atras)
    EmptyInput,         // modo Windows: texto vacio
};

// Que empaquetado usa el hash de UserChoiceLatest.
//  - Windows: el mismo que hace Windows.Internal.OpenWithHost.dll (verificado contra la DLL con
//    miles de cadenas). Es el que corresponde escribir.
//  - HelperCompatible: el del helper LGA_WinSetFTA, rarezas incluidas. En una parte de los casos
//    difiere del de Windows o falla; queda SOLO para pruebas contra el helper.
enum class HashMode {
    Windows,
    HelperCompatible,
};

// Nombre del tipo de excepcion equivalente ("IndexOutOfRangeException", etc.); vacio si None.
QString hashErrorName(HashError error);

// Resultado de un hash en texto (Base64). hash vacio con error None = el original devolvia "".
struct HashResult {
    QString hash;
    HashError error = HashError::None;
    bool ok() const { return error == HashError::None; }
};

// Resultado del empaquetado de la cadena (CalculateHash del original).
struct EncodeResult {
    QByteArray bytes; // 4 bytes de cabecera + (largo + 1) * 16 bytes de palabras
    HashError error = HashError::None;
    bool ok() const { return error == HashError::None; }
};

// Recorrido del empaquetado, solo para diagnostico.
struct EncodeTrace {
    int normalWords = 0;  // palabras de la rama principal
    int tailWords = 0;    // palabras de la rama de cola (hashLength2 < ancho del selector)
    int hackfixWords = 0; // palabras de cola con el ajuste "hashLength + 1 == roundLength"
};

// Las tres cadenas de validacion que Windows.Internal.OpenWithHost.dll trae embebidas; el
// MachineId elige cual se usa.
using ValidationStrings = std::array<QString, 3>;

// ─── 1. Funciones puras ─────────────────────────────────────────────────────

// FILETIME (100 ns desde 1601, UTC) como 16 digitos hex en minuscula: alto (8) + bajo (8).
QString hexFileTime(quint64 fileTime);

// Quita las llaves de un MachineId leido del registro ("{...}" -> "...").
QString trimMachineId(const QString &rawMachineId);

// Indice 0..2 de cadena de validacion y de orden de campos: ultimo caracter del MachineId % 3.
// MachineId vacio -> 0.
int validationIndex(const QString &machineId);

// Texto que se hashea para UserChoiceLatest (sin pasar a minusculas): los seis campos (extension,
// SID, ProgID, marca de tiempo, cadena de validacion, MachineId) en el orden que fija el MachineId.
QString buildHashString(const QString &machineId, const QString &userSid,
                        const QString &extension, const QString &progId, quint64 fileTime,
                        const ValidationStrings &validation);

// Minusculas por unidad UTF-16 / punto de codigo, sin cambiar el largo (ToLowerInvariant).
QString toLowerInvariant(const QString &text);

// Empaquetado de la cadena en palabras de 64 bits (CalculateHash del original). No pasa a
// minusculas: recibe el texto tal cual.
EncodeResult encodeHashString(const QString &text, EncodeTrace *trace = nullptr);

// Hash de 8 bytes en Base64 sobre un bloque de bytes (GetHash(byte[], int) de PS-SFTA). Es el
// mismo para UserChoice y para el ultimo paso de UserChoiceLatest.
HashResult hashBytes(const QByteArray &bytes, int lengthBase);

// Hash del UserChoice clasico sobre el texto base, que ya termina en "\0" (buildLegacyBaseInfo):
// UTF-16 del texto y lengthBase = largo * 2, como PS-SFTA y como Windows.
HashResult legacyHashOfString(const QString &text);

// Solo para pruebas: el calculo del helper (lengthBase = largo * 2 + 2 sin bytes extra), que falla
// con ArgumentException cuando el largo % 4 == 3.
HashResult legacyHashOfStringHelperCompatible(const QString &text);

// Marca de tiempo (FILETIME) truncada al minuto (UserChoice) o al milisegundo (UserChoiceLatest).
quint64 truncateToMinute(quint64 fileTime);
quint64 truncateToMillisecond(quint64 fileTime);

// true si el texto no esta vacio y es todo ASCII imprimible (0x20..0x7E).
bool isPlainAscii(const QString &text);

// Version de archivo (a.b.c.d) leida del recurso VS_FIXEDFILEINFO de un PE. Vacia si no esta.
QString fileVersionFromBytes(const QByteArray &peBytes);

// Hash de UserChoiceLatest a partir del texto ya armado (cola de GetHashNew): minusculas,
// empaquetado, largo util hasta el primer par de bytes en cero dentro de los primeros
// (largo + 8) bytes, y hashBytes() sobre ese tramo.
HashResult latestHashOfString(const QString &hashString);

// Hash de UserChoiceLatest completo (GetHashNew del original).
HashResult computeLatestHash(const QString &machineId, const QString &userSid,
                             const QString &extension, const QString &progId, quint64 fileTime,
                             const ValidationStrings &validation);

// Empaquetado como lo hace Windows. Diferencias con encodeHashString():
//   - la cola se decide con los caracteres que realmente quedan (no con una estimacion), y cada
//     palabra de cola avanza a la siguiente;
//   - las unidades de 16 bits en cero se quitan de todas las palabras (el resultado es un texto
//     UTF-16 sin ceros intermedios);
//   - la cabecera lleva cantidad de palabras + 1;
//   - la salida se corta en largo + 32 unidades de 16 bits (tope del bufer de Windows).
// Devuelve las unidades hasta el primer cero, SIN terminador. Si dos palabras salen iguales,
// Windows usaria una referencia hacia atras que no esta reproducida: devuelve RepeatedWord.
EncodeResult encodeHashStringWindows(const QString &text);

// Hash de UserChoiceLatest a partir del texto ya armado, con el empaquetado de Windows:
// minusculas, encodeHashStringWindows(), terminador de 2 bytes y hashBytes() sobre todo.
HashResult latestHashOfStringWindows(const QString &hashString);

// Hash de UserChoiceLatest completo con el modo elegido.
HashResult computeLatestHash(const QString &machineId, const QString &userSid,
                             const QString &extension, const QString &progId, quint64 fileTime,
                             const ValidationStrings &validation, HashMode mode);

// Verificacion pura para el self-test del modulo: vectores fijos con datos sinteticos (entradas ->
// hash esperado) del modo Windows y del hash viejo, mas casos negativos. No lee el registro.
void runSelfTestVectors(const std::function<void(bool ok, const QString &what)> &check);

// Texto base del UserChoice clasico: extension + SID + ProgID + marca + experiencia + "\0",
// en minusculas (SFTA.SetFTA).
QString buildLegacyBaseInfo(const QString &extension, const QString &userSid,
                            const QString &progId, const QString &hexDateTime,
                            const QString &userExperience);

// Busqueda de las cadenas de validacion en los bytes de Windows.Internal.OpenWithHost.dll.
// Devuelve cuantas de las tres encontro.
int findValidationStrings(const QByteArray &dllBytes, ValidationStrings *out);

// Busqueda del texto de experiencia en los bytes de shell32.dll; si no esta, el texto fijo.
QString findUserExperience(const QByteArray &shell32Bytes);

// ─── 2. Lectura del sistema (solo lectura) ─────────────────────────────────

QString currentUserSid();

// HKLM\SOFTWARE\Microsoft\SQMClient\MachineId tal cual (con llaves). Vacio si no existe.
QString readMachineIdRaw();

// Lee Windows.Internal.OpenWithHost.dll y extrae las tres cadenas. false si falta alguna.
bool readValidationStrings(ValidationStrings *out, QString *error);

// Lee shell32.dll y extrae el texto de experiencia (o el fijo si no lo encuentra).
QString readUserExperience();

// true si HKLM\...\SystemProtectedUserData\<SID>\AnyoneRead\AppDefaults\HashVersion == "1",
// o sea, si Windows valida con UserChoiceLatest.
bool isLatestHashActive(const QString &userSid);

// Minuto local actual, truncado a segundos cero, como FILETIME UTC (DateTime.ToFileTime).
quint64 currentMinuteFileTime();

// Version de archivo de Windows.Internal.OpenWithHost.dll (para el log).
QString readOpenWithHostVersion();

// ─── 3. Escritura (HKCU de la maquina) ──────────────────────────────────────

struct ApplyResult {
    bool ok = false;            // asociacion escrita (ver applyAssociation)
    QString reason;             // motivo si ok == false
    QString latestHash;         // Hash escrito en UserChoiceLatest
    QString legacyHash;         // Hash escrito en UserChoice (vacio si no se escribio)
    quint64 latestFileTime = 0; // marca usada para el hash de UserChoiceLatest
    int latestAttempts = 0;     // intentos de marca de tiempo usados para UserChoiceLatest
    bool latestActive = false;  // HashVersion == 1 (Windows valida con UserChoiceLatest)
    QString previousProgId;     // ProgId que habia en UserChoiceLatest antes de escribir
    bool restoredPrevious = false; // si fallo, se volvio a escribir la eleccion anterior
    QString dllVersion;         // version de Windows.Internal.OpenWithHost.dll usada
    QStringList warnings;       // pasos secundarios que fallaron o relectura que no coincide
};

// Asocia extension -> progId para el usuario actual:
//   1. (sin toasts: el helper original escribia ApplicationAssociationToasts en 0 para los ProgID
//      de TODAS las apps de la extension; el hash no los usa y eran datos ajenos, se sacaron);
//   2. UserChoice clasico: borrar, ProgId y Hash calculado con la ultima escritura de la clave
//      truncada al minuto;
//   3. UserChoiceLatest\ProgId\ProgId = progId, marca = ultima escritura de esa clave truncada al
//      milisegundo, UserChoiceLatest\Hash = hash nuevo con el empaquetado de Windows;
//   4. SHChangeNotify(SHCNE_ASSOCCHANGED);
//   5. relectura de las dos claves: si no quedaron como se escribieron, va a warnings.
// Rechaza extensiones y ProgIDs vacios o no ASCII. Todo lo que hay que leer (SID, MachineId,
// cadenas de la DLL) se lee antes de escribir nada. Si el hash nuevo no se puede calcular para una
// marca, reescribe el ProgId (marca nueva) y reintenta hasta maxAttempts; si igual falla, vuelve a
// escribir el ProgId anterior con su hash recalculado (o, si no habia, borra ProgId y Hash).
// ok == true si UserChoiceLatest quedo escrito y, cuando HashVersion no esta activo, tambien el
// UserChoice clasico.
ApplyResult applyAssociation(const QString &extension, const QString &progId, int maxAttempts = 5);

} // namespace UserChoiceLatest
