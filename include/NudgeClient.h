#pragma once
// ---------------------------------------------------------------------------
// NudgeClient — the wire half of a phrased nudge (v28.1).
//
// LlmQuickAddClient's recipe, third use (a pattern's second consumer is
// when it earns a name; its third is when the name is proven): one-shot
// POST, key resolved at fire time, generation counter for supersede-by-
// silence, exactly-once completion.
//
// One departure, and it is the client's whole personality: THIS client
// cannot fail loudly. Quick-add fails into a visible preview bar; chat
// fails into a ⚠ bubble; a nudge fires when nobody is watching, so every
// failure — no key, bad address, timeout, 401, garbage — collapses into
// the same signal: fallback(). The caller shows the deterministic
// sentence and the owner never learns a network call existed. Errors are
// a debugging story (qDebug), not a UX one.
//
// Single seat, no route walk, on purpose: the nudge's fallback seat IS
// afford::sentence(), the same reasoning that kept quick-add off the
// v26 routing table (§M). A fallback walk buys reachability for a
// feature that already cannot fail.
// ---------------------------------------------------------------------------

#include "LlmProvider.h"

#include <QNetworkAccessManager>
#include <QObject>

#include <optional>

class NudgeClient : public QObject
{
    Q_OBJECT

public:
    explicit NudgeClient(QObject* parent = nullptr);

    // Tests point this at a stub server; production reads Settings.
    void setProviderOverride(const std::optional<ai::Provider>& p)
    {
        m_override = p;
    }

    // Ask for wording. Emits EXACTLY ONE of phrased(text) / fallback(),
    // unless superseded by a newer phrase() — then silence, and the newer
    // call owns the outcome. `timeoutMs` is the whole budget: a nudge that
    // arrives late is a nudge about a stale verdict.
    void phrase(const QString& system, const QString& user,
                int timeoutMs = 8000);

signals:
    void phrased(const QString& text); // passed nudge::accept — show this
    void fallback();                   // any failure at all — show the C++
                                       // sentence; reasons stay in the log

private:
    QNetworkAccessManager m_nam;
    quint64               m_generation = 0;
    std::optional<ai::Provider> m_override;
};
