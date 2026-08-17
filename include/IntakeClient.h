#pragma once
// ---------------------------------------------------------------------------
// IntakeClient — the interview's wire (v29.1). LlmQuickAddClient's twin:
// fire-time provider read, the §E forcing hook honoured FROM BIRTH (the
// first client born after v28.10's lesson — no wire ships without it
// again), fail-fast key/address checks that say where the fix is, a
// generation counter so a superseded ask dies silently, and all MEANING
// delegated to the pure layer. Visible manners like quick-add's: someone
// just answered a question and is waiting, so failures name their cause.
// ---------------------------------------------------------------------------

#include "Intake.h"      // the pure layer — intake::llm
#include "LlmProvider.h"

#include <QDate>
#include <QNetworkAccessManager>
#include <QObject>

#include <optional>

class IntakeClient : public QObject
{
    Q_OBJECT
public:
    explicit IntakeClient(QObject* parent = nullptr);

    // The test/stub seam, LlmQuickAddClient's precedent verbatim.
    void setProviderOverride(const std::optional<ai::Provider>& p)
    {
        m_override = p;
    }

    // One extraction. Takes the data and the task so the prompt can fold
    // in the guess and the already-known due date (the pure layer's
    // contract). Emits extracted() or failed() exactly once — unless
    // superseded, in which case: silence.
    void extract(const class AppData& data, const struct Task& task,
                 const QDate& today, const QString& answer);

signals:
    void extracted(int estimateMinutes, const QDate& dueDate);
    void failed(const QString& reason);

private:
    QNetworkAccessManager       m_nam;
    std::optional<ai::Provider> m_override;
    quint64                     m_generation = 0;
};
