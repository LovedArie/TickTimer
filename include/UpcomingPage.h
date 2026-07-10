#pragma once
// ---------------------------------------------------------------------------
// UpcomingPage — every dated, unfinished task across all life areas,
// grouped Overdue / This week / Later (addendum §3.13).
//
// Read this file knowing what it does NOT contain: no new data, no save
// code, no state beyond a pointer to AppData. It is the app's fourth
// DERIVED VIEW (after the glance panel and the week/month reviews) —
// a question asked fresh on every change, never a table kept in sync.
// The grouping thresholds are VIEW logic (what "this week" means on
// screen), so they live here, not in the domain.
// ---------------------------------------------------------------------------

#include <QWidget>

class AppData;
struct Task;
class QScrollArea;

class UpcomingPage : public QWidget
{
    Q_OBJECT

public:
    explicit UpcomingPage(AppData* data, QWidget* parent = nullptr);

public slots:
    void rebuild();

private:
    QWidget* buildContent();
    // One task drawn as a Special-Days-style CARD: category-coloured accent,
    // a large title, and the deadline as a countdown headline. Built inline
    // here (not a shared widget class) because Upcoming is its only consumer —
    // extracting for one caller would be speculative abstraction.
    QWidget* buildTaskCard(const Task* task, QWidget* parent);

    AppData*     m_data;
    QScrollArea* m_scroll = nullptr;
};
