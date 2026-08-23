#pragma once
// ---------------------------------------------------------------------------
// PlannerPage — the Calendar tab. It composes the pieces you've already
// read (AgendaWidget, GlancePanel, WeekReviewPage, MonthReviewPage) and
// adds the top bar: ‹ date › navigation plus the Day/Week/Month switch.
//
// This class is a COORDINATOR, and that is a role worth recognizing: it
// owns almost no logic of its own. It holds the current date, decides
// which sub-view is visible, and translates the agenda's signals ("slot 7
// was clicked") into use-case steps ("open the picker; if accepted, ask
// AppData to add the event"). Everything else is delegated.
//
// UC1 lives here end to end: click a free slot -> PickActivityDialog ->
// AppData::addEvent -> changed() -> every widget repaints. Follow that
// chain once in the debugger and you'll understand the whole app's
// update model.
// ---------------------------------------------------------------------------

#include "Event.h"      // BlockOutcome — the catch-up slots' vocabulary
#include "Reschedule.h" // reschedule::Option — the accepted proposal

#include <QDate>
#include "Responsive.h"

#include <QWidget>

class AppData;
class TrackerService;
class AgendaWidget;
class WeekAgendaView;
class GlancePanel;
class WeekReviewPage;
class MonthReviewPage;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QFrame;
class QVBoxLayout;

class PlannerPage : public QWidget
{
    Q_OBJECT

public:
    // The phone's header owns the button that opens this; the page owns the
    // sheet. Keeps the drawer's construction here and its AFFORDANCE where
    // the user's thumb expects it — the top right of the screen, not the top
    // right of a panel.
    void openGlanceDrawer();
    bool hasGlanceDrawer() const { return m_glanceDrawer != nullptr; }

    PlannerPage(AppData* data, TrackerService* tracker,
                QWidget* parent = nullptr);

    // Re-read the display preferences (prefs::) and TELL every widget that
    // cares — the day agenda and week view get the hours window; the week
    // view, week review, month grid, and the "Week of …" label get the
    // week start. Called once from the constructor and again by MainWindow
    // whenever the Settings dialog is accepted. This is the doctrine's
    // choke point: only pages read QSettings; widgets are told (§ the
    // task-notes toggle, same file, same rule).
    void applyDisplayPrefs();

signals:
    // "Something sheet-like is covering me." MainWindow hides the capture FAB
    // while this is true: a floating + over a modal drawer is a button that
    // looks tappable and is not what the user meant. They live in different
    // parents, so z-order cannot separate them.
    void overlayOpenChanged(bool open);

private slots:
    void shiftPeriod(int direction);   // -1 = previous, +1 = next
    // RIGHT-CLICK on the period label (v22, owner request: "otherwise we lose
    // ourselves in the timeline"). Snapping home is a distinct intent from
    // stepping, so it gets a distinct gesture rather than N presses of ‹.
    void goToToday();
    void setMode(int mode);            // 0 day, 1 week, 2 month
    void onEmptySlotClicked(int slotIndex);
    void onEventClicked(const QString& eventId);
    // A drag reported a new span; route it through the domain guard, which
    // enforces the rules and can refuse. The widget already clamped the
    // preview, so this normally succeeds — belt (UI) and suspenders (domain).
    void onEventResized(const QString& id, int startMin, int endMin);
    // Plan an event on a GIVEN date at a given slot. The single planning
    // step, shared by the day view (m_date) and every week column (its own
    // date) — one rule, two callers, no divergence. Placement mode (part 3)
    // intercepts HERE, which is why "Find time" works from both views for
    // the price of one interception.
    void planAt(QDate date, int slotIndex);
    void refresh();

    // ---- needs-a-block: the deciding end of every card signal -------------
    // Named slots (not lambdas) because TWO cards ask now — the glance
    // panel's and the week tab's — and one connection target per action is
    // what keeps them behaviourally identical.
    void onNbPlan(const QString& taskId);          // enter placing mode
    void onNbEditDeadline(const QString& taskId);  // DueDateDialog reuse
    void onNbNotUrgent(const QString& taskId);
    void onNbDismiss(const QString& taskId);
    void onNbBringBack(const QString& taskId);
    void cancelPlacing();

