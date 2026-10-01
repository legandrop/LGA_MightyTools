#ifndef MIGHTYTOOLS_CLEANUPPANE_H
#define MIGHTYTOOLS_CLEANUPPANE_H

#include "modules/diskspace/cleanup/CleanupModel.h"

#include <QAbstractButton>
#include <QList>
#include <QScrollArea>
#include <QSet>
#include <QString>

class QVBoxLayout;

// Casilla de tres estados de la limpieza: todo, una parte (una raya) o nada. Pintada, sin foco de
// teclado. El click avisa con clicked(); el estado lo pone quien la usa.
class TriBox : public QAbstractButton
{
    Q_OBJECT

public:
    explicit TriBox(QWidget *parent = nullptr);
    void setState(int state); ///< 2 todo, 1 una parte, 0 nada
    int state() const { return m_state; }
    QSize sizeHint() const override { return QSize(14, 14); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_state = 0;
};

// La pestana "Clean up": tres tarjetas (Safe to delete, Needs administrator, Yours to decide) con una
// fila por categoria. Cada categoria se abre y muestra sus renglones, cada uno con su casilla.
// Solo dibuja y avisa: lo tildado vive en la lista que le pasan y lo cambia la ventana.
class CleanupPane : public QScrollArea
{
    Q_OBJECT

public:
    // Renglon de la tarjeta "Skipped" despues de limpiar.
    struct SkippedLine
    {
        QString name;
        QString detail;
    };

    explicit CleanupPane(QWidget *parent = nullptr);

    // `measuring`: el escaneo todavia no termino y los pesos no estan.
    void setCategories(const QList<Cleanup::Category> &categories, bool measuring);
    // Cartel verde de arriba ("Freed 59.3 GB. C: has 91.6 GB free."). Vacio: sin cartel.
    void setBanner(const QString &text);
    void setSkipped(const QString &summary, const QList<SkippedLine> &lines);
    // false: nada responde al click (mientras se borra, y en la captura de QA).
    void setInteractive(bool interactive);
    void setOpen(const QString &categoryId, bool open);
    bool isOpen(const QString &categoryId) const { return m_open.contains(categoryId); }

signals:
    void categoryToggled(const QString &categoryId);
    void itemToggled(const QString &categoryId, int itemIndex);
    void reviewRequested(const QString &path);
    void systemCleanupRequested();
    void addRuleRequested();
    void removeRuleRequested(const QString &categoryId);

private:
    void rebuild();
    QWidget *groupCard(Cleanup::Group group, QWidget *parent);
    void addCategory(QVBoxLayout *layout, const Cleanup::Category &category, QWidget *parent);

    QList<Cleanup::Category> m_categories;
    bool m_measuring = false;
    bool m_interactive = true;
    QString m_banner;
    QString m_skippedSummary;
    QList<SkippedLine> m_skipped;
    QSet<QString> m_open;     ///< categorias desplegadas
    QSet<QString> m_showAll;  ///< categorias con "Show N more" ya abierto
};

#endif // MIGHTYTOOLS_CLEANUPPANE_H
