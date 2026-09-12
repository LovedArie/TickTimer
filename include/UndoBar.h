#pragma once
// ---------------------------------------------------------------------------
// UndoBar - the dark "Plan A moved ... Undo" bar in the bottom-right corner of
// the TickTimer window (31.2.0, move-and-swap addendum §M.12).
//
// WHY NOT THE REMINDER TOAST. The first cut reused NotificationToast, and the
// owner rejected it on sight: it looked exactly like a reminder, and it sat in
// the top-right corner of the SCREEN. An Undo is not news arriving from
// elsewhere; it is the far end of something you did a second ago, in this
// window.
//
// WHY INSIDE THE WINDOW. The same reason, plus the phone: AndroidNotifier
// posts a system notification with a title and a body only, so a toast-borne
// Undo had no button on a phone at all. A child widget works everywhere.
//
// HOW IT FLOATS. Parented to the window's body and kept OUT of every layout,
// so it covers a corner of the page instead of pushing the page aside - the
// capture button's construction. It watches its host's resizes itself, so no
// window has to remember to move it.
// ---------------------------------------------------------------------------

#include <QWidget>

#include <functional>

class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class QTimer;

// What the bar's button runs. A namespace-scope alias rather than a spelled-
// out std::function in each signal: moc copies signal argument types into
// generated code that lives OUTSIDE the class, where a name nested in the
// class would not resolve.
using UndoAction = std::function<void()>;

class UndoBar : public QWidget
{
    Q_OBJECT

public:
    explicit UndoBar(QWidget* host);

    // Show `text`, with an Undo button when `onUndo` is set; the button runs
    // it once and closes the bar. A new offer REPLACES the one showing - only
    // the latest drag can be undone - and restarts the clock. Ten seconds is
    // the owner's number.
    void offer(const QString& text, UndoAction onUndo = {},
               int msecs = 10 * 1000);

    // Room to leave below the bar. On a phone the capture button owns the
    // very corner, so the bar sits above it.
    void setBottomClearance(int px);

    QString text() const;
    bool    offersUndo() const { return bool(m_onUndo); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void place();
    void fadeOut();
    void dismiss();

    // All four are CHILD objects of this bar, so Qt's parent-child ownership
    // destroys them with it - none of them needs a delete.
    QLabel*                 m_label   = nullptr;
    QPushButton*            m_button  = nullptr;
    QTimer*                 m_timer   = nullptr; // how long it stays up
    QGraphicsOpacityEffect* m_opacity = nullptr; // what the fade animates
    QPropertyAnimation*     m_fade    = nullptr; // made on first use
    UndoAction              m_onUndo;
    int                     m_bottomClearance = 0;
};
