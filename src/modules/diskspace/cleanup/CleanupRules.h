#ifndef MIGHTYTOOLS_CLEANUPRULES_H
#define MIGHTYTOOLS_CLEANUPRULES_H

#include "modules/diskspace/cleanup/CleanupModel.h"
#include "platform/SystemPaths.h"

#include <QList>
#include <QString>

class ScanEngine;

// Las reglas de limpieza: que carpetas de cache conoce la app, donde estan en ESTA maquina y cuanto
// pesan. Solo lectura: no borra nada (eso es CleanupJob).
//
// Principios (auditados 2026-09-30):
//  - Lista blanca de carpetas hijas conocidas; nunca la carpeta raiz de un programa (al lado de su
//    cache guarda credenciales, sesiones y ajustes).
//  - Toda ruta se resuelve a su ruta REAL. Si la carpeta final es un enlace, o termina en otro
//    volumen que el que la ventana tiene abierto, el item no se ofrece.
//  - Lo que solo es cache va en "Safe to delete" y arranca tildado. Lo que puede costar algo, o es del
//    usuario, va en "Yours to decide" y arranca destildado.
//  - Las reglas no nombran apps de escritorio una por una: reconocen la forma de sus carpetas
//    (Chromium: "Code Cache", "GPUCache"...) y le ponen al item el nombre de la carpeta que encuentran.
namespace CleanupRules {

struct Context
{
    SystemPaths::CleanupBases bases;
    QString volumeRoot; ///< real ("C:\")
    QList<Cleanup::FolderRule> folderRules;
};

// Las categorias con sus items, sin medir: lo que no depende del escaneo. Lee el disco (existencia y
// listados de un nivel).
QList<Cleanup::Category> build(const Context &context);

// Con el escaneo completo: suma los items que salen del arbol (reglas de carpetas del usuario, carpetas
// marcadas como cache), mide todos y marca los que tienen su programa corriendo. Conserva lo tildado
// de `previous` (por categoria y ruta); lo nuevo arranca tildado solo si es del grupo seguro.
QList<Cleanup::Category> refresh(const Context &context, ScanEngine &engine, const QList<Cleanup::Category> &previous);

// ---- Piezas, expuestas para el self-test y para las reglas de cada plataforma.

// Las categorias propias del sistema (plataforma): win/CleanupRulesWin.cpp, mac/CleanupRulesMac.cpp.
QList<Cleanup::Category> systemCategories(const Context &context);
// Ruta real de una carpeta que existe, esta en el volumen y no es un enlace; vacia si no.
QString usableDir(const QString &path, const Context &context);
// Un item con esos destinos; vacio (sin destinos) si ninguno sirve.
Cleanup::Item makeItem(const QString &name, const QString &shownPath, const QStringList &dirs, Cleanup::Action action,
                       const Context &context);
// La carpeta tiene la marca del estandar (los primeros bytes de su CACHEDIR.TAG son la firma).
bool hasCacheTagSignature(const QString &dir);

// Total tildado (lo que borraria "Clean up").
qint64 selectedBytes(const QList<Cleanup::Category> &categories);
// 2 todo tildado, 1 una parte, 0 nada (sobre los items que se pueden tildar).
int checkState(const Cleanup::Category &category);

} // namespace CleanupRules

#endif // MIGHTYTOOLS_CLEANUPRULES_H
