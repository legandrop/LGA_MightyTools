#ifndef MIGHTYTOOLS_CALIBRATIONDIALOG_H
#define MIGHTYTOOLS_CALIBRATIONDIALOG_H

#include <QDialog>

// "Calibrate Dope Sheet": los tres pasos y la captura del layout de Nuke con el circulo sobre una
// zona vacia del Dope Sheet (la misma imagen y las mismas instrucciones que el calibrador de la
// version AutoHotkey). Start acepta el dialogo; quien lo abrio arranca la CalibrationSession.
// Mismo lenguaje visual que HelpDialog: sin marco del sistema, su propia caja redondeada.
class CalibrationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CalibrationDialog(QWidget *parent = nullptr);

    // Ancho fijo y alto del layout ya pulido. Publico para la captura de QA.
    void fitHeight();
    // Centrado sobre `anchor` si esta visible, si no en la pantalla principal.
    void centerOn(QWidget *anchor);

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // MIGHTYTOOLS_CALIBRATIONDIALOG_H
