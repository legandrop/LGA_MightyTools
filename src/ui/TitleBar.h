#ifndef MIGHTYTOOLS_TITLEBAR_H
#define MIGHTYTOOLS_TITLEBAR_H

#include <QWidget>

// Barra de titulo propia de la ventana principal (la ventana va sin el marco del sistema): icono de
// la app, nombre, ayuda, minimizar y cerrar. Arrastrarla mueve la ventana con startSystemMove(),
// asi Windows sigue haciendo el movimiento (y el acomodo contra los bordes de la pantalla).
// Cerrar llama a close() de la ventana, que la oculta a la bandeja como antes.
// La usa tambien la ventana de limpieza de Disk Space, con su propio titulo y sin el boton de ayuda.
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget *parent = nullptr);
    TitleBar(const QString &title, bool withHelp, QWidget *parent = nullptr);
    void setTitle(const QString &title);

signals:
    void helpClicked();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    class QLabel *m_title = nullptr;
};

#endif // MIGHTYTOOLS_TITLEBAR_H
