#ifndef MIGHTYTOOLS_SIZELISTVIEW_H
#define MIGHTYTOOLS_SIZELISTVIEW_H

#include "ui/UiWidgets.h"

#include <QAbstractScrollArea>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// Una columna a la derecha del nombre.
struct SizeListColumn
{
    enum class Kind {
        Size,  ///< el peso: a la derecha, en texto fuerte
        Share, ///< una barrita con la parte que ocupa y su porcentaje
        Text,  ///< dato secundario, a la derecha, en gris
    };
    QString title;
    int width = 84;
    Kind kind = Kind::Text;
    bool sorted = false; ///< la columna por la que esta ordenada la lista
};

struct SizeListRow
{
    enum class Expander { None, Closed, Open };

    QString id;     ///< estable: lo que vuelve en las senales
    QString name;
    QString detail; ///< ruta al lado del nombre, en gris, recortada por la izquierda
    QString note;   ///< aclaracion en cursiva despues del nombre ("counting…")
    QString chip;   ///< etiqueta chica despues del nombre ("system")
    QString tooltip;
    int depth = 0;
    bool hasIcon = true;
    Icon icon = Icon::Folder;
    Expander expander = Expander::None;
    QStringList cells; ///< una por columna
    double share = -1.0; ///< 0..1 para la columna Share; negativo: sin barra
    int tone = 0;        ///< color del peso: 0 normal, 1 crecio (ambar), 2 bajo (verde)
    bool selectable = true;
    bool muted = false; ///< fila secundaria, en gris y cursiva ("12,400 smaller files")
};

// La lista de la ventana de limpieza: filas de 28 px ordenadas por peso, con arbol opcional. Pinta solo
// las filas visibles, asi que una carpeta con cien mil hijos no cuesta mas que una con diez. Es una
// lista y no un mapa a proposito: borrar algo cambia unas pocas filas y no hay nada que redibujar entero.
//
// No toma foco de teclado (regla de la app). Un click elige o suelta una fila; el click en la flecha
// la despliega; el doble click la "activa" (la ventana la abre en el Explorador).
class SizeListView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    static constexpr int kRowHeight = 28;
    static constexpr int kHeaderHeight = 28;

    explicit SizeListView(QWidget *parent = nullptr);

    void setColumns(const QString &nameTitle, const QList<SizeListColumn> &columns);
    // Reemplaza las filas. Conserva el desplazamiento, y de lo elegido lo que siga estando.
    void setRows(const QList<SizeListRow> &rows);
    const QList<SizeListRow> &rows() const { return m_rows; }

    QStringList selectedIds() const;
    void setSelectedIds(const QStringList &ids);
    void clearSelection();
    // false: no se elige ni se despliega (mientras se borra, y en la captura de QA).
    void setInteractive(bool interactive);
    // Texto del centro cuando no hay filas.
    void setEmptyText(const QString &text);
    // false: lista plana, sin el lugar de la flecha a la izquierda (Largest files, What changed).
    void setTree(bool tree);

signals:
    void expanderClicked(const QString &id);
    void selectionChanged();
    void activated(const QString &id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    int rowAt(const QPoint &pos) const;
    QRect expanderRect(int row) const;
    int nameLeft(const SizeListRow &row) const;
    int columnsLeft() const;
    void updateScrollRange();
    void setHovered(int row);

    QString m_nameTitle;
    QList<SizeListColumn> m_columns;
    QList<SizeListRow> m_rows;
    QSet<QString> m_selected;
    QString m_emptyText;
    int m_hovered = -1;
    bool m_interactive = true;
    bool m_tree = true;
};

#endif // MIGHTYTOOLS_SIZELISTVIEW_H
