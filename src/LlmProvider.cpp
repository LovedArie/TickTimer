#include "LlmProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QProcessEnvironment>
#include <QSettings>

namespace ai
{
namespace
{
// One place for the shaping constant. 300 tokens is generous for a one-line
// task JSON and cheap on every provider; it exists to bound a runaway reply,
// not to be tuned.
constexpr int kMaxTokens = 300;

// Settings tokens, spelled once. Typo-in-a-string-literal is the classic
// QSettings bug: nothing fails, the value simply never comes back.
const QLatin1String kProviderKey("ai/provider");
const QLatin1String kCustomUrlKey("ai/customBaseUrl");
const QLatin1String kCustomDialectKey("ai/customDialect");
const QLatin1String kLegacyKeyKey("ai/anthropicApiKey"); // v21.2–v23
const QLatin1String kLegacyModelKey("ai/model");         // v21.2–v23
} // namespace

QString dialectToString(Dialect d)
{
    return d == Dialect::Anthropic ? QStringLiteral("anthropic")
                                   : QStringLiteral("openai");
}

Dialect dialectFromString(const QString& s)
{
    // Unknown degrades to OpenAI, not to Anthropic: if someone types a
    // dialect name we don't know, the OpenAI shape is the one a random
    // third-party endpoint is most likely to actually speak.
    return s.trimmed().toLower() == QLatin1String("anthropic")
               ? Dialect::Anthropic
               : Dialect::OpenAi;
}

QList<Provider> catalog()
{
    // MODEL NAMES AGE. Every default below was current when written and will
    // eventually not be; that is exactly why "ai/model/<id>" exists, and why
    // a wrong model name produces a plain HTTP error the user can read rather
    // than a mystery. Cheap-and-fast is the right class of model here: we are
    // parsing one line, not writing an essay.
    return {
        Provider{QStringLiteral("anthropic"),
                 QStringLiteral("Anthropic"),
                 QUrl(QStringLiteral("https://api.anthropic.com")),
                 Dialect::Anthropic,
                 QStringLiteral("claude-haiku-4-5"),
                 QStringLiteral("claude-haiku-4-5"),
                 QByteArrayLiteral("ANTHROPIC_API_KEY"),
                 true,
                 QStringLiteral("sk-ant-…")},

        Provider{QStringLiteral("openai"),
                 QStringLiteral("OpenAI"),
                 QUrl(QStringLiteral("https://api.openai.com")),
                 Dialect::OpenAi,
                 QStringLiteral("gpt-4o-mini"),
                 QStringLiteral("gpt-4o-mini"),
                 QByteArrayLiteral("OPENAI_API_KEY"),
                 true,
                 QStringLiteral("sk-…")},

        // Note the PATH on the base URL. Groq serves its OpenAI-compatible
        // API under /openai, which is precisely the case QUrl::resolved()
        // would have quietly destroyed — see endpoint() below.
        Provider{QStringLiteral("groq"),
                 QStringLiteral("Groq"),
                 QUrl(QStringLiteral("https://api.groq.com/openai")),
                 Dialect::OpenAi,
                 QStringLiteral("llama-3.1-8b-instant"),
                 QStringLiteral("llama-3.1-8b-instant"),
                 QByteArrayLiteral("GROQ_API_KEY"),
                 true,
                 QStringLiteral("gsk_…")},

        // The one that costs nothing and leaves the machine never. needsKey
        // is false, so the client must not refuse to fire for want of a
        // credential — the "no key set" guard is per-provider, not global.
        // sendThinkFlag is true HERE AND ONLY HERE: Ollama honours or ignores
        // "think"; OpenAI proper 400s on it. Best-effort by design — the
        // reliable off-switch is a Modelfile, and the strip in extractText
        // is the guarantee either way.
        Provider{QStringLiteral("ollama"),
                 QObject::tr("Ollama (local)"),
                 QUrl(QStringLiteral("http://localhost:11434")),
                 Dialect::OpenAi,
                 QStringLiteral("llama3.2"),
                 QStringLiteral("llama3.2"),
                 QByteArray(),
                 false,
                 QString(),
                 true},

        // The escape hatch: any endpoint speaking either dialect. Its base
        // URL, dialect and model all come from Settings; the catalog entry
        // only supplies the shape and a neutral env var.
        Provider{QStringLiteral("custom"),
                 QObject::tr("Custom endpoint…"),
                 QUrl(),
                 Dialect::OpenAi,
                 QString(),
                 QString(),
                 QByteArrayLiteral("LLM_API_KEY"),
                 false, // a custom server may or may not want a key
                 QStringLiteral("optional")},
    };
}

Provider byId(const QString& id)
{
    const QList<Provider> all = catalog();
    for (const Provider& p : all) {
        if (p.id == id)
            return p;
    }
    // Repair on read: an unknown id (hand-edited file, a provider we removed,
    // a downgrade) degrades to the first entry instead of bricking quick-add.
    return all.first();
}

// PLURAL group names, and that is not a style choice — it is a QSettings
// rule, learned here by a failing test. QSettings uses '/' as a group
// separator, and a name cannot be BOTH a value and a group: with the legacy
// value "ai/model" present, writing "ai/model/anthropic" asks for a group
// called "model" where a value called "model" already lives. The write is
// silently lost — no error, no warning, just an empty string on read.
// "ai/models/<id>" cannot collide with anything v21 wrote. The migration
// below also removes the legacy entry BEFORE writing the new one, so even a
// future name clash resolves in the right order.
QString settingsKeyForKey(const QString& providerId)
{
    return QStringLiteral("ai/keys/") + providerId;
}

QString settingsKeyForModel(const QString& providerId)
{
    return QStringLiteral("ai/models/") + providerId;
}

void migrateLegacySettings()
{
    QSettings s;

    // The credential. Copy only into an empty destination — running twice
    // must not resurrect a key the user has since cleared — then REMOVE the
    // legacy entry. One copy of a secret, not two drifting ones. The cost is
    // honest: downgrading to v23 loses the key from Settings (one paste to
    // restore, and the env var still works). Two live copies of a credential
    // is the worse of the two problems.
    const QString legacyKey = s.value(kLegacyKeyKey).toString().trimmed();
    if (!legacyKey.isEmpty()) {
        const QString dest = settingsKeyForKey(QStringLiteral("anthropic"));
        const bool destEmpty = s.value(dest).toString().trimmed().isEmpty();
        s.remove(kLegacyKeyKey); // FIRST: a value can't coexist with a group
        if (destEmpty)
            s.setValue(dest, legacyKey);
    }

    // The model override was global; it can only ever have meant Anthropic,
    // since Anthropic was the only provider that existed.
    const QString legacyModel = s.value(kLegacyModelKey).toString().trimmed();
    if (!legacyModel.isEmpty()) {
        const QString dest = settingsKeyForModel(QStringLiteral("anthropic"));
        const bool destEmpty = s.value(dest).toString().trimmed().isEmpty();
        s.remove(kLegacyModelKey);
        if (destEmpty)
            s.setValue(dest, legacyModel);
    }
}

Provider resolved(const QString& id)
{
    QSettings s;
    Provider  p = byId(id);

    // "custom" carries no built-in address or dialect — it is defined
    // entirely by what the user typed.
    if (p.id == QLatin1String("custom")) {
        const QString url = s.value(kCustomUrlKey).toString().trimmed();
        if (!url.isEmpty())
            p.baseUrl = QUrl(url);
        p.dialect =
            dialectFromString(s.value(kCustomDialectKey).toString());
    }

    const QString override =
        s.value(settingsKeyForModel(p.id)).toString().trimmed();
    if (!override.isEmpty())
        p.model = override;

    return p;
}

Provider configured()
{
    // v26: configured() is now "resolve whatever the primary-seat key names"
    // — the same overlay path every route seat goes through, factored out so
    // a custom endpoint in a FALLBACK position picks up its address and
    // dialect exactly like a custom endpoint in the primary one.
    QSettings s;
    return resolved(
        s.value(kProviderKey, QStringLiteral("anthropic")).toString());
}

// ---- per-role routing (v26) -----------------------------------------------

QString settingsKeyRoute(Feature role)
{
    switch (role) {
    case Feature::QuickAdd: return QStringLiteral("ai/route/quickadd");
    case Feature::Chat:     return QStringLiteral("ai/route/chat");
    }
    return QStringLiteral("ai/route/chat"); // unreachable; keeps -Wreturn-type honest
}

QStringList configuredRouteIds(Feature role)
{
    QSettings s;
    const QStringList stored =
        s.value(settingsKeyRoute(role)).toStringList();

    // Repair on read: keep only ids the catalog knows, drop duplicates but
    // KEEP ORDER (a route is an ordered preference, not a set).
    QStringList ids;
    for (const QString& id : stored) {
        bool known = false;
        for (const Provider& p : catalog())
            known = known || p.id == id;
        if (known && !ids.contains(id))
            ids.append(id);
    }

    // MIGRATION BY DERIVATION, deliberately not by write. The §E note
    // suggested copy-once-and-remove-the-legacy-key; this is stronger and
    // more house: a missing or fully-broken route key MEANS "the one seat
    // the v25 key names", computed fresh every read. Nothing is copied
    // (nothing can be copied twice), nothing is removed (a downgrade to
    // v25.3 finds its key untouched), and `ai/provider` stays meaningful —
    // it is the PRIMARY seat, which Settings still edits directly.
    if (ids.isEmpty())
        ids.append(configured().id);
    return ids;
}

QList<Provider> routeFor(Feature role)
{
    QList<Provider> out;
    const QStringList ids = configuredRouteIds(role);
    out.reserve(ids.size());
    for (const QString& id : ids)
        out.append(resolved(id));
    return out;
}

QString settingsKeySeatName(const QString& providerId)
{
    return QStringLiteral("ai/seatName/") + providerId;
}

QString seatName(const QString& providerId)
{
    // §E.5: ONE display string for every surface that names a seat —
    // Settings rows, transcript notices, and (later) Test-all verdicts.
    // The override is COSMETIC AND NEVER A KEY: renaming a seat must not
    // re-route anything, which is why everything else in this file keeps
    // trafficking in ids.
    QSettings s;
    const QString custom =
        s.value(settingsKeySeatName(providerId)).toString().trimmed();
    return custom.isEmpty() ? byId(providerId).displayName : custom;
}

bool forcedDown(const QString& providerId)
{
    // TICKTIMER_AI_DOWN="anthropic,ollama" — the §E forcing hook, mirroring
    // TICKTIMER_COMPACT=1: testing a fallback path must not require
    // unplugging a router. Seats named here are treated as instantly
    // unreachable BEFORE any socket opens, so tests (and curious users)
    // exercise the real fall-through machinery, not a simulation of it.
    // Read per call, NOT cached in a static: tests set and clear this
    // between cases, and a cached first read would make the hook a
    // one-shot — the classic static-initialization trap wearing a QString.
    const QString list = qEnvironmentVariable("TICKTIMER_AI_DOWN");
    // v28.10: "*" downs EVERY seat — the debug panel's one checkbox. A
    // wildcard beats enumerating catalog ids in a second place that would
    // then have to agree with the catalog forever (the installer's version
    // lesson, §mechanism-over-intention, applied to a list of strings).
    if (list.trimmed() == QLatin1String("*"))
        return true;
    return list.split(QLatin1Char(','), Qt::SkipEmptyParts)
        .contains(providerId);
}

void Breaker::noteUnreachable(const QString& providerId, const QDateTime& when)
{
    m_lastUnreachable.insert(providerId, when);
}

void Breaker::noteOk(const QString& providerId)
{
    m_lastUnreachable.remove(providerId);
}

bool Breaker::coolingDown(const QString& providerId, const QDateTime& now,
                          int coolDownSecs) const
{
    const auto it = m_lastUnreachable.constFind(providerId);
    if (it == m_lastUnreachable.constEnd())
        return false;
    return it.value().secsTo(now) < coolDownSecs;
}

Breaker& breaker()
{
    // One per process, shared by every wire client: seat health is a fact
    // about the MACHINE's connectivity, not about any one conversation.
    // In-memory on purpose — persisted breaker state would mean an app
    // restart still refuses a seat that came back an hour ago.
    static Breaker instance;
    return instance;
}

QStringList planRoute(const QStringList& ids, const Breaker& b,
                      const QDateTime& now)
{
    // Skip seats that failed as unreachable within the cooldown — the §E
    // rule that keeps offline mode from paying a failed connection on every
    // message. If EVERY seat is cooling, return empty and let the caller
    // fail fast with a named error: for up to one cooldown the app answers
    // "unreachable" instantly instead of re-proving it, which is the point.
    QStringList out;
    for (const QString& id : ids) {
        if (!b.coolingDown(id, now))
            out.append(id);
    }
    return out;
}

QString envKey(const Provider& p)
{
    if (p.envVar.isEmpty())
        return QString();
    return QProcessEnvironment::systemEnvironment()
        .value(QString::fromLatin1(p.envVar))
        .trimmed();
}

QString configuredKey(const Provider& p)
{
    // Settings first (the user-facing path), environment second (the
    // developer path). Read fresh every call — never cached.
    const QString fromSettings =
        QSettings().value(settingsKeyForKey(p.id)).toString().trimmed();
    if (!fromSettings.isEmpty())
        return fromSettings;
    return envKey(p);
}

QString requestPath(Dialect d)
{
    switch (d) {
    case Dialect::Anthropic:
        return QStringLiteral("/v1/messages");
    case Dialect::OpenAi:
        return QStringLiteral("/v1/chat/completions");
    }
    return QStringLiteral("/v1/chat/completions"); // unreachable; keeps GCC quiet
}

QUrl endpoint(const Provider& p)
{
    // Concatenate, do not resolve. See the header: resolved() would delete
    // Groq's "/openai" path prefix and turn a working config into a 404.
    QString base = p.baseUrl.toString();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return QUrl(base + requestPath(p.dialect));
}

QJsonObject chatRequestBody(const Provider& p, const QString& system,
                            const QList<Message>& messages, int maxTokens)
{
    // The turns, in the shape BOTH dialects share. This is the pleasant
    // surprise of multi-turn: the per-message object is identical
    // ({role, content}); only where the SYSTEM prompt goes differs.
    QJsonArray turns;
    for (const Message& m : messages) {
        turns.append(QJsonObject{
            {QStringLiteral("role"), m.role == Role::User
                                         ? QStringLiteral("user")
                                         : QStringLiteral("assistant")},
            {QStringLiteral("content"), m.text},
        });
    }

    switch (p.dialect) {
    case Dialect::Anthropic:
        // The system prompt is a TOP-LEVEL field here, not a message.
        return QJsonObject{
            {QStringLiteral("model"), p.model},
            {QStringLiteral("max_tokens"), maxTokens},
            {QStringLiteral("system"), system},
            {QStringLiteral("messages"), turns},
        };

    case Dialect::OpenAi: {
        // …and a system MESSAGE here, first in the array. That one structural
        // difference is most of what "dialect" means.
        //
        // max_tokens over max_completion_tokens on purpose: the newer name is
        // OpenAI-only, while every compatible server (Groq, Ollama, LM Studio,
        // OpenRouter) still accepts the old one. Targeting the dialect means
        // targeting the common denominator, not the newest vendor doc.
        QJsonArray withSystem{QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), system}}};
        for (const QJsonValue& t : std::as_const(turns))
            withSystem.append(t);
        QJsonObject body{
            {QStringLiteral("model"), p.model},
            {QStringLiteral("max_tokens"), maxTokens},
            {QStringLiteral("messages"), withSystem},
        };
        // v25.2: opt-in per catalog entry, NOT per dialect — see the header.
        // "think" is an Ollama extension; OpenAI proper rejects unknown
        // fields, so this must never ride along on a cloud request.
        if (p.sendThinkFlag)
            body.insert(QStringLiteral("think"), false);
        return body;
    }
    }
    return {};
}

