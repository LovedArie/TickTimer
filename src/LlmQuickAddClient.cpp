#include "LlmQuickAddClient.h"

#include "LlmQuickAdd.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

LlmQuickAddClient::LlmQuickAddClient(QObject* parent)
    : QObject(parent)
{
}

QString LlmQuickAddClient::configuredKey()
{
    return ai::configuredKey(ai::configured());
}

void LlmQuickAddClient::parse(const QString& text, const QDate& today)
{
    const quint64 generation = ++m_generation; // supersede any in-flight ask

    // Read at FIRE TIME, never cached: a provider switch in Settings takes
    // effect on the next keystroke, not the next launch.
    const ai::Provider provider = m_override ? *m_override : ai::configured();
    const QString      key      = ai::configuredKey(provider);

    // v28.10: the §E forcing hook reaches this wire too — but with THIS
    // client's manners. A nudge fails into silence (nobody pressed
    // anything); quick-add fails into a visible preview bar (somebody is
    // waiting), so a forced-down seat names its cause instead of
    // impersonating a network error.
    if (ai::forcedDown(provider.id)) {
        emit failed(tr("%1 is forced down (TICKTIMER_AI_DOWN)")
                        .arg(provider.displayName));
        return;
    }

    if (provider.needsKey && key.isEmpty()) {
        // Fail fast, offline, and SAY WHERE THE FIX IS — the first failure
        // every new user hits, so the message carries the manual. It names
        // the provider, because "no API key set" is confusing the moment
        // there is more than one place a key could live.
        emit failed(tr("no API key set for %1 (Settings → AI)")
                        .arg(provider.displayName));
        return;
    }
    if (!provider.baseUrl.isValid() || provider.baseUrl.host().isEmpty()) {
        // The custom-endpoint footgun: a provider selected but never given an
        // address. Caught here rather than as an inscrutable network error.
        emit failed(tr("no server address set for %1 (Settings → AI)")
                        .arg(provider.displayName));
        return;
    }

    // Everything shaped by the PURE layer; this function only posts it.
    const QJsonObject body =
        ai::requestBody(provider, nlp::llm::systemPrompt(today), text);

    QNetworkRequest request(ai::endpoint(provider));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    for (const auto& header : ai::requestHeaders(provider, key))
        request.setRawHeader(header.first, header.second);
    request.setTransferTimeout(15000); // a capture bar cannot hang forever

    const ai::Dialect dialect = provider.dialect; // captured: the reply's shape

    QNetworkReply* reply =
        m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, dialect]() {
        reply->deleteLater();
        if (generation != m_generation)
            return; // a newer ask superseded this one — stale answer, drop it

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            emit failed(tr("API key rejected — check Settings → AI"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            emit failed(tr("couldn't reach the AI service"));
            return;
        }
        if (status == 404) {
            // Worth its own message: with a custom or self-hosted endpoint,
            // 404 almost always means the base URL is wrong (or is missing a
            // path prefix), not that anything is down.
            emit failed(tr("AI endpoint not found (404) — check the address"));
            return;
        }
        if (status != 200) {
            emit failed(tr("AI service error (%1)").arg(status));
            return;
        }

        // Bytes arrived; MEANING is the pure layer's job — told which
        // envelope to expect, because that is the one thing about a reply
        // only the request knew.
        const nlp::llm::Outcome outcome =
            nlp::llm::parseApiReply(reply->readAll(), dialect);
        if (outcome.ok)
            emit parsed(outcome.task);
        else
            emit failed(outcome.error);
    });
}
