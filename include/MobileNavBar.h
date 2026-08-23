#pragma once
// ---------------------------------------------------------------------------
// MobileNavBar — the phone's navigation, and only the phone's.
//
// WHY A BAR AT ALL. The 190px rail costs half a 360px screen while it is open
// and hides every destination while it is closed. A phone wants its
// destinations permanently visible and reachable by thumb, which is the bottom
// edge. This is the shape Google Calendar and TickTick both settled on, and
// the owner asked for it after a day of using the rail on a phone.
//
// IT IS A LAYOUT CHILD, NOT A FLOATER, and that is the whole difference
// between this and the capture button. A bar must RESERVE its height so no
// page ever paints underneath it; a floating action button must not, so it can
// hover over content. One goes in a layout, the other is positioned by
// geometry (the TaskDetailPanel idiom).
//
// ---- THE MAPPING IS DATA, NOT POSITION ------------------------------------
//
// The bar's tab order is Calendar, Upcoming, Assistant, Activities, Pomodoro —
// which is page indices 0, 1, 6, 2, 4. It is deliberately NOT the stack's
// order, because a phone's most-used destinations are not the desktop's.
//
// So each tab STORES the page it opens. MainWindow::m_navButtons can get away
// with being indexed by position because its order happens to match the stack
// — and its own comment records the off-by-one that cost, plus the rule that
// Archive must be appended before the Assistant to keep the two aligned.
// Copying that pattern here, where the orders genuinely differ, would rebuild
// that bug on purpose.
//
// ---- WHY THE TABS ARE NOT autoExclusive -----------------------------------
//
// Qt SILENTLY REFUSES setChecked(false) on the only checked button of an
// autoExclusive group. Not all pages have a tab — Special days and Archive
// have none — so on those pages the bar must show nothing checked, and an
// autoExclusive group physically cannot. The symptom would be "the Calendar
// tab stays lit while I'm on Special days", with no error anywhere.
//
// setCurrentPage() therefore loops and sets every tab explicitly. The desktop
// rail keeps autoExclusive because every page there does have a button.
//
// ---- WHY THE ICONS ARE PAINTED --------------------------------------------
//
// This project has no .qrc and no image files; resources/ holds two .wav
// chimes. It also has a documented trap about Unicode-as-icon being "a font
// lottery" — and that trap is live: the rail's "✦ Assistant" renders as an
// empty box on Android while its other glyphs survive.
//
// Five line icons are a few QPainter primitives each. That needs no resource
// pipeline, cannot fail on a device's fonts, scales with the device pixel
// ratio for free, and takes its colour from theme::. Custom-painted widgets
// are already one of this codebase's layers.
// ---------------------------------------------------------------------------

#include <QVector>
#include <QWidget>

class QPainter;
class QRectF;

// The five destinations a phone gets. Drawn, not typed.
enum class NavIcon { Calendar, Upcoming, Assistant, Activities, Pomodoro };

// Draw one icon inside `box`, in `colour`. Free function so the bar is not the
// only possible consumer — a future compact rail could use the same glyphs.
void paintNavIcon(QPainter& p, const QRectF& box, NavIcon icon,
                  const QColor& colour);

class MobileNavBar : public QWidget
{
    Q_OBJECT

public:
    explicit MobileNavBar(QWidget* parent = nullptr);

    // `pageIndex` is the QStackedWidget index this tab opens. The bar never
    // learns what lives there — MainWindow owns that table.
    void addTab(int pageIndex, NavIcon icon, const QString& tooltip);

    // Light the tab for `pageIndex`, or none at all if no tab owns it.
    void setCurrentPage(int pageIndex);

signals:
    void pageRequested(int pageIndex);

private:
    struct Tab
    {
        int pageIndex = -1;
        class NavTabButton* button = nullptr;
    };

    QVector<Tab> m_tabs;
    class QHBoxLayout* m_row = nullptr;
};
