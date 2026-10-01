#ifndef MIGHTYTOOLS_UISCALE_H
#define MIGHTYTOOLS_UISCALE_H

#include <QString>
#include <QtGlobal>

// Tamano general de la interfaz: 0, 1 (el de fabrica, D-35) y 2. Agranda TODO por igual (textos, iconos,
// margenes, ventanas) con el factor de escala global de Qt (QT_SCALE_FACTOR), como si la pantalla
// tuviera mas DPI. El layout en pixeles logicos no cambia: nada se corre respecto del tamano 0, y la
// ventana crece sola en la pantalla.
//
// Qt lee el factor una sola vez, al crear la QApplication: cambiarlo pide reiniciar la app
// (AppController lo hace solo). La variable de entorno se saca apenas existe la QApplication, para
// que no la herede lo que la app lanza (NukeX tambien es una app de Qt y saldria agrandado).
namespace UiScale {

inline constexpr int kMaxLevel = 2;
// El de fabrica: sin nada guardado (o con un valor roto) la app arranca en 1.
inline constexpr int kDefaultLevel = 1;
// Clave en settings.ini (seccion [app]).
QString settingsKey();

// 1.0, 1.1, 1.2. Un nivel fuera de rango vale como el de fabrica.
qreal factor(int level);
int clampLevel(int level);

// Lee el nivel guardado en settings.ini (sin nada guardado, kDefaultLevel). Se puede llamar antes de la QApplication (con los nombres de
// la app ya fijados).
int readSavedLevel();

// Antes de la QApplication: fija QT_SCALE_FACTOR si el nivel no es 0 y lo recuerda como el nivel de
// esta sesion. Con nivel 0 no toca el entorno.
void applyBeforeApp(int level);
// Despues de la QApplication: saca la variable que puso applyBeforeApp (si la puso), o devuelve la que
// el usuario ya tenia.
void clearEnvironmentAfterApp();

// El nivel con que arranco esta sesion (lo que se ve ahora).
int sessionLevel();

} // namespace UiScale

#endif // MIGHTYTOOLS_UISCALE_H
