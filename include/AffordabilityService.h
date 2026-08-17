#pragma once
// ---------------------------------------------------------------------------
// AffordabilityService — the WHEN of the proactive heads-up (v28.0).
//
// The spine (assistant addendum §A): code decides WHEN. This QObject is that
// code, and it is deliberately dumb — a timer, a debounce, and a loop. Every
// judgment (the verdict, the manners, the sentence) lives in afford:: where
// tests reach it without a clock or a widget. The service's whole job is:
//
//     every ~20 min, and shortly after the data changes:
//         for each open task with a deadline → report → decide → maybe emit
//
// It emits a SIGNAL rather than showing the toast itself, for the same
// reason ChatClient doesn't paint bubbles: a service that touches UI can't
// be constructed in a headless test, and the wire/surface split is the
// house style (provider addendum). MainWindow owns the connection to
// NotificationToast.
//
// BOOKKEEPING LIVES IN QSettings, NOT data.json — a deliberate call worth
// its comment: "what did I last say, and how many times today" is MANNERS
// STATE, not a fact about the user's life. Losing it costs at worst one
// repeated heads-up; syncing it across devices would be actively wrong (a
// nudge shown on the laptop should not mute the phone the owner is actually
// holding — each device keeps its own courtesy ledger). Facts go in the
// data file; courtesies stay local. Consequence: v28.0 needs NO format
// bump, which also keeps this slice clear of the v11 numbering collision
// recorded in the audit.
// ---------------------------------------------------------------------------

#include "Affordability.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include <QSet>

#include <functional>

class AppData;
class NudgeClient;

class AffordabilityService : public QObject
{
    Q_OBJECT

public:
    explicit AffordabilityService(AppData* data, QObject* parent = nullptr);

    // The injected clock, house pattern (PomodoroEngine, the breaker):
    // tests set a fake now and call sweep() directly; production leaves it.
    void setNowProvider(std::function<QDateTime()> provider);

    // v28.1 — the persona band, INJECTED rather than read from chat::
    // directly. Not ceremony: the direct include would drag ChatSession
    // (and through the client, Qt Network) into DOMAIN_SOURCES, undoing
    // the test-target split the CMake file celebrates. MainWindow wires
    // the real chat::configuredPersonaBand; the default (empty band) is
    // itself valid — the locked rules alone are a complete prompt.
    void setPersonaProvider(std::function<QString()> provider);

    // One pass over every open, deadlined task. Public so a test — or the
    // debug panel's "sweep now" button — can run it without waiting 20 min.
    void sweep();

    // v28.10 — the reset this service's own .cpp promised when it chose one
    // key prefix ("a 'reset the assistant's memory' button later is one
    // remove('afford')"). Forgets last-spoken verdicts and today's cap, so
    // a nudge that already spoke can be provoked into speaking again. The
    // service owns it because the prefix is its private knowledge.
    static void forgetManners();

    afford::Rule rule; // thresholds; a future Settings page edits this

signals:
    // The heads-up. taskId rides along so a future toast action ("show me")
    // can open the task — the seam costs nothing today (§G.1 sets up the
    // notification-with-action shape this will use).
    void nudge(const QString& title, const QString& body,
               const QString& taskId);

private:
    int  todaysCount(const QDateTime& now) const;
    void bumpTodaysCount(const QDateTime& now);

    // v28.1 — the phrased path. deliver() is the ONE exit for a speaking
    // nudge: model text and the C++ sentence both leave through it, so the
    // bookkeeping (cap bump, verdict store) cannot diverge between voices.
    void deliver(const QString& taskId, const QString& title,
                 const QString& body);

    AppData* m_data = nullptr;
    QTimer   m_sweepTimer;   // the 20-minute heartbeat
    QTimer   m_changeDelay;  // debounce after AppData::changed()
    std::function<QDateTime()> m_now;
    std::function<QString()>   m_persona;
    NudgeClient* m_phraser = nullptr; // the wire; owned as a child
    QSet<QString> m_inFlight; // tasks with a phrasing round trip pending —
                              // a sweep landing mid-flight must not ask
                              // twice or double-toast
};
