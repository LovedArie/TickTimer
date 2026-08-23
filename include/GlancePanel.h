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

#include "Event.h"      // BlockOutcome rides the re-emitted resolve signal
#include "Reschedule.h" // reschedule::Option rides the re-emitted accept

#include <QDate>
#include <QDateTime>
#include <QFrame>

#include <functional>

class AppData;
class CatchUpCard;
class NeedsBlockCard;
class TrackerService;
class StatBox;
class QLabel;
class QWidget;
class CategoryBars;

class GlancePanel : public QFrame
{
    Q_OBJECT

public:
    GlancePanel(const AppData* data, const TrackerService* tracker,
                QWidget* parent = nullptr);

    void setDate(QDate date);

    // The clock seam (needs-a-block part 2) — every gate/flag decision
    // reads THIS, never the wall clock directly, so tests walk the gate
    // through a whole day in microseconds. Same doctrine as
    // TrackerService::nowProvider, and for the same reason: the v19.6
    // "passing for the wrong reason" scar says seams must have no holes.
    std::function<QDateTime()> nowProvider = [] {
        return QDateTime::currentDateTime();
    };

signals:
    // The needs-a-block card's asks, re-emitted upward. This panel is a
    // CONST view of the data (const-correctness as architecture): the
    // card reports, the panel forwards, PlannerPage — holder of the
    // mutable pointer — decides. Wrong calls don't compile here.
    void planTaskRequested(const QString& taskId);
    void editDeadlineRequested(const QString& taskId);
    void notUrgentRequested(const QString& taskId);
    void dismissRequested(const QString& taskId);
    void bringBackRequested(const QString& taskId);

    // The catch-up card's asks, re-emitted the same way (catch-up §K): the
    // card reports, this const panel forwards, PlannerPage decides.
    void catchUpAcceptRequested(const QString& eventId,
                                const reschedule::Option& option);
    void catchUpResolveRequested(const QString& eventId, BlockOutcome outcome);
    void catchUpShowDayRequested(QDate date);
    void catchUpResolveAllRequested(const QStringList& eventIds,
                                    BlockOutcome outcome);

public:
    // Does the panel hold anything that is actively asking for attention —
    // flagged tasks or missed blocks? On a phone the panel lives behind a
    // drawer, and a nudge nobody opens is not a nudge; PlannerPage asks this
    // to decide whether to surface a strip on the calendar itself.
    bool needsAttention() const;

    // Who the cards' slide-over drawers should cover. Normally the panel
    // itself, but inside the phone's glance drawer the panel is a widget in
    // another SlidePanel's scroll area — a sheet sized to it would be sized
    // to the wrong thing. The owner says who gets covered.
    void setCardDrawerHost(QWidget* host);

    // Hide the panel's own "At a glance" heading. Inside the phone's drawer
    // the sheet is already titled that, and saying it twice in 40px of
    // vertical space is just noise.
    void setHeadingVisible(bool on);

public slots:
    void refresh(); // recompute everything from the raw data

private:
    const AppData*        m_data;
    const TrackerService* m_tracker;
    QDate m_date;

    NeedsBlockCard* m_needsBlock = nullptr; // the review card (part 2)
    CatchUpCard*    m_catchUp    = nullptr; // missed blocks (catch-up §K)
    QWidget*        m_reviewRow  = nullptr; // both of the above, one line
                                            // (v26.7.1 — one pill grammar)
    QWidget*        m_dayContent = nullptr; // everything the gate can hold back
    QLabel*         m_sub        = nullptr; // swaps text with the gate state
    QLabel*         m_title      = nullptr; // hidden inside the phone drawer

    StatBox*      m_focusBox      = nullptr;
    StatBox*      m_breakBox      = nullptr;
    StatBox*      m_distractedBox = nullptr;
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
