#include "core/I18n.h"

#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QTranslator>
#include <QtDebug>

#include <atomic>

namespace I18n {

namespace {
// Atomico: lo leen tambien hilos que no son el de la UI (el Apply de Open in NukeX arma sus mensajes de error
// en su propio hilo).
std::atomic<Language> g_language{Language::English};

// Las cadenas de Qt (menu contextual de los campos, botones estandar, selector de archivos de Qt) salen de la
// traduccion qtbase_es.qm que viaja en los recursos. Se instala en espanol y se saca al volver a ingles.
// El traductor cuelga de la aplicacion: se borra con ella.
void updateQtTranslator(Language language)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) {
        return;
    }
    static QPointer<QTranslator> translator;
    if (language == Language::Spanish) {
        if (!translator) {
            auto *loaded = new QTranslator(app);
            if (!loaded->load(QStringLiteral(":/translations/qtbase_es.qm"))) {
                qWarning() << "[I18n] No se pudo cargar :/translations/qtbase_es.qm";
                delete loaded;
                return;
            }
            translator = loaded;
        }
        QCoreApplication::installTranslator(translator);
    } else if (translator) {
        QCoreApplication::removeTranslator(translator);
    }
}
} // namespace

Language language()
{
    return g_language.load();
}

void setLanguage(Language language)
{
    g_language.store(language);
    updateQtTranslator(language);
}


QString code(Language language)
{
    return language == Language::Spanish ? QStringLiteral("es") : QStringLiteral("en");
}

Language fromCode(const QString &code)
{
    return code.trimmed().compare(QLatin1String("es"), Qt::CaseInsensitive) == 0 ? Language::Spanish
                                                                                  : Language::English;
}

QString nativeName(Language language)
{
    return language == Language::Spanish ? QStringLiteral("Español") : QStringLiteral("English");
}

QString tr(const QString &english)
{
    if (g_language == Language::English) {
        return english;
    }
    const auto &table = spanishTable();
    const auto it = table.constFind(english);
    return it == table.constEnd() ? english : it.value();
}

QString trc(const char *context, const char *english)
{
    const QString text = QString::fromUtf8(english);
    if (g_language == Language::English) {
        return text;
    }
    const auto &table = spanishTable();
    const auto it = table.constFind(QString::fromUtf8(context) + QLatin1Char('|') + text);
    return it == table.constEnd() ? text : it.value();
}

} // namespace I18n
