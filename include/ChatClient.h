#pragma once
// ---------------------------------------------------------------------------
// ChatClient — the WIRE half of the assistant. Fifth client in the family
// (AuthClient, SyncClient, ShareClient, LlmQuickAddClient), same house rules:
// async QNetworkAccessManager, typed signals, knows nothing about widgets or
// AppData.
//
// WHY NOT JUST REUSE LlmQuickAddClient?
// Because they differ in every dimension that matters to a network call:
//
//                    quick-add                 chat
//   payload          one line                  a windowed transcript
//   reply            JSON we must parse        prose we display verbatim
//   budget           300 tokens                800
//   timeout          15 s (a capture bar)      60 s (a person is waiting,
//                                              and a local model is slow)
//   cancellable      no (just supersede)       yes (a visible Stop)
//
// Merging them would mean a class with two modes and a flag, which is the
// shape a class takes just before it becomes two classes anyway. They SHARE
// what actually deserves sharing — ai::configured(), ai::endpoint(),
// ai::chatRequestBody(), ai::requestHeaders(), ai::extractText() — so a
// dialect fix still lands in exactly one place.
//
// WHAT THIS FILE DOES NOT DECIDE: what to say, what history to send, what the
// reply means. Those are chat:: and brief::, both pure and both tested
// offline. What is left here is POST, timeout, status codes, staleness and
// cancellation — the parts that genuinely require a socket.
// ---------------------------------------------------------------------------

#include "LlmProvider.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include <optional>

class QNetworkReply;

class ChatClient : public QObject
{
    Q_OBJECT

public:
    explicit ChatClient(QObject* parent = nullptr);

    // Force a provider instead of reading Settings — for tests and for
    // pointing at a local stub server. std::optional because "no override"
    // and "an override that is deliberately empty" are different states.
    void setProviderOverride(const std::optional<ai::Provider>& p)
    {
        m_override = p;
    }

    // Force a key instead of ai::configuredKey() (v25.1). The Settings Test
    // button exists to test WHAT IS ON SCREEN — including a key typed but not
    // yet saved — and Cancel-writes-nothing forbids saving first just to
    // test. When set, this value is used VERBATIM (the caller composes any
    // field-vs-env fallback itself); optional for the same reason as the
    // provider override: an empty override is a real state ("test with no
    // key"), distinct from "no override".
    void setKeyOverride(const std::optional<QString>& key)
    {
        m_keyOverride = key;
    }

    // Fire one turn. `system` carries the instructions AND the day briefing;
    // `messages` is the already-windowed history, ending with what the person
    // just typed. Emits replied() or failed() exactly once — unless it is
    // superseded or cancelled, in which case: silence.
    //
    // v26 — ROUTED. Without a provider override, the turn walks
    // ai::routeFor(Feature::Chat): seats currently cooling down are skipped
    // (ai::breaker), a seat that proves UNREACHABLE moves to the next
    // (announced via seatUnreachable, so the transcript can say so), and any
    // REFUSED-class failure stops the walk loudly — a wrong key must never
    // be masked by a quieter seat. With an override set, exactly that one
    // seat is tried: the Settings Test button keeps meaning "test THIS".
    void send(const QString& system, const QList<ai::Message>& messages);

    // Abandon the in-flight turn. Bumps the generation FIRST, so the reply
    // that may already be on its way is discarded rather than raced with.
    void cancel();

    bool busy() const { return !m_inflight.isNull(); }

signals:
    // seatId names who answered; viaFallback is true when it was not the
    // route's first seat — the transcript attributes those answers, because
    // a conversation with two authors of different quality must say which
    // one said the thing you are about to act on (§E).
    void replied(const QString& text, const QString& seatId,
                 bool viaFallback);
    void failed(const QString& reason);
    // One seat down, another about to be tried — a local-only notice, never
    // part of the conversation the model sees.
    void seatUnreachable(const QString& seatId, const QString& nextSeatId);

private:
    QNetworkAccessManager       m_nam;
    QPointer<QNetworkReply>     m_inflight; // QPointer: nulls itself when the
                                            // reply is destroyed, so busy()
                                            // can never read a dangling ptr
    std::optional<ai::Provider> m_override;
    std::optional<QString>      m_keyOverride; // Settings Test button (v25.1)
    quint64                     m_generation = 0;

    // v26: fire one seat of the walk; on Unreachable, recurse to the next.
    void attempt(const QList<ai::Provider>& route, int index,
                 const QString& system, const QList<ai::Message>& messages);
};
