#ifndef MIGHTYTOOLS_AREAPICKER_H
#define MIGHTYTOOLS_AREAPICKER_H

#include <QList>
#include <QElapsedTimer>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QWidget>

#include <functional>

class QScreen;

// Superpone solo geometria: nunca captura pixeles ni consulta ventanas durante --ui-shot.
class AreaPicker : public QWidget
{
public:
    enum class Mode { Draw, Window };
    AreaPicker(Mode mode, bool captureMode, QWidget *parent = nullptr);

    void setChosen(std::function<void(const QRectF &)> chosen) { m_chosen = std::move(chosen); }
    QString error() const { return m_error; }
    bool hasCandidates() const { return !m_windows.isEmpty(); }
    void open();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    struct Candidate { QString title; QRect geometry; };
    QRect workAreaAt(const QPoint &global) const;
    void finish(const QRect &selection, const QPoint &at);
    void refreshCandidates();
    int candidateAt(const QPoint &global) const;

    Mode m_mode;
    bool m_captureMode = false;
    bool m_dragging = false;
    bool m_windowPressed = false;
    QPoint m_start;
    QPoint m_current;
    int m_hovered = -1;
    QList<Candidate> m_windows;
    QElapsedTimer m_lastRefresh;
    QString m_error;
    std::function<void(const QRectF &)> m_chosen;
};

#endif // MIGHTYTOOLS_AREAPICKER_H
