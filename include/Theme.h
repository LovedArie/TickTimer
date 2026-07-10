#pragma once
// ---------------------------------------------------------------------------
// Theme — the prototype's colour palette and the app-wide stylesheet.
//
// WHY one file: the prototype defined its palette once as CSS variables
// (--focus, --break, ...) and every element referenced them. This is the
// C++ twin of that idea. Change "focus green" here and every widget,
// chart, and button follows. Scattered hex literals are how UIs drift
// into inconsistency.
//
// Qt Style Sheets (QSS) are deliberately CSS-like, so most of the
// prototype's styling translates almost line for line.
// ---------------------------------------------------------------------------

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>
#include <QStyleHints>

namespace theme
{
inline const QColor bg()      { return QColor("#F1F4F0"); }
inline const QColor surface() { return QColor("#FFFFFF"); }
inline const QColor ink()     { return QColor("#272C33"); }
inline const QColor inkSoft() { return QColor("#616974"); }
inline const QColor line()    { return QColor("#E2E6E0"); }
inline const QColor focus()   { return QColor("#2F7E6E"); } // calm green
inline const QColor brk()     { return QColor("#DD9B4A"); } // warm amber
inline const QColor track()   { return QColor("#EAEDE8"); } // empty bar
inline const QColor danger()  { return QColor("#C25B54"); }

// ---- derived colours ---------------------------------------------------
// A category's stored colour is its IDENTITY; how loud it appears on
// screen is PRESENTATION, derived here. Large areas get pastel(), text
// on them gets deep(). Deriving (instead of storing softened colours)
// means any colour the user ever picks renders softly — and existing
// data files need no migration when the look changes.

// Linear blend: t=0 gives a, t=1 gives b. The workhorse of colour math.
inline QColor mix(const QColor& a, const QColor& b, float t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

// Soft surface tint: 80% of the way toward white. For big fills.
inline QColor pastel(const QColor& c) { return mix(c, surface(), 0.80f); }

// The readable companion: same hue, tamed saturation, dark enough for
// text sitting on the pastel. HSL is the natural space for "same colour,
// different loudness" edits — RGB scattering the hue is the classic bug.
inline QColor deep(const QColor& c)
{
    float h = 0, s = 0, l = 0;
    c.getHslF(&h, &s, &l);
    if (h < 0)
        h = 0; // achromatic colours (grays) report hue as -1
    return QColor::fromHslF(h, qMin(s, 0.55f), 0.30f);
}

inline QString appStyleSheet()
{
    return QStringLiteral(R"CSS(
        QMainWindow, QDialog { background: #F1F4F0; }
        QWidget { color: #272C33; font-size: 13px; }

        /* Cards/panels — objectName selector, like a CSS class */
        QFrame#panel {
            background: #FFFFFF;
            border: 1px solid #E2E6E0;
            border-radius: 14px;
        }
        QLabel#h1 { font-size: 19px; font-weight: 800; }
        QLabel#h2 { font-size: 15px; font-weight: 700; }
        QLabel#sub { color: #616974; font-size: 12px; }
        QLabel#stateIdle     { color: #616974; font-weight: 600; }
        QLabel#stateFocusing { color: #2F7E6E; font-weight: 600; }
        QLabel#stateOnBreak  { color: #DD9B4A; font-weight: 600; }
        QLabel#encourage {
            background: rgba(47,126,110,0.08);
            color: #245F53;
            border-radius: 10px;
            padding: 10px 12px;
            font-size: 12px;
        }

        /* Left navigation */
        QToolButton#nav {
            border: none; border-radius: 10px;
            padding: 9px 14px;
            font-weight: 700; font-size: 13px;
            color: #616974; background: transparent;
            text-align: left;
        }
        QToolButton#nav:hover   { background: rgba(47,126,110,0.07); color: #272C33; }
        QToolButton#nav:checked { background: rgba(47,126,110,0.12); color: #2F7E6E; }

        /* Update banner — present but polite: tinted, bordered, no drama. */
        QWidget#updateBanner {
            background: rgba(83,74,183,0.08);
            border: 1px solid rgba(83,74,183,0.25);
            border-radius: 10px;
        }
        QWidget#updateBanner QLabel { color: #3E3A73; font-weight: 600; }

        /* Buttons */
        QPushButton {
            background: #FFFFFF; border: 1px solid #E2E6E0;
            border-radius: 9px; padding: 7px 13px; font-weight: 600;
        }
        QPushButton:hover:enabled { border-color: #2F7E6E; color: #2F7E6E; }
        QPushButton:disabled { color: #AEB4AC; }
        QPushButton#primary {
            background: #2F7E6E; border: none; color: white; padding: 9px 15px;
        }
        QPushButton#primary:hover:enabled { background: #2A7263; color: white; }
        QPushButton#breakBtn {
            background: #DD9B4A; border: none; color: white; padding: 9px 15px;
        }
        QPushButton#breakBtn:hover:enabled { background: #D08F3E; color: white; }
        QPushButton#quiet {
            background: #EEF0ED; border: none; padding: 9px 15px;
        }
        QPushButton#danger {
            background: transparent; border: none; color: #C25B54;
            font-size: 12px; padding: 4px;
        }
        QPushButton#segment {
            border-radius: 0px; border: 1px solid #E2E6E0; padding: 7px 16px;
            background: #FFFFFF; color: #616974; font-size: 12px;
        }
        QPushButton#segment:checked { background: #2F7E6E; color: white; border-color: #2F7E6E; }
        QPushButton#viewSwitcher {
            background: transparent; border: none;
            font-size: 15px; font-weight: 700; color: #272C33;
        }
        QPushButton#viewSwitcher:hover { color: #2F7E6E; }

        QLineEdit, QPlainTextEdit {
            background: #FFFFFF; border: 1px solid #E2E6E0;
            border-radius: 9px; padding: 7px 10px;
        }
        QLineEdit:focus, QPlainTextEdit:focus { border-color: #2F7E6E; }

        QListWidget { border: none; background: transparent; }
        QTreeWidget#railTree { border: none; background: transparent; }
        QTreeWidget#railTree::item {
            padding: 7px 6px; border-radius: 8px; margin: 1px 0;
        }
        QTreeWidget#railTree::item:hover { background: rgba(47,126,110,0.06); }
        QTreeWidget#railTree::item:selected {
            background: rgba(47,126,110,0.12); color: #2F7E6E;
        }
        QScrollArea { border: none; background: transparent; }
        QStatusBar { color: #616974; }

        /* Scrollbars — slim, arrowless, a soft pill. A QScrollBar is made
           of QSS SUB-CONTROLS you style separately: ::handle is the pill
           you drag; ::sub-line/::add-line are the arrow buttons (zero-sized
           here = removed); ::sub-page/::add-page are the track above and
           below the handle (transparent = no boxy groove). */
        QScrollBar:vertical {
            background: transparent; width: 10px; margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #D6DBD4; border-radius: 5px; min-height: 36px;
        }
        QScrollBar::handle:vertical:hover { background: #B9C2B8; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            background: transparent; height: 10px; margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #D6DBD4; border-radius: 5px; min-width: 36px;
        }
        QScrollBar::handle:horizontal:hover { background: #B9C2B8; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }
    )CSS");
    // R"CSS(...)CSS" is a C++11 raw string literal: no escaping of quotes
    // or newlines — ideal for embedding a stylesheet.
}

