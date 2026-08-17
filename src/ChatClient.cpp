#include "ChatClient.h"

#include "ChatSession.h" // kReplyMaxTokens — the budget is a chat decision

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

ChatClient::ChatClient(QObject* parent)
    : QObject(parent)
{
}

void ChatClient::cancel()
{
    // Order matters: bump the generation BEFORE aborting. abort() makes the
    // reply emit finished() immediately, and the lambda's first act is to
    // compare generations — so raising it here is what turns "cancelled" into
    // silence instead of a spurious "couldn't reach the AI service".
    ++m_generation;
    if (m_inflight)
        m_inflight->abort();
    m_inflight.clear();
}

void ChatClient::send(const QString& system, const QList<ai::Message>& messages)
{
    if (messages.isEmpty()) {
        // Defensive, and cheap: a window() that trimmed everything (all
        // local-only turns, say) must not produce an empty messages array,
        // which every provider rejects with an unhelpful 400.
        emit failed(tr("nothing to send"));
        return;
    }

    cancel(); // one conversation, one in-flight turn

    // Route read at FIRE TIME, never cached — reorder seats in Settings and
    // the next message walks the new order, mid-conversation.
    QList<ai::Provider> route;
    if (m_override) {
        // An override IS the route: the Test button means "test THIS seat",
        // and a test that silently wandered to a fallback would vouch for a
        // setup nobody asked about.
        route.append(*m_override);
    } else {
        const QList<ai::Provider> full = ai::routeFor(ai::Feature::Chat);
        QStringList ids;
        for (const ai::Provider& p : full)
            ids.append(p.id);
        const QStringList planned =
            ai::planRoute(ids, ai::breaker(), QDateTime::currentDateTime());
        if (planned.isEmpty()) {
            // Every seat went unreachable within the last cooldown. Fail
            // FAST and say so — re-proving a dead network one timeout at a
            // time is exactly what the breaker exists to stop.
            QStringList names;
            for (const QString& id : ids)
                names.append(ai::seatName(id));
            emit failed(tr("%1 unreachable — will retry shortly")
                            .arg(names.join(QStringLiteral(", "))));
            return;
        }
        for (const ai::Provider& p : full) {
            if (planned.contains(p.id))
                route.append(p);
        }
    }

    attempt(route, 0, system, messages);
}

void ChatClient::attempt(const QList<ai::Provider>& route, int index,
                         const QString& system,
                         const QList<ai::Message>& messages)
{
    const quint64 generation = m_generation;

    const ai::Provider provider = route.at(index);
    const QString      key =
        m_keyOverride ? *m_keyOverride : ai::configuredKey(provider);
    const bool hasNext = index + 1 < route.size();

    // The §E forcing hook: a seat listed in TICKTIMER_AI_DOWN is unreachable
    // by decree, BEFORE any socket opens — the real fall-through machinery
    // (breaker note, notice, next seat), not a simulation of it.
    if (ai::forcedDown(provider.id)) {
        ai::breaker().noteUnreachable(provider.id,
                                      QDateTime::currentDateTime());
        if (hasNext) {
            emit seatUnreachable(provider.id, route.at(index + 1).id);
            attempt(route, index + 1, system, messages);
        } else {
            emit failed(tr("couldn't reach the AI service"));
        }
        return;
    }

    if (provider.needsKey && key.isEmpty()) {
        emit failed(tr("no API key set for %1 (Settings → AI)")
                        .arg(provider.displayName));
        return;
    }
    if (!provider.baseUrl.isValid() || provider.baseUrl.host().isEmpty()) {
        emit failed(tr("no server address set for %1 (Settings → AI)")
                        .arg(provider.displayName));
        return;
    }

    const QJsonObject body = ai::chatRequestBody(provider, system, messages,
                                                 chat::kReplyMaxTokens);

    QNetworkRequest request(ai::endpoint(provider));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    for (const auto& header : ai::requestHeaders(provider, key))
        request.setRawHeader(header.first, header.second);
    // 60 s, four times quick-add's budget. A local 8B model on a laptop CPU
    // genuinely takes half a minute to answer a question about a busy day,
    // and a timeout that fires on a working setup teaches users to distrust
    // the feature.
    request.setTransferTimeout(60000);

    const ai::Dialect dialect = provider.dialect; // the reply's shape

    QNetworkReply* reply =
        m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_inflight = reply;

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, dialect, route, index, system,
             messages, provider, hasNext]() {
        reply->deleteLater();
        if (generation != m_generation)
            return; // superseded or cancelled — an answer nobody is waiting for
        m_inflight.clear();

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            emit failed(tr("API key rejected — check Settings → AI"));
            return;
        }
        if (status == 429) {
            // Chat hits this where quick-add rarely did: several turns in a
            // minute is normal conversation, and free tiers count them.
            emit failed(tr("rate limited by the AI service — wait a moment"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            // The UNREACHABLE class — nothing answered (connection refused,
            // no route, transfer timeout). The ONLY class that falls
            // through: §E's rule, because everything below this line is a
            // server SAYING something, and what it says is config feedback
            // the user must hear.
            ai::breaker().noteUnreachable(provider.id,
                                          QDateTime::currentDateTime());
            if (hasNext) {
                emit seatUnreachable(provider.id, route.at(index + 1).id);
                attempt(route, index + 1, system, messages);
            } else {
                emit failed(tr("couldn't reach the AI service"));
            }
            return;
        }
        if (status == 404) {
            emit failed(tr("AI endpoint not found (404) — check the address"));
            return;
        }
        if (status != 200) {
            emit failed(tr("AI service error (%1)").arg(status));
            return;
        }

        // Bytes arrived; the ENVELOPE is the provider layer's business. Note
        // what does NOT happen next: no parsing, no field mapping. A chat
        // reply is prose, so the text IS the answer — which is precisely why
        // this client has no pure counterpart the way quick-add has nlp::llm.
        const ai::TextResult text = ai::extractText(dialect, reply->readAll());
        if (text.ok) {
            ai::breaker().noteOk(provider.id);
            emit replied(text.text.trimmed(), provider.id, index > 0);
        } else {
            // An unparseable 200 is REFUSED-class: the seat is reachable
            // and broken, which is a configuration conversation, not a
            // routing one.
            emit failed(text.error);
        }
    });
}
