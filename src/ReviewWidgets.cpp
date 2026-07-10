#include "ReviewWidgets.h"

#include "Theme.h"
#include "Widgets.h"
#include "AppData.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

// ---- WeekBarsChart -----------------------------------------------------------

WeekBarsChart::WeekBarsChart(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(190);
}

void WeekBarsChart::setDays(QVector<QPair<QDate, stats::Totals>> days)
{
    m_days = std::move(days);
    update();
}

void WeekBarsChart::paintEvent(QPaintEvent*)
{
    if (m_days.isEmpty())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(scaledFont(font(), -1));

    // Scale every bar to the busiest day, so the chart reads relatively.
    qint64 maxTotal = 1;
    for (const auto& d : m_days)
        maxTotal = qMax(maxTotal, d.second.total());

    const int labelBand = 20;
    const int chartH = height() - labelBand;
    const int colW = width() / m_days.size();
    const int barW = qMin(46, colW * 6 / 10);

    for (int i = 0; i < m_days.size(); ++i) {
        const stats::Totals& t = m_days[i].second;
        const int x = i * colW + (colW - barW) / 2;

        const int totalH = int(qint64(chartH - 8) * t.total() / maxTotal);
        const int focusH = t.total() > 0
                               ? int(qint64(totalH) * t.focusSeconds / t.total())
                               : 0;

        // Stacked: focus (green) grows from the floor, break (amber) on top.
        p.setPen(Qt::NoPen);
        if (focusH > 0) {
            p.setBrush(theme::focus());
            p.drawRoundedRect(QRect(x, chartH - focusH, barW, focusH), 4, 4);
        }
        if (totalH - focusH > 0) {
            p.setBrush(theme::brk());
            p.drawRoundedRect(
                QRect(x, chartH - totalH, barW, totalH - focusH), 4, 4);
        }

        p.setPen(theme::inkSoft());
        p.drawText(QRect(i * colW, chartH + 2, colW, labelBand),
                   Qt::AlignHCenter | Qt::AlignTop,
                   m_days[i].first.toString("ddd"));
    }
}

// ---- CategoryPie -------------------------------------------------------------

CategoryPie::CategoryPie(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void CategoryPie::setSlices(QVector<Slice> slices)
{
    m_slices = std::move(slices);
    update();
}

void CategoryPie::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    qint64 total = 0;
    for (const Slice& s : m_slices)
        total += s.seconds;

    const QRect r = rect().adjusted(2, 2, -2, -2);
    if (total == 0) { // nothing tracked yet — an honest empty ring
        p.setBrush(theme::track());
        p.drawEllipse(r);
        return;
    }

    // QPainter::drawPie counts angles in 1/16th of a degree, starting at
    // 3 o'clock, counter-clockwise. We start at 12 o'clock (90°) like the
    // prototype's conic-gradient and walk clockwise (negative spans).
    int startAngle = 90 * 16;
    for (const Slice& s : m_slices) {
        if (s.seconds <= 0)
            continue;
        const int span = int(-360.0 * 16.0 * double(s.seconds) / double(total));
        p.setBrush(s.color);
        p.drawPie(r, startAngle, span);
        startAngle += span;
    }
}

// ---- MonthGrid ----------------------------------------------------------------

namespace
{
constexpr int kCell = 44;
constexpr int kGap  = 6;
constexpr int kHeaderBand = 18;

// Focus level thresholds — same idea as the prototype's four dot levels.
int levelFor(qint64 focusSeconds)
{
    if (focusSeconds <= 0)            return 0;
    if (focusSeconds < 1 * 3600)      return 1; // under an hour
    if (focusSeconds < 3 * 3600)      return 2; // 1–3 h
    return 3;                                    // 3 h+ — a deep day
}
} // namespace

MonthGrid::MonthGrid(QWidget* parent)
    : QWidget(parent)
{
}

void MonthGrid::setMonth(QDate anyDayInMonth,
                         QVector<QPair<QDate, stats::Totals>> days)
{
    m_month = anyDayInMonth;
    m_days = std::move(days);
    updateGeometry();
    update();
}

