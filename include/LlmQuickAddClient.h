#pragma once
// ---------------------------------------------------------------------------
// LlmQuickAddClient — the WIRE half of the AI fallback. Fourth client in the
// family (AuthClient, SyncClient, ShareClient), same rules: async
// QNetworkAccessManager, typed signals, and it knows nothing about widgets or
// AppData. Everything the reply MEANS is nlp::llm's job (pure, tested); this
// class only moves bytes and reports outcomes.
//
// v24 — WHAT THIS CLASS NO LONGER KNOWS. It used to hold five vendor facts:
// the host, the path, the header name, the model, and the reply shape. All
// five now live in ai::Provider, and this file asks for them:
//
//     ai::Provider p = ai::configured();
//     url     = ai::endpoint(p);
//     body    = ai::requestBody(p, systemPrompt(today), text);
//     headers = ai::requestHeaders(p, ai::configuredKey(p));
//
// What's left is the part that genuinely needs a network: POST, timeout,
// status codes, staleness. That is the whole point of the split — the file
// that can't be unit-tested is now too thin to hide a bug in.
//
// KEY HANDLING, per the pref-read-at-fire-time doctrine (the Pomodoro's
// notification rule): provider AND key are read fresh on every request.
// Change either in Settings and the very next Ctrl+Enter uses it; nothing is
// cached at construction. A provider that NEEDS a key and hasn't got one
// fails FAST and OFFLINE with a message naming that provider — while a local
// provider (Ollama, needsKey=false) is allowed to fire with no credential at
// all. That per-provider distinction is exactly what a single global "is the
// key set?" check got wrong.
//
// STALENESS: the user can keep typing while a request flies. Each parse()
// bumps a generation counter; a reply landing after a newer request started
// is dropped silently — an answer to a question you're no longer asking.
// ---------------------------------------------------------------------------

#include "LlmProvider.h"
#include "QuickAddParser.h"

#include <QDate>
#include <QNetworkAccessManager>
#include <QObject>
#include <optional>

class LlmQuickAddClient : public QObject
{
    Q_OBJECT
public:
    explicit LlmQuickAddClient(QObject* parent = nullptr);

    // The key for the CURRENTLY configured provider (QSettings, then that
    // provider's environment variable). Public so a caller can show "set a
    // key" guidance without firing a request.
    static QString configuredKey();

    // Force a provider instead of reading Settings — for tests and for
    // pointing at a local stub server. std::optional because "no override"
    // and "an override that happens to be empty" are different states, and a
    // sentinel value would conflate them.
    void setProviderOverride(const std::optional<ai::Provider>& p)
    {
        m_override = p;
    }

    // Fire one parse request. Emits parsed() or failed() exactly once —
    // unless a newer parse() supersedes this one, in which case: silence.
    void parse(const QString& text, const QDate& today);

signals:
    void parsed(const nlp::ParsedTask& task);
    void failed(const QString& reason);

private:
    QNetworkAccessManager   m_nam;
    std::optional<ai::Provider> m_override; // tests / local stub; normally empty
    quint64                 m_generation = 0; // stale-reply guard
};
