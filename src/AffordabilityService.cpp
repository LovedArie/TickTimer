#include "AffordabilityService.h"

#include "AppData.h"
#include "NudgeClient.h"
#include "NudgePhrasing.h"

#include <QSettings>

#include <memory>

namespace
{
// QSettings keys, all under one prefix so a "reset the assistant's memory"
// button later is one remove("afford").
//   afford/last/<taskId>  int(Verdict) last SPOKEN (or silently-cleared)
//   afford/countDate      yyyy-MM-dd the counter belongs to
//   afford/count          nudges spoken on countDate
const QString kLastPrefix = QStringLiteral("afford/last/");
const QString kCountDate  = QStringLiteral("afford/countDate");
const QString kCount      = QStringLiteral("afford/count");
} // namespace

AffordabilityService::AffordabilityService(AppData* data, QObject* parent)
    : QObject(parent)
    , m_data(data)
    , m_now([] { return QDateTime::currentDateTime(); })
    , m_persona([] { return QString(); })
{
    // 20 minutes: fast enough that "the morning filled up" is noticed while
    // it still matters, slow enough to be invisible in a profiler. Verdicts
    // move at calendar speed, not keystroke speed.
    m_sweepTimer.setInterval(20 * 60 * 1000);
    connect(&m_sweepTimer, &QTimer::timeout, this,
            &AffordabilityService::sweep);
    m_sweepTimer.start();

    // After an edit, wait 30 s of quiet before judging. Two reasons: a
    // half-built plan (three blocks placed, two to go) is exactly when a
    // "Tight" verdict would be WRONG-and-alarming; and changed() fires on
    // every keystroke-adjacent mutation — judging each one is noise. The
    // restart-on-start() trick is the debounce, same as the window-save
    // timer (v23.1).
    m_changeDelay.setSingleShot(true);
    m_changeDelay.setInterval(30 * 1000);
    connect(&m_changeDelay, &QTimer::timeout, this,
            &AffordabilityService::sweep);
    connect(m_data, &AppData::changed, &m_changeDelay,
            qOverload<>(&QTimer::start));

    m_phraser = new NudgeClient(this); // child: dies with the service
}

void AffordabilityService::setNowProvider(std::function<QDateTime()> provider)
{
    m_now = std::move(provider);
}

void AffordabilityService::forgetManners()
{
    // The one-line cash-out of the anonymous-namespace comment above: every
    // manners key lives under one prefix precisely so this is one remove.
    QSettings().remove(QStringLiteral("afford"));
}

void AffordabilityService::setPersonaProvider(std::function<QString()> provider)
{
    m_persona = std::move(provider);
}

int AffordabilityService::todaysCount(const QDateTime& now) const
{
    QSettings s;
    if (s.value(kCountDate).toString()
        != now.date().toString(Qt::ISODate))
        return 0; // a new day resets the cap by construction
    return s.value(kCount, 0).toInt();
}

void AffordabilityService::bumpTodaysCount(const QDateTime& now)
{
    QSettings s;
    const QString today = now.date().toString(Qt::ISODate);
    const int count =
        (s.value(kCountDate).toString() == today)
            ? s.value(kCount, 0).toInt() : 0;
    s.setValue(kCountDate, today);
    s.setValue(kCount, count + 1);
}

void AffordabilityService::deliver(const QString& taskId,
                                   const QString& title,
                                   const QString& body)
{
    m_inFlight.remove(taskId);

    // Bookkeeping happens at DELIVERY, not at decision: the round trip
    // took seconds, and a cap or last-verdict written before the toast
    // exists would count speech that might never have happened (a crash
    // mid-flight, a superseded request). The one small honesty cost: the
    // quiet-hours check ran at decision time, so a request fired at
    // 21:59:58 can deliver at 22:00:04 — two seconds of tardiness against
    // a re-check that would silently swallow an owed heads-up. Delivery
    // wins.
    const QDateTime now = m_now();
    emit nudge(title, body, taskId);
    bumpTodaysCount(now);
    QSettings().setValue(kLastPrefix + taskId,
                         int(afford::Verdict::Tight));
}

void AffordabilityService::sweep()
{
    const QDateTime now = m_now();
    QSettings settings;

    // v28.4: one multiplier for the whole sweep. The rate is a fact about
    // the USER's history, not about any one task — computing it per task
    // would be N identical scans producing N identical answers.
    const double multiplier = afford::personalMultiplier(*m_data, rule);

    for (const Task& task : m_data->tasks()) {
        const afford::Report report =
            afford::affordability(*m_data, task, now, rule, multiplier);
        if (report.verdict == afford::Verdict::NotApplicable)
            continue;

        const QString key = kLastPrefix + task.id;
        const auto lastSpoken = afford::Verdict(
            settings.value(key, int(afford::Verdict::NotApplicable))
                .toInt());

        const afford::Nudge n = afford::decide(
            report, task, lastSpoken, todaysCount(now), now, rule);

        if (n.speak) {
            // v28.1 — the model gets first crack at the WORDING; the
            // verdict, title and manners are already settled and travel
            // with the request as captures. In-flight guard: a sweep that
            // lands during the round trip must not ask twice — the toast
            // it would produce is the one already on its way.
            if (m_inFlight.contains(task.id))
                continue;
            m_inFlight.insert(task.id);

            const QString taskId   = task.id;
            const QString title    = n.title;
            const QString cppBody  = n.body; // the v28.0 voice, kept warm

            // Per-request connections (not one connect in the ctor):
            // phrased()/fallback() carry no task identity, so the identity
            // rides in the lambda captures — and disconnecting after one
            // outcome keeps a superseding request's signals from replaying
            // into an old task's delivery.
            auto deliverOnce =
                std::make_shared<QMetaObject::Connection>();
            auto fallbackOnce =
                std::make_shared<QMetaObject::Connection>();
            *deliverOnce = connect(
                m_phraser, &NudgeClient::phrased, this,
                [this, taskId, title, deliverOnce,
                 fallbackOnce](const QString& text) {
                    disconnect(*deliverOnce);
                    disconnect(*fallbackOnce);
                    deliver(taskId, title, text);
                });
            *fallbackOnce = connect(
                m_phraser, &NudgeClient::fallback, this,
                [this, taskId, title, cppBody, deliverOnce,
                 fallbackOnce]() {
                    disconnect(*deliverOnce);
                    disconnect(*fallbackOnce);
                    deliver(taskId, title, cppBody); // §A: the sentence
                                                     // that always works
                });

            m_phraser->phrase(
                nudge::systemPrompt(m_persona()),
                nudge::userMessage(report, task.title));
        } else if (report.verdict != afford::Verdict::Tight) {
            // Re-arm silently: once the task is Comfortable (or Unknown)
            // again, a FUTURE Tight is news and may speak. Written every
            // sweep; cheap, and it is what makes change-of-verdict a real
            // rule rather than a once-per-task fuse.
            settings.setValue(key, int(report.verdict));
        }
        // Tight-but-suppressed (quiet hours / cap / dismissal): store
        // NOTHING. The heads-up was never delivered, so it is still owed —
        // the next sweep outside the suppression gets to say it. Recording
        // it here would mark news as old before anyone heard it.
    }
}
