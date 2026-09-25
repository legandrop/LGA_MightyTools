#ifndef MIGHTYTOOLS_HELPDIALOG_H
#define MIGHTYTOOLS_HELPDIALOG_H

#include "ui/HelpSection.h"

#include <QDialog>
#include <QList>

// Ayuda unica de la app (canvas, seccion 6): version, autor, link a GitHub y una seccion por
// herramienta. Mismo lenguaje que el HelpDialog de LGA_VideoDownloader: sin marco del sistema, su
// propia caja redondeada sobre un velo que oscurece la ventana.
class HelpDialog : public QDialog
{
    Q_OBJECT
public:
    explicit HelpDialog(const QList<HelpSection> &sections, QWidget *parent = nullptr);

    // Abre el dialogo modal centrado sobre la ventana, con el velo detras.
    int execOver(QWidget *window);
    // Ancho fijo y alto del layout ya pulido. Publico para la captura de QA.
    void fitHeight();

protected:
    void paintEvent(QPaintEvent *event) override;
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
