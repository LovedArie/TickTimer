#include "WeekAgendaView.h"

#include "AgendaWidget.h"
#include "AppData.h"
#include "Event.h" // plan:: slot constants
#include "Stats.h" // stats::weekStart — the one week-snap formula
#include "Theme.h"
#include "Widgets.h" // timeLabel, scaledFont

#include <QGridLayout>
#include <QLabel>
#include <QPainter>

namespace
{
constexpr int kDays = 7;

// The shared hour-label axis on the left. A plain paint widget (no Q_OBJECT,
// no signals) with a SINGLE consumer — this view — so it lives here,
// file-local, not a public class. It reuses AgendaWidget's public grid
// constants so its labels sit exactly on the columns' hour lines.
class AgendaAxis : public QWidget
{
public:
    explicit AgendaAxis(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedWidth(AgendaWidget::gutterWidth(isCompactScreen()));
        setMinimumHeight(sizeHint().height());
    }

    // The axis is TOLD its window by the view — it must show exactly the
    // range the columns show, and only the view knows the seven-day union.
    void setWindow(int startMinutes, int endMinutes)
    {
        if (m_start == startMinutes && m_end == endMinutes)
            return;
        m_start = startMinutes;
        m_end   = endMinutes;
        setMinimumHeight(sizeHint().height());
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const int slotCount = (m_end - m_start) / plan::kSlotMinutes; // NB: "slots" is a Qt macro — never a variable name
        return {AgendaWidget::gutterWidth(isCompactScreen()),
                AgendaWidget::kTopPad
                    + slotCount * AgendaWidget::slotHeight() + 12};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), theme::surface()); // own every pixel we show
        p.setFont(scaledFont(font(), -1.5));
        p.setPen(theme::inkSoft());
        const int slotCount = (m_end - m_start) / plan::kSlotMinutes; // NB: "slots" is a Qt macro — never a variable name
        for (int i = 0; i < slotCount; ++i) {
            const int minutes = m_start + i * plan::kSlotMinutes;
            if (minutes % 60 != 0)
                continue; // labels on the hour only, wherever the window starts
            const int y = AgendaWidget::kTopPad + i * AgendaWidget::slotHeight();
            p.drawText(QRect(0, y - 8, width() - 10, 16),
                       Qt::AlignRight | Qt::AlignVCenter,
                       timeLabel(minutes).remove(":00")); // "6 AM"
        }
    }

private:
    int m_start = plan::kDayStartMinutes; // full day by default, like the
    int m_end   = plan::kDayEndMinutes;   // columns it serves
};
} // namespace

WeekAgendaView::WeekAgendaView(const AppData* data,
                               const TrackerService* tracker, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
    , m_tracker(tracker)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(1);
    grid->setVerticalSpacing(6);

    m_axis = new AgendaAxis(this);
    grid->addWidget(m_axis, 1, 0);
    grid->setColumnStretch(0, 0); // axis: fixed width, no stretch

    for (int d = 0; d < kDays; ++d) {
        auto* header = new QLabel(this);
        header->setAlignment(Qt::AlignCenter);
        m_headers.append(header);
        grid->addWidget(header, 0, d + 1);

        auto* col = new AgendaWidget(m_data, m_tracker, this);
        col->setGutter(0); // labels come from the shared axis, not each column
        m_columns.append(col);
        grid->addWidget(col, 1, d + 1);
        grid->setColumnStretch(d + 1, 1); // seven equal day columns

        // The reuse payoff: the column reports "slot N clicked" knowing
        // nothing about which day it is. We tag it with THIS column's date
        // and re-emit. Column stays date-agnostic; view stays dialog-agnostic.
        connect(col, &AgendaWidget::emptySlotClicked, this, [this, d](int slot) {
            emit emptySlotClicked(m_weekStart.addDays(d), slot);
        });
        connect(col, &AgendaWidget::eventClicked,
                this, &WeekAgendaView::eventClicked); // id is enough, pass it on
        connect(col, &AgendaWidget::eventResized,
                this, &WeekAgendaView::eventResized); // resize a block in any day
    }

    // When the data changes, the seven-day UNION window may change (a block
    // landed outside it) — recompute so the axis and all columns move
    // together. Each column also repaints itself on changed(); this hook is
    // about the shared frame, not the pixels inside it.
    connect(m_data, &AppData::changed, this,
            &WeekAgendaView::applyWindow);

    setDate(QDate::currentDate());
}

