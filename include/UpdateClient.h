#pragma once

#include <QNetworkAccessManager>
#include <QObject>

// ---------------------------------------------------------------------------
// UpdateClient — the fourth (and simplest) wire sibling: one GET, one signal.
//
// By now the family recipe writes itself — async QNAM, clearConnectionCache
// before the request (QB M3), status from HttpStatusCodeAttribute never
// reply->error() (QB M1), a typed outcome — and that's the quiet payoff of
// three earlier clients: the fourth is nearly boilerplate, and boring code
// is code you can trust.
//
// One family rule matters MORE here than anywhere: every non-Success
// outcome must end in silence. An update check the user didn't ask for has
// no right to bother them with its failures — server down, no version.json,
// airplane mode: all invisible. Only "there IS something newer" may speak,
// and even that decision belongs to version::decideBanner, not to this
// class. Wire fetches; the pure layer judges; the banner renders.
// ---------------------------------------------------------------------------

class UpdateClient : public QObject
{
    Q_OBJECT
public:
    explicit UpdateClient(const QString& serverUrl, QObject* parent = nullptr);

    enum class Outcome {
        Success,       // server advertised a version (may still be ours!)
        Unavailable,   // no version.json / bad answer — feature unconfigured
        NetworkError   // couldn't reach the server
    };
    Q_ENUM(Outcome)

    void checkForUpdate(); // -> checkFinished

signals:
    void checkFinished(UpdateClient::Outcome outcome, const QString& latest,
                       const QString& url, const QString& notes);

private:
    QNetworkAccessManager m_net;
    QString               m_url;
};