// ---------------------------------------------------------------------------
// THE PALETTE — the second theming layer, and a war story.
//
// Qt themes an app through TWO layers: the stylesheet above (CSS-like rules
// for whatever they mention) and the QPalette (semantic colour roles —
// Window, Base, Text, Highlight… — that the Fusion style uses for
// everything the stylesheet does NOT mention: scroll bars, viewports,
// selections, and any widget with autoFillBackground on).
//
// v3 pinned only the stylesheet and silently INHERITED the palette from
// the operating system. On a dark-mode Windows machine, Qt 6.5+ follows
// the system scheme and hands the app a dark palette — and the calendar's
// agenda turned black. Why the agenda exactly? A sneaky detail:
// QScrollArea::setWidget() turns ON the child widget's autoFillBackground,
// so a widget that never painted a background suddenly fills itself with
// the palette's Window colour. Lesson: a theme must pin BOTH layers, or
// the app's look is decided by whichever machine it runs on.
// ---------------------------------------------------------------------------
inline QPalette appPalette()
{
    QPalette p;
    p.setColor(QPalette::Window,          bg());        // widget backgrounds
    p.setColor(QPalette::WindowText,      ink());       // default text
    p.setColor(QPalette::Base,            surface());   // views, inputs
    p.setColor(QPalette::AlternateBase,   bg());
    p.setColor(QPalette::Text,            ink());       // text in views
    p.setColor(QPalette::Button,          surface());
    p.setColor(QPalette::ButtonText,      ink());
    p.setColor(QPalette::Highlight,       focus());     // selections
    p.setColor(QPalette::HighlightedText, surface());
    p.setColor(QPalette::PlaceholderText, inkSoft());
    p.setColor(QPalette::ToolTipBase,     surface());
    p.setColor(QPalette::ToolTipText,     ink());
    // The Disabled colour group: greyed-out widgets read from these.
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor("#AEB4AC"));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#AEB4AC"));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#AEB4AC"));
    return p;
}

// One entry point that applies the WHOLE theme — style, palette,
// stylesheet — used by main.cpp and the screenshot tool alike, so the two
// can never drift apart (Don't Repeat Yourself applies to setup code too).
inline void applyTheme(QApplication& app)
{
    // Fusion first: Qt's own cross-platform style draws from the palette
    // we set next, giving one identical baseline on every OS.
    QApplication::setStyle(QStringLiteral("Fusion"));
    app.setPalette(appPalette());
    app.setStyleSheet(appStyleSheet());

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Qt 6.8+ can also tell the OS "this app is light" — which flips the
    // WINDOW FRAME (title bar) to light on dark-mode Windows. The API
    // doesn't exist in older Qt, so it's fenced behind a compile-time
    // version check: the preprocessor removes these lines entirely when
    // building against, say, 6.4. This is how one codebase spans Qt
    // versions without #ifdef soup — one guard, at the one spot needed.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
}
} // namespace theme
