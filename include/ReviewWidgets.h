#pragma once
// ---------------------------------------------------------------------------
// ReviewWidgets — the week and month reviews (design-doc §6, step 6; the
// prototype's "sample data" panels made real).
//
// The prototype had to fake these with hard-coded numbers; here they come
// from stats::summarizeWeek / summarizeMonth — the SAME raw Segments the
// day view uses. Track a block today and this week's bar grows instantly.
// That is "derive, don't store" delivering its promised payoff: the
// reviews needed zero new data, only new questions asked of old data.
//
// Charts are hand-painted with QPainter (no chart library): a stacked bar
// chart and a pie are a few dozen lines each, and you learn more geometry
// than any library would teach you.
// ---------------------------------------------------------------------------

#include "Stats.h"

#include <QDate>
#include <QWidget>

class AppData;
class StatBox;
class QLabel;
class QVBoxLayout;

// -- week: stacked focus/break bar per day ------------------------------------
class WeekBarsChart : public QWidget
{
public:
    explicit WeekBarsChart(QWidget* parent = nullptr);
    void setDays(QVector<QPair<QDate, stats::Totals>> days);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return {420, 190}; }

private:
    QVector<QPair<QDate, stats::Totals>> m_days;
};

// -- share of tracked time per life area ---------------------------------------
class CategoryPie : public QWidget
{
public:
    struct Slice { QColor color; qint64 seconds = 0; };

    explicit CategoryPie(QWidget* parent = nullptr);
    void setSlices(QVector<Slice> slices);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return {140, 140}; }

private:
    QVector<Slice> m_slices;
};

// -- month: one dot per day, sized/coloured by focus level ---------------------
class MonthGrid : public QWidget
{
public:
    explicit MonthGrid(QWidget* parent = nullptr);
    void setMonth(QDate anyDayInMonth,
                  QVector<QPair<QDate, stats::Totals>> days);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    QDate m_month;
    QVector<QPair<QDate, stats::Totals>> m_days;
};

// -- the two assembled review pages ---------------------------------------------
class WeekReviewPage : public QWidget
{
    Q_OBJECT
public:
    explicit WeekReviewPage(const AppData* data, QWidget* parent = nullptr);
    void setDate(QDate anyDayInWeek);
public slots:
    void refresh();
private:
    const AppData* m_data;
    QDate m_date;
    StatBox* m_focusBox = nullptr;
    StatBox* m_breakBox = nullptr;
    StatBox* m_distractedBox = nullptr;
    WeekBarsChart* m_bars = nullptr;
    CategoryPie*   m_pie  = nullptr;
    QVBoxLayout*   m_legend = nullptr;
};

class MonthReviewPage : public QWidget
{
    Q_OBJECT
public:
    explicit MonthReviewPage(const AppData* data, QWidget* parent = nullptr);
    void setDate(QDate anyDayInMonth);
public slots:
    void refresh();
private:
    const AppData* m_data;
    QDate m_date;
    StatBox*   m_focusBox = nullptr;
    StatBox*   m_distractedBox = nullptr;
    MonthGrid* m_grid = nullptr;
};
