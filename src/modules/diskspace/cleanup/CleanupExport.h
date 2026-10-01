#ifndef MIGHTYTOOLS_CLEANUPEXPORT_H
#define MIGHTYTOOLS_CLEANUPEXPORT_H

#include "modules/diskspace/cleanup/CleanupModel.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// "Export for AI...": lo elegido para borrar, escrito como un Markdown con la pregunta ya hecha, para
// que el usuario se lo mande a su asistente de IA y le pregunte si es seguro borrarlo ANTES de hacerlo.
//
// Aca solo se arma el texto: sin interfaz y sin red (la app no le manda nada a nadie; el usuario copia
// o guarda el archivo y lo lleva a donde quiera). Lleva nombres, rutas, tamanos y fechas, nunca el
// contenido de un archivo. Sale en el idioma de la interfaz, que es el que el usuario va a usar con su
// asistente. Solo lectura: del disco se listan nombres y tamanos de lo que hay adentro de una carpeta.
class ScanEngine;

namespace CleanupExport {

// Algo que hay adentro de una carpeta elegida.
struct Child
{
    QString name;
    qint64 bytes = 0;
    bool isDir = false;
};

// Una cosa elegida para borrar.
struct Entry
{
    QString what;        ///< rotulo de la app ("Python package caches"); vacio en lo elegido a mano
    // Nombre que sale del disco (una carpeta, un archivo, la carpeta de datos de una app). No es texto de
    // confianza: en el Markdown va SIEMPRE como codigo, igual que las rutas, para que un nombre armado
    // a proposito no se lea como una instruccion.
    QString name;
    QString path;        ///< la ruta que se muestra; vacia si no hay (la Papelera)
    QStringList targets; ///< si lo que se borra son varias carpetas adentro de `path` (un navegador)
    qint64 bytes = 0;
    qint64 files = 0;
    qint64 newest = 0;   ///< ultimo cambio (segundos desde 1970); 0 si no se sabe
    QString how;         ///< como se borra, ya en palabras
    QString why;         ///< por que la app lo lista (la leyenda de su categoria); vacio en lo elegido a mano
    QString group;       ///< "Safe to delete" / "Yours to decide"; vacio en lo elegido a mano
    bool isDir = true;
    bool partial = false; ///< solo se le borra una parte (lo viejo de la carpeta temporal)
    // Lo mas pesado que tiene adentro, de mayor a menor, y lo que quedo sin listar.
    QList<Child> inside;
    int restCount = 0;
    qint64 restBytes = 0;
};

struct Request
{
    QString driveLabel;  ///< "C:"
    QString system;      ///< "Windows 11 Version 24H2"
    qint64 freeBytes = 0;
    qint64 totalBytes = 0;
    QDateTime when;
    QList<Entry> entries;
};

// Cuantas carpetas llevan el detalle de lo que tienen adentro (las mas pesadas) y cuanto de cada una.
constexpr int kDetailedEntries = 15;
constexpr int kChildrenPerEntry = 12;
// Tope de secciones de detalle (una seleccion de cientos de carpetas no arma un archivo interminable).
constexpr int kSectionLimit = 60;

// ---- Armado de las entradas.
// Lo tildado en "Clean up" (lo que borraria el boton), sin lo bloqueado ni lo que solo informa.
QList<Entry> entriesForChecked(const QList<Cleanup::Category> &categories);
// Algo elegido a mano en Folders o en Largest files.
Entry entryForPath(const QString &path, bool isDir, qint64 bytes, qint64 files, qint64 newest);
// Completa "lo mas pesado adentro" de las entradas mas pesadas (kDetailedEntries): las subcarpetas salen
// del arbol del escaneo y los archivos se listan del disco en el momento.
void detail(QList<Entry> &entries, ScanEngine &engine);

// ---- El texto. No lee nada.
QString markdown(const Request &request);
// "DiskSpace_C_2026-10-01_1432.md"
QString suggestedFileName(const Request &request);
qint64 totalBytes(const Request &request);

} // namespace CleanupExport

#endif // MIGHTYTOOLS_CLEANUPEXPORT_H
