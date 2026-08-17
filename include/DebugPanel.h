#pragma once
// ---------------------------------------------------------------------------
// DebugPanel — every seam, one keyboard chord (v28.10).
//
// Born from the first field report's closing observation: the v28 services
// were built full of injection seams — setNowProvider, public sweep()s,
// TICKTIMER_AI_DOWN — and not ONE was reachable from the running app. The
// owner could not force a check-in (it needs 06:00–11:00 AND a computably
// heavy day), could not skip a 20-minute sweep, and had never once heard
// the v28.0 fallback voice, because a working provider always wins. Seams
// only tests can reach are half a seam; this panel is the other half.
//
// WHY A CHORD AND NOT A MENU BAR: the app's chrome is a nav rail. Growing a
// QMenuBar to host one developer entry would change every user's window for
// the benefit of one debugging session. Ctrl+Shift+D costs nothing to the
// people who don't know it and one line of TESTING.md to the people who do.
//
// WHY IT DECIDES NOTHING: the panel is pure glass over existing seams.
// Every button calls a public method the test suite already calls, or flips
// an environment variable the test suite already flips. The moment a button
// here seems to need new judgement, that judgement belongs in the service
// (and in a test) first — the panel only ever gets to PRESS things. That is
// also why its constructor takes FUNCTIONS for the two jobs that cross
// object boundaries (the briefing text, the clock rewire): the composition
// root keeps the wiring, the panel keeps the buttons.
// ---------------------------------------------------------------------------

#include <QDateTime>
#include <QDialog>

#include <functional>
#include <optional>

class AffordabilityService;
class CheckInService;
class QCheckBox;
class QDateTimeEdit;
class QLabel;

class DebugPanel : public QDialog
{
    Q_OBJECT

public:
    // `briefing`  — returns the exact text the assistant receives, fetched
    //               fresh per press (the context is the product; debugging
    //               the assistant starts with reading what it was told).
    // `applyClock`— rewires every service's now-provider to the given fixed
    //               moment, or back to the wall clock on nullopt. Lives in
    //               MainWindow because WHICH services have clocks is
    //               composition knowledge, not panel knowledge.
    // `injectProposal` (v29.0) — composes and presents a sample intake
    //    proposal through the REAL boundary (validate → card → tap →
    //    apply), returning a status line for the panel. The composition
    //    lives in MainWindow because choosing a target needs AppData and
    //    the chat's handle world — wiring knowledge, as ever.
    DebugPanel(AffordabilityService* afford, CheckInService* checkIn,
               std::function<QString()> briefing,
               std::function<void(const std::optional<QDateTime>&)> applyClock,
               std::function<QString()> injectProposal,
               std::function<QString()> startIntake,
               QWidget* parent = nullptr);

private:
    void showBriefing();

    AffordabilityService* m_afford  = nullptr;
    CheckInService*       m_checkIn = nullptr;
    std::function<QString()> m_briefing;
    std::function<void(const std::optional<QDateTime>&)> m_applyClock;
    std::function<QString()> m_injectProposal;
    std::function<QString()> m_startIntake;

    QDateTimeEdit* m_clockEdit  = nullptr;
    QLabel*        m_clockState = nullptr;
};
