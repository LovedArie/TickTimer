#pragma once
// ---------------------------------------------------------------------------
// NotificationToast — the popup TickTimer OWNS (v19.9).
//
// Why it exists: QSystemTrayIcon::showMessage doesn't show a popup — it
// SUBMITS one to the OS notification pipeline, and Windows may silently
// decline (Focus Assist, per-app notification settings, full-screen
// suppression). The owner heard the sound fire and saw no toast: the
// handler ran, the OS ate the balloon. An app that must be heard at
// 12:00 sharp cannot rent its voice — so this card is a plain window,
// drawn by us, suppressible by no one.
//
// It is the mini timer's recipe, third use (the flags are becoming house
// style): parentless (a parent = Win32 OWNER = hidden with a minimized
// owner — the v19.5.1 scar), Qt::Tool + Frameless + StaysOnTop,
// WA_TranslucentBackground (round corners are ours to paint), plus two
// new ones that make it a NOTIFICATION rather than a window:
//   WA_ShowWithoutActivating — it must never steal the keyboard from
//     whatever the user is typing in; a popup that grabs focus is a
//     different bug than a popup that doesn't appear;
//   WA_DeleteOnClose — fire-and-forget lifetime: it dismisses itself
//     (timer + fade), a click dismisses it early, nobody owns it.
// Stacking: live toasts park top-right of the primary screen, newest
// below the survivors; a static registry repositions the rest when one
// dies mid-stack.
// ---------------------------------------------------------------------------

#include <QPointer>
#include <QWidget>

#include <functional>

// ---------------------------------------------------------------------------
// ToastSpec — a toast as a VALUE (v28.0), same doctrine as ai::Provider:
// describing a thing with a struct instead of a parameter list means new
// kinds of toast are new values, not new overloads. Added for the
// affordability nudge; sized for what §G.1's check-in will need next
// (notification → tap → the app opens somewhere), so v28.2 extends a spec
// instead of rewriting a class.
// ---------------------------------------------------------------------------
struct ToastSpec
{
    // Kind drives the accent bar (see paintEvent): the app's voice-print
    // stays, but an Alert speaks in the danger red. Colour is data now, not
    // a constant buried in a paint routine — the same promotion DueTimeRole
    // made for the delegate.
    enum class Kind { Info, Alert };

    QString title;
    QString body;
    Kind    kind  = Kind::Info;
    int     msecs = 6000;

    // Optional action row. Empty text = no button = exactly the old toast.
    // onAction runs, then the toast closes; the plain click-to-dismiss
    // still works everywhere outside the button.
    QString               actionText;
    std::function<void()> onAction;
};

class NotificationToast : public QWidget
{
    Q_OBJECT

public:
    // The spec entry point (v28.0)...
    static NotificationToast* show(const ToastSpec& spec);
    // ...and the historical one, now a thin veneer over it. Kept because
    // ten call sites say show(title, body) and a mechanical churn through
    // them buys nothing.
    static NotificationToast* show(const QString& title, const QString& body,
                                   int msecs = 6000);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override; // click = dismiss

private:
    explicit NotificationToast(const ToastSpec& spec);
    void fadeOutAndClose();

    // THE movement seam: every position a toast ever takes goes through
    // here — restack() computes targets, moveTo() applies them. Today the
    // body is one move() call; the day entry/shuffle animates, this one
    // function grows a QPropertyAnimation and no caller changes. Built as
    // a joint on purpose; the motion itself stays YAGNI until wanted.
    void moveTo(const QPoint& target);

    static void restack(); // survivors close ranks when one dies early

    ToastSpec m_spec; // kept: paintEvent reads kind after construction

    static QList<QPointer<NotificationToast>> s_live;
};