void WeekAgendaView::setVisibleWindow(int startMinutes, int endMinutes)
{
    if (m_prefStart == startMinutes && m_prefEnd == endMinutes)
        return;
    m_prefStart = startMinutes;
    m_prefEnd   = endMinutes;
    applyWindow();
}

void WeekAgendaView::setFirstDayOfWeek(Qt::DayOfWeek day)
{
    if (m_firstDay == day)
        return;
    m_firstDay = day;
    // Re-snap around a day we're certainly showing, under the new rule.
    // Force the re-snap even if the resulting start is unchanged elsewhere:
    const QDate anchor = m_weekStart.addDays(3); // mid-week, safely inside
    m_weekStart = QDate();                        // defeat setDate's no-op
    setDate(anchor);
}

void WeekAgendaView::applyWindow()
{
    // The alignment rule of every multi-column screen: sibling columns must
    // share ONE window or 9 AM stops being one horizontal line. Each day
    // could stretch differently (its own out-of-window blocks), so the view
    // takes the UNION across the seven days — computed by the SAME
    // windowCovering every single agenda uses, just folded seven times.
    int start = plan::kDayEndMinutes, end = plan::kDayStartMinutes;
    for (int d = 0; d < m_columns.size(); ++d) {
        const auto w = AgendaWidget::windowCovering(
            m_data, m_weekStart.addDays(d), m_prefStart, m_prefEnd);
        start = qMin(start, w.first);
        end   = qMax(end, w.second);
    }
    // m_axis is stored as QWidget* because AgendaAxis is deliberately
    // file-local (single consumer). The cast is safe by construction: this
    // .cpp is the only code that ever assigns m_axis, three screens up.
    static_cast<AgendaAxis*>(m_axis)->setWindow(start, end);
    for (auto* col : m_columns)
        col->setVisibleWindow(start, end); // union ⊇ each day's own needs,
                                           // so per-column stretching adds 0
}

void WeekAgendaView::setDate(QDate anyDayInWeek)
{
    // Snap to the week's first day — Monday by default, the user's
    // preference when a page has said otherwise. One shared formula
    // (stats::weekStart) so this view, the review's totals, and the "Week
    // of …" label can never disagree about what "this week" means.
    m_weekStart = stats::weekStart(anyDayInWeek, m_firstDay);
    for (int d = 0; d < m_columns.size(); ++d)
        m_columns[d]->setDate(m_weekStart.addDays(d));
    relabelHeaders();
    applyWindow(); // a new set of dates can mean a new union window
}

void WeekAgendaView::relabelHeaders()
{
    const QDate today = QDate::currentDate();
    for (int d = 0; d < m_headers.size(); ++d) {
        const QDate date = m_weekStart.addDays(d);
        const bool isToday = (date == today);
        m_headers[d]->setText(QStringLiteral("%1 %2")
                                  .arg(date.toString("ddd"))
                                  .arg(date.day()));
        // Today stands out in the focus colour; the rest stay quiet.
        m_headers[d]->setStyleSheet(
            QStringLiteral("font-size:12px; font-weight:%1; color:%2;")
                .arg(isToday ? 800 : 600)
                .arg((isToday ? theme::focus() : theme::inkSoft()).name()));
    }
}

void WeekAgendaView::setShowTaskDescriptions(bool show)
{
    for (auto* col : m_columns)
        col->setShowTaskDescriptions(show);
}
