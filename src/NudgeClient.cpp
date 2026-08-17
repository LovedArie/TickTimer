#include "NudgeClient.h"

#include "NudgePhrasing.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

NudgeClient::NudgeClient(QObject* parent)
    : QObject(parent)
{
}

void NudgeClient::phrase(const QString& system, const QString& user,
                         int timeoutMs)
{
    const quint64 generation = ++m_generation; // supersede any in-flight ask

    // Read-at-fire-time, the house doctrine: the provider and key are
    // resolved when the request departs, not when the service was built.
    const ai::Provider provider =
        m_override ? *m_override : ai::configured();
    const QString key = ai::configuredKey(provider);

    // v28.10: the §E forcing hook finally reaches this wire. Until now a
    // working provider ALWAYS won here, which meant the v28.0 sentence —
    // the voice the whole arc guarantees — had never once been heard
    // outside a test. A seam only tests can reach is half a seam; forced
    // seats collapse into fallback() like every other failure, because
    // this client's personality is that it cannot fail loudly.
    if (ai::forcedDown(provider.id)) {
        emit fallback();
        return;
    }

    // Every "can't even try" case is the same case here. Quick-add turns
    // these into guidance ("set a key in Settings → AI") because a human
    // just pressed Ctrl+Enter and is waiting; nobody pressed anything for
    // a nudge, so guidance would be a notification about a notification.
    if ((provider.needsKey && key.isEmpty())
        || !provider.baseUrl.isValid() || provider.baseUrl.host().isEmpty()) {
        emit fallback();
        return;
    }

    const QJsonObject body = ai::requestBody(provider, system, user);
    QNetworkRequest request(ai::endpoint(provider));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    for (const auto& header : ai::requestHeaders(provider, key))
        request.setRawHeader(header.first, header.second);
    // The transfer timeout IS the exactly-once timeout: Qt aborts the
    // reply, finished() fires with an error, and the handler below turns
    // it into fallback(). No parallel QTimer to race against.
    request.setTransferTimeout(timeoutMs);

    const ai::Dialect dialect = provider.dialect;
    QNetworkReply* reply = m_nam.post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, dialect]() {
        reply->deleteLater();
        if (generation != m_generation)
            return; // superseded — the newer ask owns the outcome

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "nudge phrasing: network" << reply->errorString();
            emit fallback();
            return;
        }
        const ai::TextResult text =
            ai::extractText(dialect, reply->readAll());
        if (!text.ok) {
            qDebug() << "nudge phrasing: extract" << text.error;
            emit fallback();
            return;
        }
        const QString cleaned = nudge::accept(text.text);
        if (cleaned.isEmpty()) {
            qDebug() << "nudge phrasing: rejected by gate"
                     << text.text.left(80);
            emit fallback();
            return;
        }
        emit phrased(cleaned);
    });
}
