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
// la despliega; el doble click la "activa" (la ventana la abre en el Explorador). Un click en un titulo
// pide ordenar por esa columna: la lista no ordena, solo muestra la flecha; el orden lo arma quien la
// llena (en Folders se ordena cada nivel del arbol por separado).
class SizeListView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    static constexpr int kRowHeight = 28;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kNameColumn = -1; ///< la columna del nombre, en setSort() y sortRequested()
    static constexpr int kNoColumn = -2;

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
    // La columna por la que esta ordenada (kNameColumn, o el indice de una de `columns`) y el sentido.
    void setSort(int column, bool descending);
    int sortColumn() const { return m_sortColumn; }
    bool sortDescending() const { return m_sortDescending; }
    // La columna del encabezado en esa x del viewport (kNoColumn si cae entre columnas).
    int headerColumnAt(int x) const;

signals:
    void expanderClicked(const QString &id);
    void selectionChanged();
    void activated(const QString &id);
    void sortRequested(int column);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int rowAt(const QPoint &pos) const;
    QRect expanderRect(int row) const;
    int nameLeft(const SizeListRow &row) const;
    int columnsLeft() const;
    void updateScrollRange();
    void setHovered(int row);
    void setHoveredHeader(int column);
    QString headerTitle(const QString &title, int column) const;
    // El tooltip de una fila (su ruta completa). `x`: donde apunta la flecha, en el viewport.
    void updateRowTip(int row, int x);
    // El nombre de esa fila no entra en el ancho que tiene (se pinta recortado).
    bool nameIsCut(const SizeListRow &row) const;

    QString m_nameTitle;
    QList<SizeListColumn> m_columns;
    QList<SizeListRow> m_rows;
    QSet<QString> m_selected;
    QString m_emptyText;
    int m_hovered = -1;
    int m_hoveredHeader = kNoColumn;
    int m_sortColumn = 0;
    bool m_sortDescending = true;
    int m_tipRow = -1;
    // Widget invisible que ocupa la fila con tooltip: le da al tooltip un ancla del tamano de la fila
    // (para ubicarse arriba o abajo de ELLA y ocultarse solo cuando el mouse la deja).
    QWidget *m_tipAnchor = nullptr;
    bool m_interactive = true;
    bool m_tree = true;
};

#endif // MIGHTYTOOLS_SIZELISTVIEW_H
