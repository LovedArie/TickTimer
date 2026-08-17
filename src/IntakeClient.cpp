#include "IntakeClient.h"

#include "AppData.h"
#include "Category.h"
#include "Task.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

IntakeClient::IntakeClient(QObject* parent)
    : QObject(parent)
{
}

void IntakeClient::extract(const AppData& data, const Task& task,
                           const QDate& today, const QString& answer)
{
    const quint64 generation = ++m_generation;

    // Read at fire time, never cached — the house rule.
    const ai::Provider provider = m_override ? *m_override : ai::configured();
    const QString      key      = ai::configuredKey(provider);

    if (ai::forcedDown(provider.id)) {
        emit failed(tr("%1 is forced down (TICKTIMER_AI_DOWN)")
                        .arg(provider.displayName));
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

    QString areaName;
    if (const Category* c = data.categoryById(task.categoryId))
        areaName = c->name;
    const QJsonObject body = ai::requestBody(
        provider,
        intake::llm::systemPrompt(task, areaName,
                                  intake::historyGuess(data, task), today),
        answer);

    QNetworkRequest request(ai::endpoint(provider));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    for (const auto& header : ai::requestHeaders(provider, key))
        request.setRawHeader(header.first, header.second);
    request.setTransferTimeout(20000); // an interview can breathe a little
                                       // longer than a capture bar

    const ai::Dialect dialect = provider.dialect;

    QNetworkReply* reply =
        m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, generation, dialect]() {
        reply->deleteLater();
        if (generation != m_generation)
            return; // superseded — a stale extraction must not speak

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
        if (status != 200) {
            emit failed(tr("AI service error (%1)").arg(status));
            return;
        }

        const intake::llm::Extraction outcome =
            intake::llm::parseReply(reply->readAll(), dialect);
        if (outcome.ok)
            emit extracted(outcome.estimateMinutes, outcome.dueDate);
        else
            emit failed(outcome.error);
    });
}
