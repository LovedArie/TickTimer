#pragma once
// ---------------------------------------------------------------------------
// GlancePanel — the day view's right-hand sidebar: focused/break stat boxes,
// per-category bars, and the calm encouragement line.
//
// "Calculated live from what you tracked — nothing is stored twice." That
// sentence from the prototype is literally this panel's contract: every
// number here is re-derived (stats::summarizeDay) on every refresh. The
// panel has no state to get stale.
//
// The Supplementary Spec's headline usability rule — calm and NON-SHAMING —
// lives here in code: the encouragement line never scolds. An empty day is
// "a fresh start", never "you did nothing".
// ---------------------------------------------------------------------------

#include <QDate>
#include <QFrame>

class AppData;
class TrackerService;
class StatBox;
class QLabel;
class CategoryBars;

class GlancePanel : public QFrame
{
    Q_OBJECT

public:
    GlancePanel(const AppData* data, const TrackerService* tracker,
                QWidget* parent = nullptr);

    void setDate(QDate date);

public slots:
    void refresh(); // recompute everything from the raw data

private:
    const AppData*        m_data;
    const TrackerService* m_tracker;
    QDate m_date;

    StatBox*      m_focusBox      = nullptr;
    StatBox*      m_breakBox      = nullptr;
    StatBox*      m_distractedBox = nullptr;
    class CategoryPie* m_pie = nullptr;   // day's split, colours match rows
    CategoryBars* m_bars       = nullptr;
    QLabel*       m_encourage  = nullptr;
};

// The per-category horizontal bars — a second, smaller custom-painted
// widget. Same three-step pattern as AgendaWidget, minus mouse handling.
class CategoryBars : public QWidget
{
    Q_OBJECT

public:
    struct Row
    {
        QString name;
        QColor  color;
        qint64  seconds = 0;
    };

    explicit CategoryBars(QWidget* parent = nullptr);
    void setRows(QVector<Row> rows);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    QVector<Row> m_rows;
};
