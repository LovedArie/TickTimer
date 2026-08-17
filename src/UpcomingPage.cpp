#include "UpcomingPage.h"

#include "AppData.h"
#include "TaskCardDelegate.h"
#include "TaskDetailDialog.h"
#include "TaskFilterProxy.h"
#include "TaskListModel.h"
#include "Theme.h"
#include "Widgets.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QLabel>
#include <QListView>
#include <QScroller>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

UpcomingPage::UpcomingPage(AppData* data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(26, 22, 26, 22);

    // Everything lives in one left-aligned, width-capped panel — the same
    // wrapLeft + maxWidth framing the page had before, so the visual home of
    // the list is unchanged even though its machinery is completely new.
    auto* panel = new QFrame;
    panel->setObjectName("panel");
    // v22.1: NO width cap on the panel. Two rounds of "still too small"
    // taught the actual lesson — this is the page's ONLY content, so the
    // page is the frame and the panel simply fills it. Line-length restraint
    // moved to the one place it belongs: the delegate's kMaxCardW, which
    // caps the CARDS (the text) while the panel uses the window it was
    // given. One reader-comfort rule, one owner, instead of two caps
    // fighting about the same pixels.
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(8);

    auto* title = new QLabel(tr("Upcoming"), panel);
    title->setObjectName("h2");
    auto* sub = new QLabel(
        tr("Every dated, unfinished task across all your life areas — "
           "derived live, stored nowhere."),
        panel);
    sub->setObjectName("sub");
    sub->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(sub);

    // ---- the pipeline (built once, never rebuilt) --------------------------
    m_model = new TaskListModel(m_data, this);
    m_proxy = new TaskFilterProxy(this);
    m_proxy->setSourceModel(m_model);

    m_delegate = new TaskCardDelegate(this);
    // The delegate reports intent; the page turns intent into a domain call —
    // the same thin-shell contract the old TaskRow honoured, now for a painted
    // row. setTaskDone/removeTask fire changed(), the model re-snapshots, the
    // view repaints: one loop, no hand-removal.
    connect(m_delegate, &TaskCardDelegate::doneToggled,
            this, [this](const QString& id, bool on) {
                m_data->setTaskDone(id, on);
            });
    connect(m_delegate, &TaskCardDelegate::deleteRequested,
            this, [this](const QString& id) { m_data->removeTask(id); });
    connect(m_delegate, &TaskCardDelegate::editRequested,
            this, &UpcomingPage::editTask);

    m_view = new QListView(panel);
    m_view->setModel(m_proxy);
    m_view->setItemDelegate(m_delegate);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->viewport()->setAutoFillBackground(false);
    // Click-to-act, not click-to-select: no selection highlight to fight the
    // card aesthetic (the delegate draws the only state that matters).
    m_view->setSelectionMode(QAbstractItemView::NoSelection);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // ResizeMode::Adjust relayouts items when the view is resized, so the
    // delegate's width-following sizeHint stays correct as the window changes.
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMouseTracking(true);
    QScroller::grabGesture(m_view->viewport(), // finger-flick on touch screens
                           QScroller::LeftMouseButtonGesture);

    m_empty = new QLabel(panel);
    m_empty->setObjectName("encourage");
    m_empty->setWordWrap(true);
    m_empty->setTextFormat(Qt::RichText);
    // Centred, because it is now the only thing on a large surface — a
    // top-left sentence in a big panel reads as a leftover, not a message.
    m_empty->setAlignment(Qt::AlignCenter);

    m_stack = new QStackedWidget(panel);
    m_stack->addWidget(m_view);  // index 0
    m_stack->addWidget(m_empty); // index 1

    // ---- the four lenses: All / Urgent / Medium / Low ----------------------
    // Same checkable + autoExclusive QToolButtons as before (the nav rail's
    // radio idiom), but clicking one now just re-points the proxy's filter —
    // no list rebuild, no widget churn.
    auto* tabs = new QHBoxLayout;
    tabs->setSpacing(6);
    const struct { QString label; int filter; } lenses[] = {
        {tr("All"),    -1},
        {tr("Urgent"), int(Task::Priority::Urgent)},
        {tr("Medium"), int(Task::Priority::Medium)},
        {tr("Low"),    int(Task::Priority::Low)},
    };
    for (const auto& lens : lenses) {
        auto* b = new QToolButton(panel);
        b->setObjectName("nav");
        b->setText(lens.label);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setChecked(lens.filter == -1); // "All" starts selected
        b->setCursor(Qt::PointingHandCursor);
        const int f = lens.filter;
        connect(b, &QToolButton::clicked, this, [this, f]() {
            m_proxy->setPriorityFilter(f);
            refreshEmptyState();
        });
        tabs->addWidget(b);
    }
    tabs->addStretch(1);

    layout->addSpacing(6);
    layout->addLayout(tabs);
    layout->addSpacing(10);
    layout->addWidget(m_stack, 1); // the list eats every spare pixel

    // Straight in with stretch 1 — no wrapLeft, because there is no width
    // cap left for it to align against; the panel and the page are now the
    // same rectangle.
    outer->addWidget(panel, 1);

    // The list toggles to the empty message whenever its visible count hits
    // zero — driven by the proxy's own signals, so a completed task or a
    // filtered-out lens both flip it without the page polling anything.
    connect(m_proxy, &QAbstractItemModel::modelReset,
            this, &UpcomingPage::refreshEmptyState);
    connect(m_proxy, &QAbstractItemModel::rowsInserted,
            this, &UpcomingPage::refreshEmptyState);
    connect(m_proxy, &QAbstractItemModel::rowsRemoved,
            this, &UpcomingPage::refreshEmptyState);
    connect(m_proxy, &QAbstractItemModel::layoutChanged,
            this, &UpcomingPage::refreshEmptyState);
    refreshEmptyState();
}

void UpcomingPage::refreshEmptyState()
{
    if (m_proxy->rowCount() > 0) {
        m_stack->setCurrentWidget(m_view);
        return;
    }
    // Two different silences, two different answers: a filtered lens with no
    // matches, and a genuinely empty horizon. The second is GOOD NEWS and the
    // copy says so — the non-shaming rule applies to praise as much as blame.
    QString headline, hint;
    if (m_proxy->priorityFilter() >= 0) {
        headline = tr("Nothing at this urgency");
        hint = tr("Set a task's priority from its detail panel — "
                  "click its title anywhere it appears.");
    } else {
        headline = tr("Your horizon is clear \u2713");
        hint = tr("Give a task a due date (and a time, if it has one) in the "
                  "Activities tab and it will line up here.");
    }
    m_empty->setText(QStringLiteral(
        "<div style='font-size:17px; font-weight:600; color:#2B2F36;'>%1</div>"
        "<div style='font-size:13px; color:#7A828C; margin-top:8px;'>%2</div>")
                         .arg(headline, hint));
    m_stack->setCurrentWidget(m_empty);
}

void UpcomingPage::editTask(const QString& taskId)
{
    // v28.5: seed/exec/apply — and piece navigation — all live in
    // runTaskDetail; it re-reads by id, so the removed-between-click-and-
    // now case is its first guard. Window parenting rule unchanged.
    runTaskDetail(*m_data, taskId, window());
}
