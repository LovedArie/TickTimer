#include "QuickCaptureOverlay.h"

#include "Widgets.h"

#include "AppData.h"
#include "LlmQuickAddClient.h"
#include "QuickAddParser.h"
#include "QuickAddPreview.h"

#include <QDate>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

QuickCaptureOverlay::QuickCaptureOverlay(AppData* data, QWidget* parent)
    : QDialog(parent)
    , m_data(data)
{
    // Frameless + translucent-free: a plain rounded panel. Qt::Dialog keeps it
    // a real window (focusable, Esc-closable via QDialog::reject) while the
    // frameless hint drops the OS title bar — it should feel like a command
    // palette, not a settings dialog.
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    // NOT modal (v21.2): modality swallows outside clicks, and the outside
    // click is now our dismiss gesture. The deactivate handler below is what
    // keeps capture a beat instead of a lingering mode.
    setObjectName("quickCapture");
    // A command palette is a line of text, not a screen. On a phone it takes
    // the full width (it needs it) and only the height it actually uses.
    setProperty("compactTopSheet", true);
    setStyleSheet(
        "#quickCapture { background: white; border: 1px solid #D8DDD6; "
        "border-radius: 12px; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(6);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(
        tr("Capture a task…  (\"call dentist tomorrow #health\")"));
    // 520 is a comfortable command-palette width on a desktop and simply
    // wider than a phone: a 360px screen cannot honour it, so the bar was
    // rendered off the edge with its right-hand side unreachable. A minimum
    // width is a PROMISE, and a promise this widget cannot keep on the device
    // it is running on is the bug, not the screen.
    m_input->setMinimumWidth(isCompactScreen() ? 0 : 520);
    m_input->setStyleSheet(
        "font-size: 15px; padding: 8px 10px; border: 1px solid #D8DDD6; "
        "border-radius: 8px;");
    layout->addWidget(m_input);

    m_preview = new QLabel(this);
    m_preview->setTextFormat(Qt::RichText);
    m_preview->setStyleSheet("font-size: 11px; padding-left: 2px;");
    m_preview->hide();
    layout->addWidget(m_preview);

    m_hint = new QLabel(this);
    m_hint->setStyleSheet("font-size: 10px; color: #8A9098;");
    layout->addWidget(m_hint);

    connect(m_input, &QLineEdit::textChanged,
            this, &QuickCaptureOverlay::updatePreview);
    connect(m_input, &QLineEdit::returnPressed,
            this, &QuickCaptureOverlay::commit);

    // v21.2 — the AI fallback, explicitly summoned (Ctrl+Enter), never
    // automatic: no surprise network calls, no latency on keystrokes, and the
    // deterministic parser stays the instant default. The event filter is how
    // we see Ctrl+Enter BEFORE the line edit turns it into returnPressed.
    m_ai = new LlmQuickAddClient(this);
    connect(m_ai, &LlmQuickAddClient::parsed,
            this, &QuickCaptureOverlay::onAiParsed);
    connect(m_ai, &LlmQuickAddClient::failed,
            this, &QuickCaptureOverlay::onAiFailed);
    m_input->installEventFilter(this);
}

// The default hint doubles as the manual — every gesture the overlay knows.
static QString defaultHint()
{
    return QObject::tr(
        "Enter adds · Ctrl+Enter asks AI ✨ · click away or Esc closes");
}

void QuickCaptureOverlay::popup()
{
    m_input->clear(); // stale half-typed thoughts don't survive a re-summon
    m_aiParse.reset();
    m_hint->setText(defaultHint());
    if (parentWidget()) {
        // Center in the upper third of the parent — palette position, eyes
        // barely move from wherever they were.
        const QRect p = parentWidget()->geometry();
        adjustSize();
        move(p.center().x() - width() / 2, p.top() + p.height() / 5);
    }
    show();
    raise();
    activateWindow(); // deactivation is our dismiss signal — start active
    m_input->setFocus();
}

bool QuickCaptureOverlay::event(QEvent* e)
{
    if (e->type() == QEvent::WindowDeactivate)
        close(); // click-away / alt-tab: attention left, so does the overlay
    return QDialog::event(e);
}

QString QuickCaptureOverlay::targetCategoryId(const QString& hint) const
{
    // Rule 1: an explicit '#tag' — the domain answers what the name means.
    if (!hint.isEmpty()) {
        const QString byName = m_data->categoryIdByName(hint);
        if (!byName.isEmpty())
            return byName;
    }
    // Rule 2: the remembered default — IF it still exists (categories can be
    // deleted between captures; a stale id must not become a ghost write).
    if (!m_defaultCategoryId.isEmpty()
        && m_data->categoryById(m_defaultCategoryId))
        return m_defaultCategoryId;
    // Rule 3: the first category; empty means "no categories at all".
    return m_data->categories().isEmpty() ? QString()
                                          : m_data->categories().first().id;
}

