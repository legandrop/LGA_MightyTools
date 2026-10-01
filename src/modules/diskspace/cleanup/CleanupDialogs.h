#ifndef MIGHTYTOOLS_CLEANUPDIALOGS_H
#define MIGHTYTOOLS_CLEANUPDIALOGS_H

#include "modules/diskspace/cleanup/CleanupModel.h"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;

// Los carteles de la ventana de limpieza. Cada uno se arma con una funcion que NO lo muestra (asi la
// captura de QA lo dibuja sin exec()); quien lo usa llama a exec().
namespace CleanupDialogs {

// "Clean up 60.2 GB on C:?": las lineas mas pesadas, un "N more" y como queda el disco.
// `lines`: nombre y peso ya formateado. `rest`: las que quedan detras de "N more" (el link las
// despliega; vacia, el link no hace nada). Acepta con "Clean up".
QDialog *confirmCleanup(QWidget *parent, const QString &driveLabel, const QString &total, const QList<QPair<QString, QString>> &lines,
                        const QList<QPair<QString, QString>> &rest, int moreCount, const QString &moreSize, const QString &freeBefore,
                        const QString &freeAfter);

// "Delete 2 folders permanently?": las rutas con su peso. El resultado de exec() es Result.
// (Distintos de QDialog::Accepted: aceptar el cartel por otro camino nunca cuenta como "Delete".)
enum DeleteChoice { Cancel = 0, Delete = 2, ToRecycleBin = 3 };
// Que se borra, para el titulo: solo carpetas, solo archivos o de las dos cosas.
enum class DeleteKind { Items, Folders, Files };
QDialog *confirmDelete(QWidget *parent, const QList<QPair<QString, QString>> &paths, int moreCount, qint64 fileCount,
                       bool offerRecycleBin, DeleteKind kind = DeleteKind::Items);

// "Ask an AI before deleting": que se exporta y a donde va. El resultado de exec() es ExportChoice.
// `count`: cuantas cosas hay elegidas; `size`: su peso, ya formateado.
enum ExportChoice { ExportCancel = 0, ExportCopy = 4, ExportSave = 5 };
QDialog *exportForAi(QWidget *parent, int count, const QString &size);

} // namespace CleanupDialogs

// "Add a folder rule...": una carpeta, y si la regla es esa carpeta o toda carpeta con cierto nombre
// adentro de ella.
class FolderRuleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FolderRuleDialog(const QString &startDir, QWidget *parent = nullptr);
    Cleanup::FolderRule rule() const;
    // Quien decide si una regla sirve (esta en el disco abierto, no es una carpeta protegida). Sin
    // validador, alcanza con que la carpeta exista.
    void setValidator(const std::function<bool(const Cleanup::FolderRule &)> &validator);
    // QA: el cartel con datos cargados.
    void setExample(const QString &folder, const QString &match);

private:
    void refresh();
    void browse();

    QLineEdit *m_folder = nullptr;
    QLineEdit *m_match = nullptr;
    QPushButton *m_whole = nullptr;
    QPushButton *m_named = nullptr;
    QPushButton *m_add = nullptr;
    QLabel *m_problem = nullptr;
    QString m_startDir;
    std::function<bool(const Cleanup::FolderRule &)> m_validator;
};

#endif // MIGHTYTOOLS_CLEANUPDIALOGS_H
