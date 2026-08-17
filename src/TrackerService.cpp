#include "TrackerService.h"

#include "AppData.h"

namespace
{
constexpr int kTickMs      = 1000;      // UI refresh cadence
constexpr int kHeartbeatMs = 30 * 1000; // crash-insurance refresh cadence
} // namespace

TrackerService::TrackerService(AppData* data, QObject* parent)
    : QObject(parent)
    , m_data(data)
{
    m_secondTimer.setInterval(kTickMs);
    m_heartbeatTimer.setInterval(kHeartbeatMs);

    // Qt signal->signal connection: the QTimer's timeout IS our tick; no
    // forwarding slot needed.
    connect(&m_secondTimer, &QTimer::timeout, this, &TrackerService::tick);
    // Same heartbeat, second job: watch for the tracked block's window
    // closing. Order deliberate — enforce AFTER tick, so the final second
    // still paints before the stop lands.
    connect(&m_secondTimer, &QTimer::timeout,
            this, &TrackerService::enforceWindow);

    // The heartbeat: every 30 s, stamp "still alive at <now>" into the
    // RunningState. touchRunning() emits changed(), changed() triggers a
    // save — so the insurance on disk is never more than 30 s stale.
    connect(&m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        m_data->touchRunning(nowProvider());
    });
}

qint64 TrackerService::liveSeconds() const
{
    if (m_state == State::Idle)
        return 0;
    // nowProvider, NOT currentDateTime(). The v19.6 audit found FOUR
    // wall-clock reads past the seam (here, beginInterval, the heartbeat,
    // and commit's end-stamp) — caught by a visual check whose fake-clock
    // badge refused to tick, then by a test that had been green for the
    // wrong reason. A seam that's only mostly installed is worse than
    // none: every test trusts it, and each hole lies to all of them. The
    // repair rule: grep the whole file for currentDateTime, not just the
    // symptom's line — holes come in families (the same habit that made
    // one made the others).
    return m_startedAt.secsTo(nowProvider());
}

void TrackerService::enforceWindow()
{
    if (m_state == State::Idle)
        return;
    if (canTrackNow(m_eventId))
        return; // window still open (or door still says yes) — nothing to do

    // The window has passed (or the block vanished — a deleted block ends
    // its own tracking, which is the honest reading of deletion). stop()
    // commits the in-flight interval with real timestamps: at most one
    // tick past the boundary, which is the truth of when tracking ended.
    const QString ended = m_eventId;
    stop();
    emit trackedBlockEnded(ended);
}

QString TrackerService::liveEventNow() const
{
    const QDateTime now = nowProvider();
    for (const Event* e : m_data->eventsOn(now.date()))
        if (e->isLiveAt(now))
            return e->id; // at most one — the no-overlap rule at work
    return {};
}

bool TrackerService::canTrackNow(const QString& eventId) const
{
    const Event* e = m_data->eventById(eventId);
    return e && e->isLiveAt(nowProvider());
}

void TrackerService::startFocus(const QString& eventId)
{
    // Guard FIRST, before ANY side effect: a refused start must not stop
    // (or steal from) whatever interval is currently running. Refusal
    // means "nothing happened", not "something half-happened".
    if (!canTrackNow(eventId))
        return;
    // Guard: no-op if we're already doing exactly this (double-click safety).
    if (m_state == State::Focusing && m_eventId == eventId)
        return;
    commitCurrentInterval();
    beginInterval(eventId, SegmentKind::Focus);
    m_state = State::Focusing;
    emit stateChanged();
}

void TrackerService::startBreak(const QString& eventId)
{
    if (!canTrackNow(eventId)) // same honesty guard as startFocus
        return;
    if (m_state == State::OnBreak && m_eventId == eventId)
        return;
    // UC2 step 2: switching focus->break ENDS the focus interval (a
    // finished fact, committed) and begins a break interval. The commit
    // happens automatically on every transition — the user never has to
    // remember to "save" their focus time.
    commitCurrentInterval();
    beginInterval(eventId, SegmentKind::Break);
    m_state = State::OnBreak;
    emit stateChanged();
}

void TrackerService::startDistracted(const QString& eventId)
{
    if (!canTrackNow(eventId)) // same honesty guard as startFocus
        return;
    if (m_state == State::Distracted && m_eventId == eventId)
        return;
    // Exactly the shape of startBreak: commit whatever interval was running,
    // then open a Distracted one. Off-task time is still REAL time on the
    // clock, so it's tracked identically — only the kind (and later, its
    // colour) differs. The auto-commit means switching focus->distracted
    // seals the focus interval as a finished fact, no manual "save".
    commitCurrentInterval();
    beginInterval(eventId, SegmentKind::Distracted);
    m_state = State::Distracted;
    emit stateChanged();
}

void TrackerService::stop()
{
    if (m_state == State::Idle)
        return;
    commitCurrentInterval();
    m_state = State::Idle;
    m_eventId.clear();
    m_secondTimer.stop();
    m_heartbeatTimer.stop();
    emit stateChanged();
}

void TrackerService::beginInterval(const QString& eventId, SegmentKind kind)
{
    m_eventId   = eventId;
    m_kind      = kind;
    m_startedAt = nowProvider();

    // Write the crash insurance FIRST, before a single second elapses —
    // if we die right now, the start timestamp is already on disk.
    RunningState state;
    state.eventId  = eventId;
    state.kind     = kind;
    state.start    = m_startedAt;
    state.lastSeen = m_startedAt;
    m_data->setRunning(state);

    m_secondTimer.start();
    m_heartbeatTimer.start();
}

void TrackerService::commitCurrentInterval()
{
    if (m_state == State::Idle)
        return;

    Segment s;
    s.kind  = m_kind;
    s.start = m_startedAt;
    s.end   = nowProvider();

    // Order matters: clear the insurance before appending, so the single
    // save triggered by appendSegment writes both effects at once and the
    // file never claims a segment is both committed AND still running.
    m_data->clearRunning();
    m_data->appendSegment(m_eventId, s); // drops zero-length noise itself
}
