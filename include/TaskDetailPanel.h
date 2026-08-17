#pragma once
// ---------------------------------------------------------------------------
// TaskDetailPanel — the task-detail OVERLAY drawer (v28.6; overlay v28.6.1).
//
// v28.6.0 docked this panel INTO the body layout — [nav][pages][panel] —
// and the owner's first real session rejected that shape on sight: the
// panel read as part of the Activities page and stole width from the main
// screen. v28.6.1 is the correction, and the vocabulary matters:
//
//   DOCKED  = a layout member; opening it RESIZES everything else.
//   OVERLAY = a floating child painted IN FRONT; the world keeps its
//             size and loses contrast instead — a scrim (a translucent
//             dim layer) covers the content, the drawer slides over it,
//             and clicking the scrim is clicking "away": it closes.
//
// So the panel now owns two widgets: itself (fixed-width drawer hugging
// the host's right edge, slid in/out by animating its x position) and
// m_scrim (host-sized, rgba-dimmed, click = closePanel). Neither sits in
// a layout; the panel repositions both from an event filter when the
// host resizes. Width went 360 → 440, clamped so a narrow window always
// keeps 220 px of visible content — an overlay that covers everything
// is a modal with extra steps.
//
// EVERY close route runs the same unsaved guard — ✕, Esc, and now the
// scrim click. "Click anywhere else closes" never means "click anywhere
// else discards"; the Save/Discard/Stay prompt stands between a stray
// click and dirty work, same as always.
//
// The explicit-save contract, the dirty tracking, the rebuild-not-reset
// form lifecycle, and the changed() rules are all unchanged from
// v28.6.0 — see design-addendum-detail-panel.md. Hostless construction
// (no overlay, no scrim) is kept for tests that drive the save/guard
// logic without a window.
// ---------------------------------------------------------------------------

#include <QString>
#include <QWidget>

#include <functional>

class AppData;
class TaskDetailForm;
class QLabel;
class QPushButton;
class QPropertyAnimation;
class QScrollArea;
class QToolButton;

class TaskDetailPanel : public QWidget
{
    Q_OBJECT

public:
    // host: the widget this drawer overlays (MainWindow passes the body).
    // Null = hostless mode: plain widget, no scrim, no sliding — the
    // logic tests' harness.
    explicit TaskDetailPanel(AppData* data, QWidget* host = nullptr);

    void openTask(const QString& taskId);
    void closePanel();

    bool    isOpen() const { return m_open; }
    QString currentTaskId() const { return m_taskId; }

    // v28.7 — after openTask on a just-created piece: title focused and
    // fully selected, first keystroke replaces the placeholder.
    void focusTitleForNaming();

    enum class UnsavedChoice { Save, Discard, Stay };
    void setUnsavedPromptForTests(std::function<UnsavedChoice()> prompt)
    {
        m_promptOverride = std::move(prompt);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override; // Esc = closePanel()
    // Watches the HOST for resizes (reposition drawer + scrim) and the
    // SCRIM for clicks (the "anywhere else" close).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildFor(const QString& taskId);
    void saveNow();
    bool resolveUnsavedEdits();
    void updateSaveUi();
    void animateTo(bool open);
    void repositionOverlay(); // host resized while open
    int  panelWidth() const;

    AppData*        m_data = nullptr;
    QWidget*        m_host = nullptr;
    QWidget*        m_scrim = nullptr; // the lower-contrast layer
    QString         m_taskId;
    bool            m_open = false;
    bool            m_applying = false;

    TaskDetailForm* m_form   = nullptr;
    QScrollArea*    m_scroll = nullptr;
    QLabel*         m_header = nullptr;
    QLabel*         m_savedFlash = nullptr;
    QPushButton*    m_save   = nullptr;
    QToolButton*    m_close  = nullptr;
    QPropertyAnimation* m_anim = nullptr;

    std::function<UnsavedChoice()> m_promptOverride;

    static constexpr int kWidth      = 440; // was 360 — "feels tight"
    static constexpr int kKeepClear  = 220; // content never fully covered
};