void QuickCaptureOverlay::updatePreview()
{
    // ANY edit invalidates an armed AI parse: the AI answered the OLD text,
    // and committing an answer to a question you rewrote is the drift bug the
    // preview exists to prevent.
    m_aiParse.reset();

    const QString text = m_input->text();
    if (text.trimmed().isEmpty()) {
        m_preview->hide();
        adjustSize();
        return;
    }

    const nlp::ParsedTask p =
        nlp::parseQuickAdd(text, QDate::currentDate());
    renderParse(p, /*fromAi=*/false);
}

void QuickCaptureOverlay::renderParse(const nlp::ParsedTask& p, bool fromAi)
{

    // The overlay ALWAYS shows the landing area (unlike the Activities input,
    // where the selected rail item is visible context): resolved-name in
    // focus green, or the raw hint + '?' when the tag matched nothing and the
    // fallback will catch it.
    const QString catId = targetCategoryId(p.categoryHint);
    const Category* c = m_data->categoryById(catId);
    const bool resolvedByHint =
        !p.categoryHint.isEmpty() && c
        && c->name.compare(p.categoryHint, Qt::CaseInsensitive) == 0;
    const QString chip = !p.categoryHint.isEmpty() && !resolvedByHint
                             ? p.categoryHint     // unknown tag: show it, '?'
                             : (c ? c->name : QString());

    const QString html = quickAddPreviewHtml(p, chip, resolvedByHint || c);
    // The ✨ marker is the provenance label: this interpretation came from the
    // model, not the grammar — trust it accordingly.
    m_preview->setText(fromAi ? QStringLiteral("\u2728 ") + html : html);
    m_preview->show();
    adjustSize();
}

void QuickCaptureOverlay::requestAiParse()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty())
        return;
    m_hint->setText(tr("\u2728 asking AI\u2026"));
    m_ai->parse(text, QDate::currentDate());
}

void QuickCaptureOverlay::onAiParsed(const nlp::ParsedTask& task)
{
    m_aiParse = task; // armed: the next Enter commits THIS, not a re-parse
    renderParse(task, /*fromAi=*/true);
    m_hint->setText(tr("\u2728 AI parse ready \u2014 Enter adds"));
}

void QuickCaptureOverlay::onAiFailed(const QString& reason)
{
    // The deterministic parse is still live and previewed — say so, so a
    // network hiccup never reads as "capture is broken".
    m_hint->setText(
        tr("\u2728 %1 \u2014 the regular parse below still works")
            .arg(reason));
}

bool QuickCaptureOverlay::eventFilter(QObject* watched, QEvent* e)
{
    // Ctrl+Enter must be OURS before QLineEdit converts it into
    // returnPressed — otherwise "ask the AI" would also commit.
    if (watched == m_input && e->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(e);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && key->modifiers().testFlag(Qt::ControlModifier)) {
            requestAiParse();
            return true; // consumed: no returnPressed, no commit
        }
    }
    return QDialog::eventFilter(watched, e);
}

void QuickCaptureOverlay::commit()
{
    // The armed AI parse wins; otherwise the deterministic parse of the text
    // as it stands. Either way it is exactly what the preview shows — any
    // edit since the AI reply already cleared m_aiParse (see updatePreview).
    const nlp::ParsedTask p =
        m_aiParse ? *m_aiParse
                  : nlp::parseQuickAdd(m_input->text(), QDate::currentDate());
    if (p.title.isEmpty())
        return; // nothing to name the task — leave the text for editing

    const QString catId = targetCategoryId(p.categoryHint);
    if (catId.isEmpty())
        return; // no categories exist; the Activities page is the fix

    const QString id = m_data->addTask(p.title, catId, p.dueDate, p.dueTime);
    if (!id.isEmpty()
        && (p.priority != Task::Priority::Medium
            || p.repeat != Task::Repeat::None)) {
        m_data->updateTask(id, p.title, QString(), p.dueDate, p.dueTime, p.repeat,
                           p.priority);
    }

    emit taskCaptured(catId); // MainWindow persists this as the new default
    m_aiParse.reset();

    // Batch mode: clear and stay. The flash in the hint line is the receipt —
    // enough feedback to trust the add, not enough to interrupt the dump.
    m_input->clear();
    const Category* c = m_data->categoryById(catId);
    m_hint->setText(tr("Added \"%1\" to %2 ✓ · Esc closes")
                        .arg(p.title, c ? c->name : QString()));
}