QSize MonthGrid::sizeHint() const
{
    if (!m_month.isValid())
        return {7 * (kCell + kGap), 6 * (kCell + kGap) + kHeaderBand};
    const QDate first(m_month.year(), m_month.month(), 1);
    const int startColumn = first.dayOfWeek() - 1; // Monday-first grid
    const int rows = (startColumn + first.daysInMonth() + 6) / 7;
    return {7 * (kCell + kGap), rows * (kCell + kGap) + kHeaderBand};
}

void MonthGrid::paintEvent(QPaintEvent*)
{
    if (!m_month.isValid())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(scaledFont(font(), -1.5));

    static const char* dows[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    p.setPen(theme::inkSoft());
    for (int c = 0; c < 7; ++c)
        p.drawText(QRect(c * (kCell + kGap), 0, kCell, kHeaderBand),
                   Qt::AlignCenter, dows[c]);

    const QColor levelColors[] = {QColor("#DFE3DD"), theme::brk(),
                                  QColor("#7FC3B4"), theme::focus()};
    const int levelSizes[] = {7, 10, 13, 17};

    const QDate first(m_month.year(), m_month.month(), 1);
    const int startColumn = first.dayOfWeek() - 1;

    for (int day = 1; day <= first.daysInMonth(); ++day) {
        const int index = startColumn + day - 1;
        const int col = index % 7, row = index / 7;
        const QRect cell(col * (kCell + kGap),
                         kHeaderBand + row * (kCell + kGap), kCell, kCell);

        p.setPen(theme::line());
        p.setBrush(theme::surface());
        p.drawRoundedRect(cell, 8, 8);

        p.setPen(theme::inkSoft());
        p.drawText(cell.adjusted(6, 4, -4, -4),
                   Qt::AlignLeft | Qt::AlignTop, QString::number(day));

        // byDay from summarizeMonth is indexed from the 1st — day N is
        // simply entry N-1. Positional agreement like this deserves the
        // comment it just got; silent index math is where bugs hide.
        const qint64 focus = (day - 1) < m_days.size()
                                 ? m_days[day - 1].second.focusSeconds
                                 : 0;
        const int lvl = levelFor(focus);
        const int d = levelSizes[lvl];
        p.setPen(Qt::NoPen);
        p.setBrush(levelColors[lvl]);
        p.drawEllipse(cell.center() + QPoint(0, 6), d / 2, d / 2);
    }
}

// ---- WeekReviewPage -------------------------------------------------------------

WeekReviewPage::WeekReviewPage(const AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
    , m_date(QDate::currentDate())
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 18, 26, 22);
    layout->setSpacing(14);

    m_focusBox = new StatBox(tr("Focused this week"), theme::focus(), this);
    m_breakBox = new StatBox(tr("Break"), theme::brk(), this);
    m_distractedBox = new StatBox(tr("Distracted"), theme::danger(), this);
    auto* statRow = new QHBoxLayout;
    statRow->addWidget(m_focusBox);
    statRow->addWidget(m_breakBox);
    statRow->addWidget(m_distractedBox);
    statRow->addStretch(1);

    m_bars = new WeekBarsChart(this);

    m_pie = new CategoryPie(this);
    m_legend = new QVBoxLayout;
    m_legend->setSpacing(5);
    auto* pieRow = new QHBoxLayout;
    pieRow->setSpacing(22);
    pieRow->addWidget(m_pie);
    pieRow->addLayout(m_legend);
    pieRow->addStretch(1);

    layout->addLayout(statRow);
    layout->addWidget(m_bars);
    layout->addLayout(pieRow);
    layout->addStretch(1);

    connect(m_data, &AppData::changed, this, &WeekReviewPage::refresh);
    refresh();
}

void WeekReviewPage::setDate(QDate anyDayInWeek)
{
    m_date = anyDayInWeek;
    refresh();
}