QJsonObject requestBody(const Provider& p, const QString& system,
                        const QString& userText)
{
    // A one-shot IS a conversation with one turn in it. Writing it this way
    // deletes the duplicate dialect switch that would otherwise drift: fix a
    // dialect bug once, and quick-add and chat both get the fix.
    return chatRequestBody(p, system, {Message{Role::User, userText}},
                           kMaxTokens);
}

QList<QPair<QByteArray, QByteArray>> requestHeaders(const Provider& p,
                                                    const QString& key)
{
    QList<QPair<QByteArray, QByteArray>> headers;
    switch (p.dialect) {
    case Dialect::Anthropic:
        if (!key.isEmpty())
            headers.append({QByteArrayLiteral("x-api-key"), key.toUtf8()});
        // The API version is pinned, not "latest": a date-stamped contract
        // means a vendor change can't silently reshape our replies.
        headers.append(
            {QByteArrayLiteral("anthropic-version"), QByteArrayLiteral("2023-06-01")});
        break;
    case Dialect::OpenAi:
        // No key -> no header at all. A local Ollama with an empty
        // "Authorization: Bearer " can reject the request outright; sending
        // nothing is the honest description of having nothing.
        if (!key.isEmpty())
            headers.append({QByteArrayLiteral("Authorization"),
                            QByteArrayLiteral("Bearer ") + key.toUtf8()});
        break;
    }
    return headers;
}

