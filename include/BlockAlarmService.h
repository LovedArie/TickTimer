#pragma once
// ---------------------------------------------------------------------------
// BlockAlarmService — "your 9:00 block is starting" (owner request), the
// third verse of the services song.
//
// Why a SERVICE and not the AgendaWidget: the widget is a painter, and up
// to ten of them are alive at once (day view, seven week columns, two in
// compare) — ten timers would mean ten toasts — and none may even be
// visible when the alarm should fire (being elsewhere is the scenario a
// notification exists for; the Pomodoro taught us that one). A block's
// schedule is DATA; the thing that watches data plus the clock is
// app-lifetime state, owned by MainWindow like its two older siblings.
//
// How it decides WHEN to wake (derive-don't-store, applied to time):
// there is no stored alarm list. The service derives the single NEXT
// start moment from AppData, arms one precise single-shot for exactly
// then, and re-derives whenever the data changes — a block moved,
// added, or deleted re-aims the timer automatically because the timer
// was never anything BUT a view of the data.
//
// What keeps it honest and quiet:
//   - a HIGH-WATER MARK (m_announcedUpTo): only starts strictly after it
//     are ever announced, and it only moves forward. One remembered
//     instant makes duplicates impossible and start-up back-spam
//     impossible (the mark is born at "now").
//   - a GRACE WINDOW: waking from a long sleep, anything staler than
//     kGraceSeconds is skipped silently — ten stale toasts about a
//     morning that already happened is noise, not help.
//   - the OWN-HANDS rule, inherited from the Pomodoro's skip(): a block
//     created already-underway announces nothing (its start is behind
//     the mark), and MainWindow additionally mutes blocks you are
//     ALREADY tracking — you clearly know.
// ---------------------------------------------------------------------------

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <functional>

class AppData;

class BlockAlarmService : public QObject
{
    Q_OBJECT

public:
    // Late is forgiven up to here; beyond it, silence (see header).
    static constexpr int kGraceSeconds = 120;

    explicit BlockAlarmService(const AppData* data, QObject* parent = nullptr);

    // TEST SEAM, the house pattern (TrackerService::nowProvider) — with one
    // instructive difference: THIS service reads the clock AT CONSTRUCTION
    // (the high-water mark is born at "now"), so a seam patched onto the
    // object afterwards would arrive too late — the mark would already be
    // wall-clock real. When a dependency is used in the constructor, it
    // must come in THROUGH the constructor; "inject after" only works for
    // dependencies first touched after. Hence the second ctor below.
    BlockAlarmService(const AppData* data, std::function<QDateTime()> now,
                      QObject* parent = nullptr);

    std::function<QDateTime()> nowProvider; // set by whichever ctor ran

public slots:
    void poll(); // announce anything due, then re-arm for the next start

signals:
    // Every block whose start just arrived (usually one; simultaneous
    // starts share one emission so the UI can build ONE toast, not a
    // stack). Ids, not pointers: the receiver looks up fresh state at
    // its own moment of use — a pointer could dangle if handling is ever
    // deferred; an id can only miss (and a miss is handled).
    void blocksStarting(const QVector<QString>& eventIds);

private:
    void rearm(const QDateTime& now); // aim the single-shot at the next start

    const AppData* m_data = nullptr;  // not owned; observed
    QDateTime m_announcedUpTo;        // the high-water mark
    QTimer    m_timer;                // single-shot, PRECISE (see ctor note)
};
