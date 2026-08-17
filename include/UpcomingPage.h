#pragma once
// ---------------------------------------------------------------------------
// UpcomingPage — every dated, unfinished task across all life areas, grouped
// Overdue / This week / Later (addendum §3.13).
//
// v20: this page is the app's FIRST model/view screen. It used to be a
// rebuild-on-changed() page — delete every card and rebuild from AppData on
// each change. Now it owns a small pipeline instead and never rebuilds a
// widget again:
//
//     AppData ─changed()▶ TaskListModel ▶ TaskFilterProxy ▶ QListView
//                          (snapshot)      (lens: filter+    (+ TaskCardDelegate
//                                           sort)              paints each row)
//
// What that buys, concretely: the page's job shrinks to WIRING. It builds the
// pipeline once, connects the delegate's click signals to AppData, and points
// the priority tabs at the proxy's filter. Data changes flow model→view with
// no page involvement — contrast every other page, which still tears itself
// down and rebuilds on changed(). See docs/design-addendum-model-view.md.
//
// The page keeps NO data state of its own: which lens is on lives in the proxy
// (a view state), and the list lives in the model. The only thing the page
// still decides is the empty-state message, because "what to say when there's
// nothing" is page copy, not model data.
//
// v22.1 REWORK, second round. v22 answered "too small" with a wider cap and
// a strip of count chips; the owner's review answered back: still too small,
// and the chips were buttons nobody needed. The correction is instructive —
// "too small" is fixed with SIZE, not with more controls:
//
//   * no panel width cap at all (the cards keep their own kMaxCardW, so line
//     length is still bounded — by the one component that owns the text);
//   * taller cards and a full type-size step up in the delegate;
//   * the count chips are gone, and so is the proxy's bucket lens that
//     existed only to serve them. Features are hypotheses; the owner's read
//     is the experiment result, and dead machinery left behind a removed
//     feature is how codebases rot.
//
// ---------------------------------------------------------------------------

#include <QWidget>

class AppData;
class TaskListModel;
class TaskFilterProxy;
class TaskCardDelegate;
class QListView;
class QStackedWidget;
class QLabel;

class UpcomingPage : public QWidget
{
    Q_OBJECT

public:
    explicit UpcomingPage(AppData* data, QWidget* parent = nullptr);

private slots:
    // Toggle between the list and the "nothing here" message when the visible
    // row count changes (data edited, or the lens filtered everything out).
    void refreshEmptyState();
    // A card was clicked: open its detail dialog. Parented to the window, never
    // the view — see the .cpp for the double-free that rule prevents.
    void editTask(const QString& taskId);

private:
    AppData*          m_data;
    TaskListModel*    m_model    = nullptr;
    TaskFilterProxy*  m_proxy    = nullptr;
    TaskCardDelegate* m_delegate = nullptr;
    QListView*        m_view     = nullptr;
    QStackedWidget*   m_stack    = nullptr; // [0] = list, [1] = empty message
    QLabel*           m_empty    = nullptr;
};