void WeekReviewPage::refresh()
{
    const stats::PeriodSummary s = stats::summarizeWeek(*m_data, m_date);

    m_focusBox->setValue(stats::formatSeconds(s.totals.focusSeconds));
    m_breakBox->setValue(stats::formatSeconds(s.totals.breakSeconds));
    m_distractedBox->setValue(
        stats::formatSeconds(s.totals.distractedSeconds));
    m_bars->setDays(s.byDay);

    QVector<CategoryPie::Slice> slices;
    // Clear and rebuild the legend (small enough to rebuild wholesale).
    // Subtlety worth knowing: deleting a QLayout does NOT delete the
    // widgets it managed — they are children of the page, not the layout.
    // So teardown must walk nested items and delete widgets explicitly.
    std::function<void(QLayout*)> clearLayout = [&](QLayout* layout) {
        while (QLayoutItem* item = layout->takeAt(0)) {
            delete item->widget();               // null-safe: delete nullptr is a no-op
            if (QLayout* child = item->layout())
                clearLayout(child);
            delete item;
        }
    };
    clearLayout(m_legend);
    for (const Category& c : m_data->categories()) {
        const qint64 secs = s.byCategory.value(c.id, 0);
        if (secs <= 0)
            continue;
        slices.append({c.color, secs});
        const int pct = s.totals.total() > 0
                            ? int(secs * 100 / s.totals.total())
                            : 0;
        auto* row = new QLabel(
            QStringLiteral("%1  —  %2 (%3%)")
                .arg(c.name, stats::formatSeconds(secs))
                .arg(pct),
            this);
        row->setStyleSheet("font-size:12px;");
        auto* icon = new QLabel(this);
        icon->setPixmap(colorDot(c.color, 9));
        auto* line = new QHBoxLayout;
        line->setSpacing(7);
        line->addWidget(icon);
        line->addWidget(row);
        line->addStretch(1);
        m_legend->addLayout(line);
    }

    // The week's books must balance: categories now hold FOCUS time only
    // (§3.37), so break and drift join the pie as their own slices — the
    // whole week accounted for, nothing hiding inside a life area. Same
    // helper-lambda shape as the category rows, same amber/danger hues as
    // everywhere else.
    const auto addSink = [&](const QString& name, const QColor& color,
                             qint64 secs) {
        if (secs <= 0)
            return;
        slices.append({color, secs});
        const int pct = s.totals.total() > 0
                            ? int(secs * 100 / s.totals.total())
                            : 0;
        auto* row = new QLabel(QStringLiteral("%1  —  %2 (%3%)")
                                   .arg(name, stats::formatSeconds(secs))
                                   .arg(pct),
                               this);
        row->setStyleSheet("font-size:12px;");
        auto* icon = new QLabel(this);
        icon->setPixmap(colorDot(color, 9));
        auto* line = new QHBoxLayout;
        line->setSpacing(7);
        line->addWidget(icon);
        line->addWidget(row);
        line->addStretch(1);
        m_legend->addLayout(line);
    };
    addSink(tr("Break"), theme::brk(), s.totals.breakSeconds);
    addSink(tr("Distracted"), theme::danger(), s.totals.distractedSeconds);

    m_pie->setSlices(std::move(slices));
}

// ---- MonthReviewPage ---------------------------------------------------------------

MonthReviewPage::MonthReviewPage(const AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
    , m_date(QDate::currentDate())
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 18, 26, 22);
    layout->setSpacing(14);

    m_focusBox = new StatBox(tr("Focused this month"), theme::focus(), this);
    // Focus next to distraction at month scale — the app's thesis in one
    // row. (Break is deliberately absent here: at a month's distance, rest
    // is fine and needs no audit; drift is the number worth watching.)
    m_distractedBox = new StatBox(tr("Distracted"), theme::danger(), this);
    auto* statRow = new QHBoxLayout;
    statRow->addWidget(m_focusBox);
    statRow->addWidget(m_distractedBox);
    statRow->addStretch(2);

    m_grid = new MonthGrid(this);

    auto* hint = new QLabel(
        tr("Each dot is a day's focus level — bigger and greener means "
           "deeper focus."),
        this);
    hint->setObjectName("sub");

    layout->addLayout(statRow);
    layout->addWidget(m_grid, 0, Qt::AlignLeft);
    layout->addWidget(hint);
    layout->addStretch(1);

    connect(m_data, &AppData::changed, this, &MonthReviewPage::refresh);
    refresh();
}

void MonthReviewPage::setDate(QDate anyDayInMonth)
{
    m_date = anyDayInMonth;
    refresh();
}

void MonthReviewPage::refresh()
{
    const stats::PeriodSummary s = stats::summarizeMonth(*m_data, m_date);
    m_focusBox->setValue(stats::formatSeconds(s.totals.focusSeconds));
    m_distractedBox->setValue(
        stats::formatSeconds(s.totals.distractedSeconds));
    m_grid->setMonth(m_date, s.byDay);
}
