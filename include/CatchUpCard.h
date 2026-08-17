#pragma once
// ---------------------------------------------------------------------------
// CatchUpCard — one chip, three intensities, and a drawer (catch-up §K.5).
//
// v26.7 IS A REDESIGN, prototyped in HTML before a line of C++ moved (the
// owner's call after the panel grew crowded — see §K.5 for the process
// note). The card used to render its whole world inline on the glance
// panel: header, rows, footers, markers, chips. All of that DOING now
// lives in a SlidePanel drawer — the exact idiom NeedsBlockCard shipped in
// v22.9, "chips are handles, lists live in slide-overs" — and the card
// itself collapses to ONE chip whose intensity carries the state:
//
//   PROMINENT  amber, "N to catch up" — at a moment (morning open /
//              evening) with unresolved blocks: the feature's stage time.
//   MUTED      gray, dashed — the block count is real but the day is not
//              the time: snoozed ("N · back 22:30") or the moment toggle
//              is off. Present, honest, ignorable. This IS v26.6's
//              hidden≠gone marker, promoted from patch to design element.
//   ABSENT     nothing pending, nothing recoverable: zero rent.
//
// SNOOZE IS DE-EMPHASIS, NOT A LOCK (the owner's workflow, §K.5): "Later"
// demotes the chip to muted until the evening moment — but the muted chip
// still opens the drawer. The midday "finished early, let me review" tap
// costs one click and does NOT re-arm the prominence; close the drawer and
// the chip is muted again until the reset. Consequence: v26.6's "Show now"
// button ceases to exist as a concept — access was never what the snooze
// governed, only attention.
//
// WHAT THE DRAWER HOLDS: every verb. The rows (Move → / More… / Done /
// Skip), the undo receipt, Skip all, the resolved-blocks bring-back
// section, and Later. The glance panel is a reading surface again.
//
// STILL TRUE FROM BEFORE, unchanged: const AppData + signals up (the page
// decides); injected `now`; fingerprint-gated refresh (v22.2); the chip is
// ONE PERSISTENT button restyled in place, which retires this card's
// rebuild-teardown entirely — you can't delete a widget under a click if
// you never delete it.
// ---------------------------------------------------------------------------

#include "Event.h"      // BlockOutcome — the vocabulary of resolveRequested
#include "Reschedule.h" // reschedule::Option rides the accept signal

#include <QDateTime>
#include <QFrame>
#include <QStringList>
#include <QVector>

class AppData;
class QPushButton;
class SlidePanel;

class CatchUpCard : public QFrame
{
    Q_OBJECT

public:
    explicit CatchUpCard(const AppData* data, QWidget* parent = nullptr);

    void refresh(const QDateTime& now);

    // The gate's veto (v26.7.1): while the needs-block gate holds the
    // panel, the chip yields — one blocking review at a time (§K). A
    // SETTER rather than the panel calling hide() directly, because
    // visibility must have ONE owner: a panel-side hide() would fight the
    // fingerprint gate (data unchanged -> refresh early-returns -> the
    // chip never returns when the gate opens). The setter clears the
    // print, so the next refresh re-decides from scratch.
    void setSuppressed(bool suppressed);

    // Who the drawer covers — injected by the panel (v26.7.5); same
    // written-contract story as the sibling's setter. Unset falls back to
    // parent-then-self, which is what keeps bare test cards working.
    void setDrawerHost(QWidget* host) { m_drawerHost = host; }

    bool hasAnything() const { return m_hasAnything; }

signals:
    void acceptProposalRequested(const QString& eventId,
                                 const reschedule::Option& option);
    void resolveRequested(const QString& eventId, BlockOutcome outcome);
    void showDayRequested(QDate date);
    void resolveAllRequested(const QStringList& eventIds,
                             BlockOutcome outcome);

private:
    QString fingerprint(const QDateTime& now) const;
    void    applyChip(const QDateTime& now);

    // The drawer, lazily created on first open. Host = this card's parent
    // (the glance panel) so the sheet covers the column; falls back to the
    // card itself in bare embeddings (tests) — degraded but safe. Same
    // construction as NeedsBlockCard::drawer(), on purpose.
    SlidePanel* drawer();
    void        fillDrawer(const QDateTime& now);
    QWidget*    makeRow(const Event& block, const QDateTime& now);

    bool      isEveningAt(const QDateTime& now) const;
    bool      snoozedAt(const QDateTime& now) const;
    QDateTime snoozeTarget() const;

    const AppData* m_data;
    QPushButton*   m_chip   = nullptr; // the ONE persistent surface
    SlidePanel*    m_drawer = nullptr;
    QWidget*       m_drawerHost = nullptr; // injected (v26.7.5)

    bool      m_hasAnything = false;
    bool      m_suppressed  = false; // the gate's veto — see setSuppressed
    QString   m_expanded;       // row whose alternatives are open (drawer)
    bool      m_showResolved = false; // bring-back section open (drawer)
    QString   m_lastPrint;
    QDateTime m_lastNow;        // last injected clock — the persistent chip
                                // handler's `now` (nowProvider doctrine)
    QDateTime m_sessionSnooze;  // v22.7: RAM survives broken settings

    QStringList m_undoIds;      // §K.2 — the receipt, now drawn in the drawer
    QString     m_undoLabel;
};
