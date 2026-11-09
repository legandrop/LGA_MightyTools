#ifndef MIGHTYTOOLS_PALETTEEDITOR_H
#define MIGHTYTOOLS_PALETTEEDITOR_H

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class LgaColorPickerDialog;

// Editor de paletas, solo para desarrollo (MIGHTYTOOLS_PALETTE_EDITOR=1, el mismo de LGA GateBouncer): un color
// por grupo con el selector LGA, los tonos de cada grupo ajustables aparte, y guardar con nombre en la carpeta
// MIGHTYTOOLS_PALETTE_DIR (un .json por paleta). La ventana y el aviso de ejemplo cambian en vivo. El editor
// tiene colores fijos: una paleta ilegible no lo vuelve ilegible a el. Lo que recuerda (la ultima paleta
// abierta) va en un _editor.ini de esa carpeta, no en el settings.ini de la app.
class PaletteEditor final : public QWidget
{
public:
    explicit PaletteEditor(const QString &folder, QWidget *parent = nullptr);
    ~PaletteEditor() override;
    static QString folderFromEnvironment();
    // Al cerrar el editor (la app de desarrollo se cierra con el, no queda en la bandeja).
    std::function<void()> onClosed;

    // Muestra un aviso de Disk Space de prueba (tambien para la QA: sampleNotice() lo devuelve).
    void showSampleNotice();
    QWidget *sampleNotice() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildRows();
    void refreshList(const QString &select = {});
    void loadIndex(int index);
    void save();
    void pick(const QString &roleId, const QString &memberHex); // memberHex vacio: el base del grupo
    void markDirty(bool dirty);
    QString lastPalette() const;
    void rememberPalette(const QString &path) const;

    QString m_folder;
    QComboBox *m_list = nullptr;
    QLineEdit *m_name = nullptr;
    QCheckBox *m_liked = nullptr;
    QLabel *m_status = nullptr;
    QVBoxLayout *m_rows = nullptr;
    QPointer<LgaColorPickerDialog> m_picker;
    QPointer<QWidget> m_notice;
    QList<QColor> m_recent;
    bool m_loading = false;
};

#endif // MIGHTYTOOLS_PALETTEEDITOR_H
