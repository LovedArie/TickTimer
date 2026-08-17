#pragma once

#include "AppData.h"

#include <QDate>
#include <QDialog>
#include <QJsonObject>
#include <QVBoxLayout>

class AgendaWidget;
class QLabel;
class TrackerService;

// ---------------------------------------------------------------------------
// CompareDialog v2 — no longer a scoreboard: a PLANNING screen.
//
// Two real AgendaWidgets side by side in ONE shared scroll (same widget, same
// kSlotHeight — the rows align to the pixel, so "are we both free at 7?" is
// answered by your eyes). The stats ride in a side column.
//
// The asymmetry is the design:
//   - YOUR side is fully live: click a free slot to plan (the same
//     PickActivityDialog → three-doors recipe as PlannerPage), click a block
//     to open EventDialog, drag an edge to resize. Same signals, same domain
//     doors, same guards — this screen adds ZERO new mutation paths.
//   - THEIR side is a snapshot painted by the identical widget, made
//     untouchable with WA_TransparentForMouseEvents: it LOOKS the same and
//     ignores the mouse entirely. Read-only by physics, not by discipline.
//
// (History note: v1 held `const AppData*` so the compiler enforced
// look-don't-touch. The requirement changed — comparing turned out to be
// PLANNING, and planning means editing your own day right here. The const
// didn't vanish; it moved to where it still belongs: the peer snapshot and
// the AgendaWidget's own const view of both.)
// ---------------------------------------------------------------------------

class CompareDialog : public QDialog
{
    Q_OBJECT
public:
    // `myName` (owner request): the columns are labelled with real account
    // names, PINNED above the scroll — scrolling to 9 PM must not scroll
    // away the answer to "which side is me?". Empty myName degrades to
    // "You" (tests, or a session that somehow has no name).
    CompareDialog(AppData* mine, TrackerService* tracker,
                  const QString& myName, const QString& peerName,
                  const QJsonObject& peerBlob, QWidget* parent = nullptr);

    // Public + parameterless-refresh entry so tests can drive the dialog to
    // a KNOWN date and read the labels — never trust "today" in a test.
    void showDay(const QDate& day);

private:
    void refresh();
    void planAt(QDate date, int slotIndex); // PlannerPage's recipe, reused

    AppData*        m_mine;    // live and editable — that's the point now
    TrackerService* m_tracker; // for EventDialog (timers keep working here)
    AppData         m_peer;    // owned snapshot — read-only forever
    QString         m_myName;  // the logged-in account, for the column header
    QString         m_peerName;
    QDate           m_day;

    AgendaWidget* m_myAgenda   = nullptr;
    AgendaWidget* m_peerAgenda = nullptr;
    QLabel*       m_dayLabel   = nullptr;
    QLabel*       m_headline   = nullptr;
    QLabel*       m_cells[4][3] = {};
};
