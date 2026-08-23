#pragma once
// ---------------------------------------------------------------------------
// SlidePanel — a slide-over drawer (v22.9, owner request: "two buttons that
// open a side screen" instead of inline accordions).
//
// WHAT IT IS. One widget that covers its host: the widget ITSELF is the
// scrim (a translucent click-catcher over everything), and a sheet — a
// framed column pinned to the right edge — slides in over the host's
// content. Qt Widgets has no built-in drawer, so this is the standard
// construction: an overlay CHILD of the host (not a window), which is what
// makes it move, clip and stack with the host for free.
//
// WHY AN OVERLAY BEATS THE ACCORDION IT REPLACES. An accordion answers
// "show me more" by SHOVING everything below it — the layout jumps, the
// user's visual anchors move, and two accordions stacked (the owner's
// screenshot) turn the panel into a pogo stick. A drawer answers the same
// question by LAYERING: the list appears on top, the panel behind never
// reflows, and dismissing the drawer restores exactly the picture you left.
// Expansion is a NAVIGATION, not a mutation of the page.
//
// OWNERSHIP CONTRACT. The panel owns its chrome (scrim, sheet, title, close
// button, scroll area); the CALLER owns the content — clearContent() then
// fill contentLayout() with rows, exactly like filling any layout. The
// panel never inspects what it carries: it is a container with an entrance,
// reusable for any future "more, aside" need.
// ---------------------------------------------------------------------------

#include <QWidget>

class QFrame;
class QLabel;
class QPropertyAnimation;
class QVBoxLayout;

class SlidePanel : public QWidget
{
    Q_OBJECT

public:
    explicit SlidePanel(QWidget* host);

    void setTitle(const QString& title);

    // Re-purpose the header button. By default it is a ✕ that closes the
    // sheet; a caller with a better use for that corner can take it, as long
    // as the sheet stays dismissible some other way — the scrim, Esc/Back, or
    // the control that opened it. Passing an empty text restores the ✕.
    void setHeaderButton(const QString& text, const QString& tooltip);
    QVBoxLayout* contentLayout() const { return m_content; }
    void clearContent();

    void open();       // idempotent: opening an open panel does nothing
    void closePanel(); // animates out, then hides; emits closed()
    bool isOpen() const { return m_open; }

signals:
    // Emitted for EVERY way out — ✕, scrim tap, Esc — so the owner keeps
    // one piece of "is it open" state in one place.
    void closed();

    // Emitted on open AND on close. An owner needs this to move widgets that
    // must not float above a sheet but live in a DIFFERENT parent, where
    // raise() cannot reach them — the capture FAB is exactly that case.
    void openStateChanged(bool open);

    // Only emitted when the header button has been re-purposed; the default
    // ✕ closes the sheet itself and reports nothing.
    void headerButtonClicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override; // scrim tap = close
    void paintEvent(QPaintEvent* event) override;      // the translucency

private:
    void placeSheet(bool shown);

    QWidget*  m_host;
    QFrame*   m_sheet   = nullptr;
    class QPushButton* m_headerBtn = nullptr;
    bool      m_headerBtnCloses = true;
    QLabel*   m_title   = nullptr;
    QVBoxLayout* m_content = nullptr;
    QPropertyAnimation* m_anim = nullptr;
    bool m_open = false;
};