QString strippedOfThinking(const QString& text)
{
    // Plain scanning, not a regex. The lazy-match regex for this
    // (<think>.*?</think> with DotMatchesEverything) is the classic place a
    // pathological reply costs quadratic time, and a MISSING closer would
    // make it match nothing — exactly backwards, since an unclosed span is
    // the streaming-truncation case we most need to catch. Two indexOf calls
    // per span state the intent and have no such cliffs.
    //
    // Case-insensitive because the tag is model output, not a spec: it has
    // been observed as <think> but there is no grammar guaranteeing case.
    static const QLatin1String kOpen("<think>");
    static const QLatin1String kClose("</think>");

    QString out = text;
    while (true) {
        const int open = out.indexOf(kOpen, 0, Qt::CaseInsensitive);
        if (open < 0)
            break;
        const int close = out.indexOf(kClose, open, Qt::CaseInsensitive);
        if (close < 0) {
            // Unclosed: the model died (or was cut off) mid-deliberation.
            // Everything from <think> on is deliberation; drop it all rather
            // than leak half of it. The caller's empty-check + reasoning
            // fallback then decide whether anything answerable remains.
            out.truncate(open);
            break;
        }
        out.remove(open, close + kClose.size() - open);
    }
    return out.trimmed();
}

TextResult extractText(Dialect d, const QByteArray& body)
{
    TextResult out;

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        out.error = QObject::tr("AI reply was not JSON");
        return out;
    }
    const QJsonObject root = doc.object();

    switch (d) {
    case Dialect::Anthropic: {
        // { content: [ {type:"text", text:…}, … ] } — walk to the first text
        // block rather than indexing [0]: a reply may lead with another block
        // type, and content[0].text would then be silently empty.
        const QJsonArray content =
            root.value(QStringLiteral("content")).toArray();
        for (const QJsonValue& block : content) {
            const QJsonObject o = block.toObject();
            if (o.value(QStringLiteral("type")).toString()
                == QLatin1String("text")) {
                out.text = o.value(QStringLiteral("text")).toString();
                break;
            }
        }
        break;
    }
    case Dialect::OpenAi: {
        // { choices: [ {message:{role,content}} ] } — take the first choice;
        // we never request n>1.
        const QJsonArray choices =
            root.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject msg = choices.first()
                                        .toObject()
                                        .value(QStringLiteral("message"))
                                        .toObject();
            out.text = msg.value(QStringLiteral("content")).toString();

            // v25.2 — the silent-content failure. Some reasoning setups
            // (DeepSeek-R1 style, and Ollama configurations of Qwen3) route
            // ALL text into a side field and leave content empty; before
            // this, the app reported "no text content" and discarded a good
            // answer. Fall back ONLY when content is empty after the scrub
            // — content always beats reasoning when both hold text, because
            // reasoning is deliberation and pasting it above an answer would
            // be the leak bug reintroduced by the fix for the other one.
            // Two spellings, because the field predates any standard.
            if (strippedOfThinking(out.text).isEmpty()) {
                const QString r =
                    msg.value(QStringLiteral("reasoning")).toString();
                out.text =
                    !r.trimmed().isEmpty()
                        ? r
                        : msg.value(QStringLiteral("reasoning_content"))
                              .toString();
            }
        }
        break;
    }
    }

    // The scrub runs on BOTH dialects' output, after the switch. Anthropic
    // proper never inline-tags (its thinking is a structured block, already
    // skipped above) — but a Custom endpoint may claim either dialect while
    // proxying a model that does, and a strip that finds no tag costs one
    // failed indexOf. This also scrubs the reasoning-fallback text, which
    // can itself carry tags.
    out.text = strippedOfThinking(out.text);

    if (out.text.isEmpty()) {
        out.error = QObject::tr("AI reply had no text content");
        return out;
    }
    out.ok = true;
    return out;
}

} // namespace ai
