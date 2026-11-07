#ifndef MIGHTYTOOLS_TITLEBAR_H
#define MIGHTYTOOLS_TITLEBAR_H

#include <QWidget>

class QAbstractButton;

// Barra de titulo propia de la ventana principal (la ventana va sin el marco del sistema): el logo de
// la app, nombre, ayuda, minimizar y cerrar, sobre la tinta de la mesa (Pliego, D-73). Arrastrarla mueve
// la ventana con el sistema: en mac con el mouseDown real (WindowFrame::startDrag), en Windows con
// startSystemMove(), asi el sistema sigue haciendo el movimiento (y el acomodo contra los bordes de la
// pantalla). Cerrar llama a close() de la ventana, que la oculta a la bandeja como antes.
// La usa tambien la ventana de limpieza de Disk Space, con su propio titulo y sin el boton de ayuda.
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);
    TitleBar(const QString &title, bool withHelp, QWidget *parent = nullptr);
    void setTitle(const QString &title);

    // El «?» abre o cierra la ayuda de la pagina: se pinta magenta mientras esta abierta, y se apaga
    // donde no hay ayuda (General).
    void setHelpActive(bool active);
    void setHelpEnabled(bool enabled);
    QAbstractButton *helpButton() const { return m_help; }

signals:
    void helpClicked();
    // Un click en una zona libre de la barra arranca el arrastre de la ventana. Lo cuenta
    // `--ui-probe titlebar-drag` para saber que zonas arrastran.
    void dragRequested();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    class QLabel *m_title = nullptr;
    QAbstractButton *m_help = nullptr;
};

#endif // MIGHTYTOOLS_TITLEBAR_H
