#pragma once
// ---------------------------------------------------------------------------
// TaskRow — one task as a row: checkbox, title, due-date badge, delete.
//
// WHY THIS FILE EXISTS — the "second consumer" rule: this row lived
// inline in ActivitiesPage until the Upcoming page needed the identical
// row. One consumer: keep it inline. Two consumers: extract, because two
// copies drift apart the day one of them gets a bug fix the other never
// hears about. (Extracting at ONE consumer is speculative abstraction —
// just as much a smell in the other direction.)
//
// The row is a thin shell in the EventDialog tradition: every control
// forwards to AppData and holds no truth of its own. The page that owns
// it rebuilds on changed(), so the row never even needs a refresh method
// — it is born correct and dies at the next rebuild.
// ---------------------------------------------------------------------------

#include <QWidget>

class AppData;
struct Task;

class TaskRow : public QWidget
{
public:
    // showCategoryDot: the Upcoming page mixes tasks from every life area,
    // so each row states its area with the coloured dot; inside the
    // Activities detail pane the area is already the headline, and
    // repeating it on every row would be noise.
    TaskRow(AppData* data, const Task& task, bool showCategoryDot,
            QWidget* parent = nullptr);
};
