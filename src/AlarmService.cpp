#include "AlarmService.h"

#include "AppData.h"

AlarmService::AlarmService(const AppData* data, QObject* parent)
    : AlarmService(data, [] { return QDateTime::currentDateTime(); },
                   parent)
{
}

AlarmService::AlarmService(const AppData* data, std::function<QDateTime()> now,
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
    //
    // Worth knowing what this timer is and is not, after v30.6: it is the
    // DESKTOP half of the story, and only that. It is why the alarm is
    // exact on a machine that stays awake, and it is exactly the mechanism
    // that dies when Android suspends the process — which is why the
    // schedule is also published outward (see scheduleChanged).
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AlarmService::poll);

    // The schedule is a VIEW of the data: any change may add, move or
    // remove an alarm.
    connect(m_data, &AppData::changed, this, &AlarmService::republish);

    republish();
}

void AlarmService::setTrackedEventProvider(std::function<QString()> provider)
{
    m_tracked = std::move(provider);
    republish(); // the mute rules just changed meaning
}

void AlarmService::setExtrasProvider(
    std::function<QVector<alarms::Alarm>()> provider)
{
    m_extras = std::move(provider);
    republish();
}

void AlarmService::derive(const QDateTime& now)
{
    // FROM THE MARK, NOT FROM `now` — and this is the one subtle line in
    // the file. An alarm whose instant sits in the gap between the mark and
    // now has not been announced yet: it is precisely what poll() is about
    // to look for. Deriving the window from `now` would drop exactly those
    // and the app would go silent at the moment it should speak. (It did,
    // for one build; two tests caught it.)
    const QDateTime from =
        (m_announcedUpTo.isValid() && m_announcedUpTo < now) ? m_announcedUpTo
                                                             : now;

    // Everything the planner implies, plus whatever the composition root
    // contributes. Derived fresh every time and never accumulated: a
    // schedule that appended would keep alarms the data no longer supports.
    m_schedule = alarms::upcoming(*m_data, from,
                                  m_tracked ? m_tracked() : QString(), rule);

    // Extras are held to the same window as everything else, HERE rather
    // than in each provider — one gate every extra must pass through beats
    // trusting each provider to have got its own bounds right.
    if (m_extras) {
        const QDateTime until = from.addSecs(qint64(rule.horizonHours) * 3600);
        for (const alarms::Alarm& a : m_extras())
            if (a.at > from && a.at <= until)
                m_schedule.append(a);
    }
}

void AlarmService::republish()
{
    const QDateTime now = nowProvider();
    derive(now);

    // What goes OUTWARD is the future only. m_schedule deliberately reaches
    // back to the mark so poll() can find what just came due, but handing
    // a past instant to Android's AlarmManager makes it fire immediately —
    // the app would shout about this morning every time you opened it.
    QVector<alarms::Alarm> ahead;
    for (const alarms::Alarm& a : m_schedule)
        if (a.at > now)
            ahead.append(a);

    emit scheduleChanged(ahead);
    rearm(now);
}

void AlarmService::poll()
{
    const QDateTime now = nowProvider();

    // Re-derive first: an alarm may have been added since the timer was
    // armed, and the mark below is about to sweep past this instant either
    // way. Deriving after the sweep would lose it.
    derive(now);

    const QVector<alarms::Alarm> ready =
        alarms::dueBetween(m_schedule, m_announcedUpTo, now, rule);

    // The mark moves past STALE alarms too, not just announced ones. That
    // is what makes "skipped in silence" permanent rather than deferred.
    m_announcedUpTo = now;

    if (!ready.isEmpty())
        emit due(ready);

    rearm(now);
}

void AlarmService::rearm(const QDateTime& now)
{
    // Strictly after the MARK, not after "now": an instant in the gap
    // between them is exactly what the next poll() must still catch.
    const QDateTime next = alarms::nextAfter(m_schedule, m_announcedUpTo);

    if (!next.isValid()) {
        m_timer.stop(); // nothing scheduled ahead — sleep until data changes
        return;
    }

    // Cap the nap at an hour: QTimer intervals are ints (a start next month
    // would overflow), and an hourly self-check heals clock jumps and
    // suspend/resume without any platform-specific wake signals. On a phone
    // that self-check never runs, which is precisely why the schedule is
    // ALSO handed to the OS.
    const qint64 msec = qMin<qint64>(qMax<qint64>(now.msecsTo(next), 0),
                                     60LL * 60 * 1000);
    m_timer.start(int(msec));
}
