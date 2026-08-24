#pragma once
// ---------------------------------------------------------------------------
// CheckInService — the morning knock (v28.2 part 2). AffordabilityService's
// little sibling: a timer, a settings fact, and a pure gate. All judgment
// lives in checkin::shouldOffer; the service owns exactly the two things a
// pure function cannot — the clock ticking and the QSettings ledger
// ("checkin/lastOffered", manners state per the v28.0 doctrine: courtesy is
// per-device, so a laptop's morning knock never spends the phone's).
//
// It emits offer() and marks lastOffered AT EMIT — not at tap. Deliberate:
// the once-a-day promise is about ASKING, and a dismissed toast was still
// an ask. Marking at tap would re-knock every 15 minutes at someone who
// already said "not now" with their thumb — the snooze-is-a-lie bug again.
// ---------------------------------------------------------------------------

#include "CheckIn.h"

#include <QDate>
#include <QObject>
#include <QTimer>

#include <functional>

class AppData;

class CheckInService : public QObject
{
    Q_OBJECT

public:
    explicit CheckInService(AppData* data, QObject* parent = nullptr);

    void setNowProvider(std::function<QDateTime()> provider);
    void sweep(); // public for tests and the debug panel's "sweep now"

    // v28.10 — the debug panel's rehearsal button. Skips the GATE (morning
    // window ∧ heavy day ∧ once-a-day) but not the SCRIPT: same body, same
    // signal, same tap-through into the chat. Deliberately does NOT touch
    // the ledger — a rehearsal must not spend the real morning's one ask.
    void forceOffer();

    // The ledger reset, owned HERE because the key is this file's private
    // knowledge — the panel presses, the service knows which key it keeps.
    static void clearTodaysAsk();

    // ...and the READ side of that same ledger (v30.6). AlarmService needs
    // it to decide whether a scheduled morning knock is still owed, and it
    // asks through this door for exactly the reason the reset lives here:
    // one file, and only one, may know how the key is spelled.
    static QDate lastOffered();

    checkin::Rule rule;

signals:
    void offer(const QString& title, const QString& body);

private:
    void offerNow(const QDateTime& now); // compose + emit; callers own gates

    AppData* m_data = nullptr;
    QTimer   m_timer; // 10 min: the window is 5h wide; finer is pointless
    std::function<QDateTime()> m_now;
};
