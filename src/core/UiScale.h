#ifndef MIGHTYTOOLS_UISCALE_H
#define MIGHTYTOOLS_UISCALE_H

#include <QSize>
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

// Si la app ofrece el tamano de interfaz en esta plataforma. En macOS no (D-45): Qt en mac dibuja mal el
// texto con una escala de pantalla fraccionaria (1.1 da 2.2 en una Retina: pierde la negrita y deja
// restos de texto), y macOS ya tiene su propia escala en Ajustes > Pantallas. Ahi la app va siempre al
// tamano del diseno (el 0) y applyBeforeApp no toca nada.
bool supported();

// 1.0, 1.1, 1.2. Un nivel fuera de rango vale como el de fabrica.
qreal factor(int level);
int clampLevel(int level);

// Limite por pantalla (D-35): un nivel solo vale si la ventana mas grande de la app entra entera en el
// area util de la pantalla principal. La ventana de limpieza es la mas ancha (960) y la principal la
// mas alta (676); el self-test controla que ninguna crezca sin pasar por aca.
QSize largestWindow();
// El nivel mas alto con el que largestWindow() entra en `unscaledArea` (pixeles logicos del sistema, sin
// el factor de la app). El 0 siempre vale. Un area invalida no limita (kMaxLevel).
int maxFittingLevel(const QSize &unscaledArea);
// `wanted` (el guardado) recortado al que entra. El guardado no se toca: en una pantalla mas grande
// vuelve solo.
int fitLevel(int wanted, const QSize &unscaledArea);

// Corridas automatizadas: la pantalla virtual de la captura (800 x 600) no es la del usuario. main la
// reemplaza con --screen-area <ancho>x<alto> (sin el flag, QSize(): no se limita) y la pagina General
// usa esta en lugar de la de Qt.
void overrideScreenArea(const QSize &unscaledArea);
bool screenAreaOverridden();
QSize overriddenScreenArea();

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
