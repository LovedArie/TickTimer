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

#include <QDate>
#include <QWidget>

class AppData;
class TrackerService;
class AgendaWidget;
class WeekAgendaView;
class GlancePanel;
class WeekReviewPage;
class MonthReviewPage;
class QLabel;
class QPushButton;
class QStackedWidget;
class QFrame;
class QVBoxLayout;

class PlannerPage : public QWidget
{
    Q_OBJECT

public:
    PlannerPage(AppData* data, TrackerService* tracker,
                QWidget* parent = nullptr);

private slots:
    void shiftPeriod(int direction);   // -1 = previous, +1 = next
    void setMode(int mode);            // 0 day, 1 week, 2 month
    void onEmptySlotClicked(int slotIndex);
    void onEventClicked(const QString& eventId);
    // A drag reported a new span; route it through the domain guard, which
    // enforces the rules and can refuse. The widget already clamped the
    // preview, so this normally succeeds — belt (UI) and suspenders (domain).
    void onEventResized(const QString& id, int startMin, int endMin);
    // Plan an event on a GIVEN date at a given slot. The single planning
    // step, shared by the day view (m_date) and every week column (its own
    // date) — one rule, two callers, no divergence.
    void planAt(QDate date, int slotIndex);
    void refresh();

private:
    void updateViewSwitcher();
    // Repopulate the "Due today" strip for the currently-viewed day. This
    // is the read-only bridge from Tasks to the Calendar: it SHOWS what is
    // due, it does not place tasks onto time blocks (that remains deferred —
    // see the task-details addendum). Derived on every rebuild, never stored.
    void rebuildDueStrip();

    AppData*        m_data;
    TrackerService* m_tracker;
    QDate m_date;                      // the day (or a day inside the
    int   m_mode = 0;                  //  week/month) being viewed

    QPushButton*    m_viewSwitcher = nullptr; // shows the period; click cycles the view
    QStackedWidget* m_stack       = nullptr;
    QFrame*         m_duePanel    = nullptr;  // "Due today" strip (day view)
    QVBoxLayout*    m_dueLayout   = nullptr;  // its rebuildable content
    AgendaWidget*   m_agenda      = nullptr;
    WeekAgendaView* m_weekAgenda  = nullptr;
    GlancePanel*    m_glance      = nullptr;
    WeekReviewPage* m_week        = nullptr;
    MonthReviewPage* m_month      = nullptr;
};
