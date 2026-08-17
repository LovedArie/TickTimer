#pragma once
// ---------------------------------------------------------------------------
// ProposalCard — §B stage 3, the confirmation, as a widget (v29.0).
//
// One proposed change, rendered from the STRUCTURED proposal — never from
// the proposer's prose. The summary the owner reads is composed by
// Proposal::summary() out of the same fields apply() will consume, so what
// you approve is what will run; a proposer cannot describe one change and
// request another, because the description IS the request, re-rendered.
//
// The card is glass (the DebugPanel doctrine, second application): it
// shows, it emits applyRequested / discardRequested, and it decides
// nothing — validation and application belong to the container, which
// owns the AppData and the handle map. After the container acts, it calls
// settle(): buttons die, the outcome line appears, and the card becomes a
// visible piece of history in the transcript column.
//
// A card born with a failing verdict (rendered but not appliable — the
// world may have been wrong from the start) shows the reason and never
// enables Apply: a refusal the owner can READ beats a button that
// silently does nothing.
// ---------------------------------------------------------------------------

#include "AssistantVerbs.h"

#include <QWidget>

class QLabel;
class QPushButton;

class ProposalCard : public QWidget
{
    Q_OBJECT

public:
    ProposalCard(const verbs::Proposal& proposal, const QString& summary,
                 const verbs::Verdict& renderVerdict,
                 QWidget* parent = nullptr);

    const verbs::Proposal& proposal() const { return m_proposal; }

    // The container's verdict, delivered after Apply/Discard: disables
    // both buttons and shows the outcome. Idempotent on purpose — a
    // settled card stays settled.
    void settle(const QString& outcome);

signals:
    void applyRequested();
    void discardRequested();

private:
    verbs::Proposal m_proposal;
    QPushButton*    m_apply   = nullptr;
    QPushButton*    m_discard = nullptr;
    QLabel*         m_outcome = nullptr;
};
