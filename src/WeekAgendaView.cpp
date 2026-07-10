#include "WeekAgendaView.h"

#include "AgendaWidget.h"
#include "AppData.h"
#include "Event.h" // plan:: slot constants
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
        setFixedWidth(AgendaWidget::kDefaultGutter);
        setMinimumHeight(sizeHint().height());
    }

    QSize sizeHint() const override
    {
        return {AgendaWidget::kDefaultGutter,
                AgendaWidget::kTopPad
                    + plan::kSlotsPerDay * AgendaWidget::kSlotHeight + 12};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), theme::surface()); // own every pixel we show
        p.setFont(scaledFont(font(), -1.5));
        p.setPen(theme::inkSoft());
        for (int i = 0; i < plan::kSlotsPerDay; i += 2) { // on the hour only
            const int y = AgendaWidget::kTopPad + i * AgendaWidget::kSlotHeight;
            const int minutes = plan::kDayStartMinutes + i * plan::kSlotMinutes;
            p.drawText(QRect(0, y - 8, width() - 10, 16),
                       Qt::AlignRight | Qt::AlignVCenter,
                       timeLabel(minutes).remove(":00")); // "6 AM"
        }
    }
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

    grid->addWidget(new AgendaAxis(this), 1, 0);
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

    // Columns paint straight from AppData; when the data changes, repaint them.
    connect(m_data, &AppData::changed, this, [this]() {
        for (auto* col : m_columns)
            col->update();
    });

    setDate(QDate::currentDate());
}

void WeekAgendaView::setDate(QDate anyDayInWeek)
{
    // Snap to Monday (dayOfWeek: 1 = Mon .. 7 = Sun) so the seven columns line
    // up with the Monday-first week the stats panel below uses.
    m_weekStart = anyDayInWeek.addDays(-(anyDayInWeek.dayOfWeek() - 1));
    for (int d = 0; d < m_columns.size(); ++d)
        m_columns[d]->setDate(m_weekStart.addDays(d));
    relabelHeaders();
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
