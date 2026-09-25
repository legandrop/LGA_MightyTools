#ifndef MIGHTYTOOLS_VERSIONCOMPARE_H
#define MIGHTYTOOLS_VERSIONCOMPARE_H

#include <QString>

// Comparacion de versiones de la app. Fuente unica de verdad para decidir si el tag de
// un release es mas nuevo que la version instalada (MIGHTYTOOLS_VERSION).
//
// Compara NUMERICAMENTE POR SEGMENTO: 0.10 > 0.9 > 0.1, y 0.11 > 0.10. La app numera
// el minor como entero de dos digitos (0.10, 0.11, ...), no como decimal. La version anterior,
// portada de FM_VersionCompare (LGA_FileManagerS3), completaba el minor con ceros a la derecha
// hasta tres digitos, o sea que lo leia como decimal: 0.10 valia lo mismo que 0.1 y menos que
// 0.9. Ese esquema es el de las apps con minor de ancho fijo ("X.YYY") y aca no aplica.
namespace VersionCompare {

// True si candidateVersion es mas nueva que referenceVersion. Ambas toleran el prefijo "v" y
// espacios. Si alguna no es una lista de enteros separados por punto ("0.10", "1.2.3"),
// devuelve false: ante una version ilegible no se ofrece un update.
bool isNewer(const QString &candidateVersion, const QString &referenceVersion);

} // namespace VersionCompare

#endif // MIGHTYTOOLS_VERSIONCOMPARE_H
