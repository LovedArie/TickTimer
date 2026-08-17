#pragma once
// ---------------------------------------------------------------------------
// QuickCaptureOverlay — global capture: press Ctrl+N ANYWHERE in the app and a
// floating input appears; type a natural-language line, Enter commits it, the
// input clears for the next one (brain-dump batching), Esc dismisses.
//
// WHY AN OVERLAY, not a bar on every page: capture is rare-but-urgent. A
// persistent bar pays screen rent on every page for the 1% of moments you
// need it; a summoned overlay costs nothing until the thought strikes — and
// when it does, the thought goes straight from head to list without touching
// the mouse or leaving the current page. (For an ADHD workflow that gap —
// "I'll add it when I get to the Activities page" — is precisely where tasks
// die.)
//
// THE CATEGORY QUESTION: the Activities input lives inside a life area, so
// "which category?" answers itself. The overlay has no such context. Rules,
// in order:
//   1. a '#tag' routes explicitly (AppData::categoryIdByName — exact,
//      case-insensitive);
//   2. otherwise the DEFAULT set via setDefaultCategoryId() — MainWindow
//      passes the last category captured into (persisted in QSettings), so
//      the bar remembers where you usually throw things;
//   3. otherwise the first category.
// The preview ALWAYS shows the landing area — with no visible context, saying
// where the task will go isn't a nicety, it's the whole trust story.
//
// Commit is the same addTask + updateTask pair the Activities input uses, and
// the parse is the same pure function — three surfaces, one interpretation.
// ---------------------------------------------------------------------------

#include "QuickAddParser.h" // ParsedTask (also carried by the AI reply)

#include <QDialog>
#include <optional>

class AppData;
class LlmQuickAddClient;
class QLabel;
class QLineEdit;

class QuickCaptureOverlay : public QDialog
{
    Q_OBJECT

public:
    explicit QuickCaptureOverlay(AppData* data, QWidget* parent = nullptr);

    // The no-#tag fallback (rule 2 above). Stored as an ID, resolved at
    // commit time — if the category has been deleted meanwhile, commit falls
    // through to rule 3 instead of writing to a ghost.
    void setDefaultCategoryId(const QString& id) { m_defaultCategoryId = id; }

    // Summon: reset, center over the parent window, focus the input.
    void popup();

protected:
    bool eventFilter(QObject* watched, QEvent* e) override; // Ctrl+Enter hook
    // Click-away = dismiss (v21.2). Losing window activation — a click on the
    // main window, another app, an alt-tab — closes the overlay. Capture is a
    // beat, not a mode: if your attention left, the overlay's job is over.
    // (Esc still works via QDialog::reject; this just adds the natural exit.)
    bool event(QEvent* e) override;

signals:
    // Fired after each successful commit with the category the task landed
    // in — MainWindow persists it as the next default ("capture memory").
    void taskCaptured(const QString& categoryId);

private slots:
    void updatePreview();
    void commit();
    // v21.2 — the AI fallback. requestAiParse fires on Ctrl+Enter; the two
    // handlers land the client's reply. They are SLOTS (not lambdas) so tests
    // can drive the success path via QMetaObject::invokeMethod without a
    // network — the same seam the signal connection uses.
    void requestAiParse();
    void onAiParsed(const nlp::ParsedTask& task);
    void onAiFailed(const QString& reason);

private:
    // Render one parse into the preview label (shared by the live
    // deterministic path and the AI path — one readout, per the v21.1 rule).
    void renderParse(const nlp::ParsedTask& p, bool fromAi);

    // Where a commit would land RIGHT NOW (rules 1..3). Empty only when the
    // app has no categories at all — the one state capture cannot help.
    QString targetCategoryId(const QString& hint) const;

    AppData*   m_data;
    QLineEdit* m_input   = nullptr;
    QLabel*    m_preview = nullptr;
    QLabel*    m_hint    = nullptr; // "Enter adds · Esc closes" + added-flash
    QString    m_defaultCategoryId;

    // The AI parse currently "armed" for commit. std::optional is the honest
    // type: there either IS an AI interpretation of the current text or there
    // isn't — and ANY edit clears it, because the AI answered the OLD text.
    LlmQuickAddClient*             m_ai = nullptr;
    std::optional<nlp::ParsedTask> m_aiParse;
};
