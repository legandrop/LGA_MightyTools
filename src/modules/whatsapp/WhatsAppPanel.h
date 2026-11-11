#ifndef MIGHTYTOOLS_WHATSAPPPANEL_H
#define MIGHTYTOOLS_WHATSAPPPANEL_H

#include <QWidget>

class QLabel;
class QPushButton;
class SegmentedSwitch;
class ShortcutRow;
class StatusCard;
class WhatsAppModule;

// Panel de WhatsApp Chat: el estado (con «Open copied number» y la nota del ultimo intento), el atajo, el
// pais por defecto y donde se abren los chats con lo que se encontro instalado.
class WhatsAppPanel : public QWidget
{
    Q_OBJECT

public:
    WhatsAppPanel(WhatsAppModule *module, bool interactive, QWidget *parent = nullptr);

public slots:
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    QWidget *buildShortcutCard();
    QWidget *buildNumbersCard();
    QWidget *buildOpenCard();
    void showCountryMenu();
    void statusAction();

    WhatsAppModule *m_module = nullptr;
    bool m_interactive = false;
    StatusCard *m_status = nullptr;
    ShortcutRow *m_shortcut = nullptr;
    QPushButton *m_country = nullptr;
    QLabel *m_countryHint = nullptr;
    SegmentedSwitch *m_openIn = nullptr;
    QLabel *m_found = nullptr;
};

#endif // MIGHTYTOOLS_WHATSAPPPANEL_H
