#ifndef MIGHTYTOOLS_PANELSTATE_H
#define MIGHTYTOOLS_PANELSTATE_H

// Lo que un panel tiene a medio hacer y que se perderia si la ventana se suelta (D-77: la ventana se destruye a los
// 5 minutos de estar escondida, y con ella los paneles). Un panel que puede tener trabajo sin guardar (un editor
// abierto, un campo elegido sin Save, una accion que espera al sistema) hereda esta interfaz ademas de QWidget;
// MainWindow::hasUnsavedWork() la consulta con dynamic_cast y, si alguno dice que si, la ventana no se suelta.
// Ante la duda, true: el costo de equivocarse para ese lado es solo memoria.
class PanelState
{
public:
    virtual ~PanelState() = default;
    virtual bool hasUnsavedWork() const = 0;
};

#endif // MIGHTYTOOLS_PANELSTATE_H
