#include "CheckInService.h"

#include "AppData.h"
#include "DayBriefing.h"

#include <QSettings>

namespace
{
const QString kLastOffered = QStringLiteral("checkin/lastOffered");
} // namespace

CheckInService::CheckInService(AppData* data, QObject* parent)
    : QObject(parent)
    , m_data(data)
    , m_now([] { return QDateTime::currentDateTime(); })
{
    m_timer.setInterval(10 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &CheckInService::sweep);
    m_timer.start();
    // First look shortly after launch, not instantly: launching INTO a
    // toast reads as an ambush; thirty seconds in, it reads as a knock.
    QTimer::singleShot(30 * 1000, this, &CheckInService::sweep);
}

void CheckInService::setNowProvider(std::function<QDateTime()> provider)
{
    m_now = std::move(provider);
}

void CheckInService::sweep()
{
    const QDateTime now = m_now();
    const QDate lastOffered =
        QDate::fromString(QSettings().value(kLastOffered).toString(),
                          Qt::ISODate);
    if (!checkin::shouldOffer(*m_data, now, lastOffered, rule))
        return;

    QSettings().setValue(kLastOffered,
                         now.date().toString(Qt::ISODate)); // ask = spent —
                                                            // marked at EMIT,
                                                            // see the header
    offerNow(now);
}

void CheckInService::forceOffer()
{
    // The gate skipped on purpose, the ledger untouched on purpose (v28.10,
    // the debug panel): the point is to SEE the flow — toast, tap, the chat
    // opening with the question waiting — on a quiet Tuesday afternoon.
    offerNow(m_now());
}

void CheckInService::clearTodaysAsk()
{
    QSettings().remove(kLastOffered);
}

QDate CheckInService::lastOffered()
{
    // An absent key reads as an invalid QDate, which every caller already
    // treats as "never asked" — the tolerant-read habit JsonStore uses,
    // applied to a preference.
    return QSettings().value(kLastOffered).toDate();
}

void CheckInService::offerNow(const QDateTime& now)
{
    // The toast line is C++ and SPECIFIC (§G.3): the heavy facts, briefly.
    int planned = 0;
    for (const Event& e : m_data->events())
        if (e.date == now.date())
            planned += e.plannedEndMinutes - e.plannedStartMinutes;
    QString body = tr("Today looks full");
    if (planned > 0)
        body = tr("Today looks full — %1 planned")
                   .arg(brief::spanLabel(qint64(planned) * 60));
    body += tr(". Got a second to check in?");

    emit offer(tr("Morning check-in"), body);
}
