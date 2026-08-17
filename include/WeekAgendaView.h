#pragma once
// ---------------------------------------------------------------------------
// WeekAgendaView — a seven-day timeline built by REUSING AgendaWidget seven
// times (one column per day) behind a single shared time-axis.
//
// This is the payoff of AgendaWidget's design. Because each column only
// REPORTS clicks via signals and never touches the app, we can tile seven of
// them and let the page decide what a click means — here, "plan on THIS
// column's date". A widget that opened its own dialog could never be reused
// like this. (See AgendaWidget.h.)
//
// Same discipline, one level up: this view stays ignorant too. It emits
// emptySlotClicked(date, slot) and eventClicked(id); it never opens a dialog
// or mutates data. The PlannerPage wires those to the very same planning path
// the single-day agenda uses — one planning rule, two views.
// ---------------------------------------------------------------------------

#include <QDate>
#include <QVector>
#include <QWidget>

class AppData;
class TrackerService;
class AgendaWidget;
class QLabel;

class WeekAgendaView : public QWidget
{
    Q_OBJECT

public:
    WeekAgendaView(const AppData* data, const TrackerService* tracker,
                   QWidget* parent = nullptr);

    void setDate(QDate anyDayInWeek); // snaps to the Monday-first week

signals:
    void emptySlotClicked(QDate date, int slotIndex); // "plan on that day"
    void eventClicked(const QString& eventId);         // "open this block"
    void eventResized(const QString& eventId, int newStartMin, int newEndMin);

private:
    void relabelHeaders();

    const AppData*        m_data;
    const TrackerService* m_tracker;
    QDate m_weekStart;                 // Monday of the shown week
    QVector<AgendaWidget*> m_columns;  // 7, Monday..Sunday
public:
    // Forwarded display preference — one call fans out to all seven columns.
    // Declared here, DEFINED in the .cpp: this header only forward-declares
    // AgendaWidget, and calling a member needs the complete type. Keeping
    // the include out of the header is deliberate (fewer rebuild ripples);
    // the price is that method bodies touching AgendaWidget live in the .cpp.
    void setShowTaskDescriptions(bool show);

    // Display preferences (settings addendum), told by the page like the one
    // above. The window is NOT simply forwarded to the columns: seven days
    // could each stretch differently over their own blocks, so the view
    // computes the seven-day UNION and hands every column (and the shared
    // axis) the same range — alignment is this screen's whole reason to
    // exist, so it is enforced here, in the one place that can see all
    // seven days at once.
    void setVisibleWindow(int startMinutes, int endMinutes);
    void setFirstDayOfWeek(Qt::DayOfWeek day);
private:
    void applyWindow(); // recompute the union; move axis + columns together

    QVector<QLabel*>       m_headers;  // 7 day headers above the columns
    QWidget*               m_axis = nullptr; // the shared hour axis (set once)
    Qt::DayOfWeek          m_firstDay  = Qt::Monday;
    int                    m_prefStart = 6 * 60;  // preference window; the
    int                    m_prefEnd   = 24 * 60; // union never shows less
};
