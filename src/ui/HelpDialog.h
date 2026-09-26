#ifndef MIGHTYTOOLS_HELPDIALOG_H
#define MIGHTYTOOLS_HELPDIALOG_H

#include "ui/HelpSection.h"

#include <QDialog>
#include <QList>

class QScrollArea;

// Ayuda unica de la app (canvas, seccion 6): version, autor, link a GitHub y una seccion por
// herramienta. Mismo lenguaje que el HelpDialog de LGA_VideoDownloader: sin marco del sistema, su
// propia caja redondeada sobre un velo que oscurece la ventana.
//
// Encabezado y "Close" quedan fijos; las secciones van en un scroll propio: con cinco herramientas
// la ayuda puede ser mas alta que la pantalla, y el dialogo se acota a la pantalla de la ventana.
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(const QList<HelpSection> &sections, QWidget *parent = nullptr);

    // Abre el dialogo modal centrado sobre la ventana, con el velo detras, acotado a la pantalla.
    int execOver(QWidget *window);
    // Ancho fijo y el alto del contenido, sin pasar de `maxHeight` (0 = sin tope: la captura de QA
    // lo dibuja entero). Con tope, las secciones scrollean y el encabezado y "Close" quedan a la vista.
    void fitHeight(int maxHeight = 0);
    QScrollArea *scrollArea() const { return m_scroll; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QScrollArea *m_scroll = nullptr;
};

// Velo semitransparente sobre la ventana mientras hay un dialogo abierto.
class Scrim : public QWidget
{
    Q_OBJECT
public:
    explicit Scrim(QWidget *parent);
protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // MIGHTYTOOLS_HELPDIALOG_H
