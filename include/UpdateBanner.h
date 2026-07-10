#pragma once

#include <QWidget>

// ---------------------------------------------------------------------------
// UpdateBanner — the quiet strip that says a newer TickTimer exists.
//
// The manners are the design (design-addendum-update §E):
//   - it's a STRIP, not a modal: it never blocks the planner, never steals
//     focus, never demands an answer;
//   - "Get it" opens the release page in the browser — the app never
//     downloads or installs anything itself (Level 1, on purpose);
//   - ✕ records the dismissed version in QSettings and hides. The pure rule
//     in version::decideBanner then keeps THAT version silent forever while
//     letting the next one speak — dismissal is per-version, not global.
//
// The widget itself holds no judgement: whoever creates it (MainWindow) has
// already run decideBanner. Glass renders verdicts; it doesn't reach them.
// ---------------------------------------------------------------------------

class UpdateBanner : public QWidget
{
    Q_OBJECT
public:
    UpdateBanner(const QString& latestVersion, const QString& url,
                 const QString& notes, QWidget* parent = nullptr);
};
