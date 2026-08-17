#include "BlockAlarmService.h"

#include "AppData.h"
#include "Event.h"

namespace
{
// The one place an Event's date + minutes become an instant. plannedStart
// lives as minutes-after-midnight on a date (domain shape); alarms live in
// QDateTime (clock shape) — this is the seam between the two.
QDateTime startInstant(const Event& e)
{
    return QDateTime(e.date, QTime(0, 0))
        .addSecs(e.plannedStartMinutes * 60);
}
} // namespace

BlockAlarmService::BlockAlarmService(const AppData* data, QObject* parent)
    : BlockAlarmService(data, [] { return QDateTime::currentDateTime(); },
                        parent)
{
}

BlockAlarmService::BlockAlarmService(const AppData* data,
                                     std::function<QDateTime()> now,
                                     QObject* parent)
    : QObject(parent)
    , nowProvider(std::move(now))
    , m_data(data)
    , m_announcedUpTo(nowProvider()) // born at "now": the past is not ours
{
    // PRECISE, not Qt's default CoarseTimer: coarse trades accuracy for
    // battery by allowing ~5% slack — harmless for a repaint, but 5% of a
    // 40-minute wait is a toast TWO MINUTES late, right past the grace
    // window's spirit. Alarms are the rare timer that earns precision.
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &BlockAlarmService::poll);

    // The timer is a VIEW of the data: any change may move the next start.
    connect(m_data, &AppData::changed, this, [this]() {
        rearm(nowProvider());
    });

    rearm(nowProvider());
}

void BlockAlarmService::poll()
{
    const QDateTime now = nowProvider();

    // Everything due since the mark, oldest first isn't needed — one pass,
    // one emission. Fresh starts get announced; stale ones (slept laptop,
    // suspended VM) are skipped in SILENCE — but both move the mark, so
    // neither can ever be announced twice or resurrect later.
    QVector<QString> due;
    for (const Event& e : m_data->events()) {
        const QDateTime start = startInstant(e);
        if (start <= m_announcedUpTo || start > now)
            continue;
        if (start.secsTo(now) <= kGraceSeconds)
            due.append(e.id);
    }
    m_announcedUpTo = now;

    if (!due.isEmpty())
        emit blocksStarting(due);

    rearm(now);
}

void BlockAlarmService::rearm(const QDateTime& now)
{
    // Derive the single next start strictly after the mark (not after
    // "now": a start in the gap between them is exactly what poll() must
    // still catch on its next run).
    QDateTime next;
    for (const Event& e : m_data->events()) {
        const QDateTime start = startInstant(e);
        if (start <= m_announcedUpTo)
            continue;
        if (!next.isValid() || start < next)
            next = start;
    }

    if (!next.isValid()) {
        m_timer.stop(); // nothing scheduled ahead — sleep until data changes
        return;
    }

    // Cap the nap at an hour: QTimer intervals are ints (a start next month
    // would overflow), and an hourly self-check heals clock jumps and
    // suspend/resume without any platform-specific wake signals.
    const qint64 msec = qMin<qint64>(qMax<qint64>(now.msecsTo(next), 0),
                                     60LL * 60 * 1000);
    m_timer.start(int(msec));
}
