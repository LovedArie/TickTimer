#include "ProposalCard.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ProposalCard::ProposalCard(const verbs::Proposal& proposal,
                           const QString& summary,
                           const verbs::Verdict& renderVerdict,
                           QWidget* parent)
    : QWidget(parent)
    , m_proposal(proposal)
{
    // A framed box, visually distinct from speech bubbles: this is not
    // something SAID, it is something ASKED — the difference the whole
    // trust boundary hangs on, so the chrome states it.
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("proposalFrame"));
    frame->setFrameShape(QFrame::StyledPanel);

    auto* inner = new QVBoxLayout(frame);

    auto* heading = new QLabel(tr("Proposed change"));
    heading->setObjectName(QStringLiteral("proposalHeading"));
    inner->addWidget(heading);

    auto* body = new QLabel(summary);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    inner->addWidget(body);

    m_outcome = new QLabel;
    m_outcome->setWordWrap(true);
    m_outcome->setVisible(false);
    inner->addWidget(m_outcome);

    auto* row = new QHBoxLayout;
    m_apply = new QPushButton(tr("Apply"));
    m_apply->setObjectName(QStringLiteral("proposalApply"));
    m_discard = new QPushButton(tr("Discard"));
    m_discard->setObjectName(QStringLiteral("proposalDiscard"));
    row->addStretch(1);
    row->addWidget(m_discard);
    row->addWidget(m_apply);
    inner->addLayout(row);

    // Born invalid → born readable: the reason shows immediately and
    // Apply never lives. Discard stays enabled — the owner can always
    // wave a broken proposal away.
    if (!renderVerdict.ok) {
        m_outcome->setText(renderVerdict.reason);
        m_outcome->setVisible(true);
        m_apply->setEnabled(false);
    }

    connect(m_apply, &QPushButton::clicked, this,
            &ProposalCard::applyRequested);
    connect(m_discard, &QPushButton::clicked, this,
            &ProposalCard::discardRequested);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(frame);
}

void ProposalCard::settle(const QString& outcome)
{
    m_apply->setEnabled(false);
    m_discard->setEnabled(false);
    m_outcome->setText(outcome);
    m_outcome->setVisible(true);
}