    // ---- catch-up: the deciding end of the card's signals (v26.2 §K) ------
    // Same shape as the five above: the card asks, this page — the holder of
    // the mutable pointer — calls the domain door.
    void onCuAccept(const QString& eventId, const reschedule::Option& option);
    void onCuResolve(const QString& eventId, BlockOutcome outcome);
    void onCuShowDay(QDate date);
    void onCuResolveAll(const QStringList& eventIds, BlockOutcome outcome);

protected:
    // The container's size class reaches the page here. See
    // ResponsiveWatcher.h; the handler only shows and hides.
    bool event(QEvent* e) override;

private:
    void applyLayoutMode(responsive::Mode mode);

    // Push m_date into every sub-view and re-derive the label and strips.
    // Extracted from shiftPeriod() the moment goToToday() became a second
    // caller: one "the date moved" routine, so a new navigation gesture can
    // never forget to update the due strip (the exact bug this shape
    // prevents — the old code had that sequence inline, once).
    void applyDate();
    void updateViewSwitcher();
    // Placement mode (needs-a-block part 3). All DERIVED per refresh from
    // m_placingTaskId + the data — the banner, the day strip, and the
    // agenda's highlight runs have no state of their own to go stale.
    void refreshPlacing();
    QVector<QPair<int, int>> freeRunsFor(QDate date, int minMinutes) const;
    Qt::DayOfWeek m_firstDay = Qt::Monday; // cached pref for the week label
    // Repopulate the "Due today" strip for the currently-viewed day. This
    // is the read-only bridge from Tasks to the Calendar: it SHOWS what is
    // due, it does not place tasks onto time blocks (that remains deferred —
    // see the task-details addendum). Derived on every rebuild, never stored.
    void rebuildDueStrip();

    AppData*        m_data;
    TrackerService* m_tracker;
    QDate m_date;                      // the day (or a day inside the
    int   m_mode = 0;                  //  week/month) being viewed

    QString      m_placingTaskId;      // task being placed; empty = not placing
    QFrame*      m_placingBanner = nullptr; // above the day agenda
    QLabel*      m_placingLabel  = nullptr; // "Placing X — deadline …"
    QHBoxLayout* m_placingStrip  = nullptr; // the rebuildable day buttons
    class NeedsBlockCard* m_weekCard = nullptr; // the SAME derived list,
                                       // rendered where the week view has room

    QPushButton*    m_viewSwitcher = nullptr; // shows the period; click cycles the view
    QStackedWidget* m_stack       = nullptr;
    QFrame*         m_duePanel    = nullptr;  // "Due today" strip (day view)
    QVBoxLayout*    m_dueLayout   = nullptr;  // its rebuildable content
    AgendaWidget*   m_agenda      = nullptr;
    WeekAgendaView* m_weekAgenda  = nullptr;
    GlancePanel*    m_glance      = nullptr;
    // Held so applyLayoutMode() can give the agenda its room back on a phone.
    class QScrollArea* m_agendaScroll = nullptr;
    class QVBoxLayout* m_agendaLayout = nullptr;
    class QLabel*      m_agendaSub    = nullptr; // names the tap-or-hold gesture
    class QVBoxLayout* m_outerLayout  = nullptr; // page margins go to 0 flat
    class QHBoxLayout* m_topBarLayout = nullptr; // keeps the ‹ › off the bezel
    class QFrame*      m_agendaPanel  = nullptr; // loses its card when flat
    class QHBoxLayout* m_agendaHead   = nullptr; // text keeps its inset when
                                                 // the TIMELINE goes flush

    // ---- the phone's glance ------------------------------------------------
    // On a phone the panel lives in a drawer instead of a 320px column, and
    // that home is chosen ONCE at construction. Nothing moves at a mode
    // change, so the "does it come back on a tablet?" problem never arises —
    // the desktop shell simply never put it in a drawer.
    //
    // NEVER call clearContent() on this panel: SlidePanel::clearContent()
    // DELETES what it holds, and the established caller idiom is
    // clear-then-refill. This one is fill-once.
    bool m_phoneShell = false;
    class SlidePanel*  m_glanceDrawer = nullptr;
    class QPushButton* m_attentionStrip = nullptr; // shrinks the day, never
                                                   // covers it
    void refreshAttentionStrip();
    WeekReviewPage* m_week        = nullptr;
    MonthReviewPage* m_month      = nullptr;
};
