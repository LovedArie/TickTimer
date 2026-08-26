#pragma once
// ---------------------------------------------------------------------------
// Touch.h — how big a thing has to be before a thumb can hit it (v30.7).
//
// WHY THIS IS A FILE AND NOT A NUMBER TYPED IN TWENTY-FIVE PLACES. The app
// shipped to a phone with no touch standard at all. Every control's height
// was whatever its font plus its padding happened to come to — the
// stylesheet contains exactly two `min-height` rules and both are scrollbar
// handles — so the answer ranged from 15 to 58 depending on which file you
// were reading. Every delete ✕ in the app was 22–28; the only door to
// Settings on a phone was 34; the only door to Special days was 32.
//
// A rule that lives in one header can be raised once, tested once, and
// argued with once. Twenty-five literals cannot.
//
// THE NUMBERS, AND WHERE THEY COME FROM. None of this project's usual
// sources cover mobile UI — Larman, Farley and Stroustrup have nothing to
// say about thumbs, and while Bass/Clements/Kazman treat usability as a
// quality attribute with tactics (this file is "support user initiative"
// made concrete), the figures are not in any of them. They are the
// platform's own published specs:
//
//   48 x 48   Material Design's minimum touch target, and WCAG 2.5.5
//             Target Size (Level AAA). Material puts it at about 9mm of
//             physical screen regardless of density.
//   24 x 24   WCAG 2.5.8 Target Size (Minimum), Level AA — the floor
//             below which a target is a defect rather than a compromise.
//    8        Material's minimum gap between adjacent targets, for the
//             cases where a control is genuinely allowed to be smaller
//             than 48 and must not overlap its neighbour.
//
// THE UNIT, WHICH IS THE PART THAT MAKES THIS EASY. On the phone this app
// is built for, Qt reports a 360x800 logical screen at devicePixelRatio
// 3.00, on a 1080x2400 panel at Android density 480. So one Qt logical
// pixel is one third of a physical pixel is exactly one Android dp:
//
//     1 Qt logical px  ==  1 dp
//
// Measured, not assumed: EventDialog's colour swatch is setFixedSize(15,15)
// and comes out 45 physical pixels wide in a screengrab. That identity is
// what lets every number below be a plain int compared against plain
// widget sizes, with no conversion anywhere — and it holds only because
// nothing in this repo sets QT_SCALE_FACTOR or a rounding policy. Any of
// those would break it silently. (design-addendum-responsive.md's
// "Explicitly NOT done" once claimed a 24px button was "8 logical px of
// thumb target"; that divided by the ratio the wrong way. It is 24dp —
// half the target, not a sixth of it.)
//
// PURE, LIKE Responsive.h. `compact` is a parameter and never a lookup, so
// the rules below can be asserted in the Core-only suite without a screen,
// a QApplication or an environment variable. The UI layer supplies the
// answer; this header only knows what to do with it.
// ---------------------------------------------------------------------------

#include <QRect>
#include <QSize>

namespace touch
{

// Material / WCAG 2.5.5 (AAA). The target this app aims at.
inline constexpr int kMinTarget = 48;

// WCAG 2.5.8 (AA). Below this a control is broken, not merely tight — the
// distinction matters when a target genuinely cannot be 48 (a half-hour on
// a timeline) and we need to say how far down is still defensible.
inline constexpr int kFloor = 24;

// Material's minimum gap between adjacent targets.
inline constexpr int kSpacing = 8;

// ---------------------------------------------------------------------------
// sizeFor — "I want to DRAW this at n; how big must the control be?"
//
// The visual size and the target size are different questions, and Material
// says so explicitly: an icon may look 24dp while the padding around it
// makes the target 48. On a desktop, where the pointer is exact, the visual
// size IS the right size — so off compact this returns n unchanged and the
// mouse-driven UI is untouched.
// ---------------------------------------------------------------------------
inline constexpr int sizeFor(int visualPx, bool compact)
{
    return (compact && visualPx < kMinTarget) ? kMinTarget : visualPx;
}

inline constexpr QSize sizeFor(int w, int h, bool compact)
{
    return QSize(sizeFor(w, compact), sizeFor(h, compact));
}

// ---------------------------------------------------------------------------
// expand — grow a HIT rectangle around what is painted, about its centre.
//
// For custom-painted rows, where the drawn thing must stay small (a 20px
// checkbox on an 86px card looks right and a 48px one does not) but the
// area that RESPONDS to a finger should not. The delegates already did a
// hand-rolled version of this — `adjusted(-4, -4, 4, 4)` with the comment
// "generous touch target" — which bought 28px against a 48 guideline, and
// only on two of the five zones in the row.
//
// Centred growth is what keeps paint and hit-test honest with each other:
// the target is exactly the drawn thing plus a symmetric margin, so a tap
// that looks like it landed on the checkbox did.
// ---------------------------------------------------------------------------
inline QRect expand(const QRect& visual, bool compact)
{
    if (!compact)
        return visual;
    const int dw = qMax(0, kMinTarget - visual.width());
    const int dh = qMax(0, kMinTarget - visual.height());
    // Integer halves, remainder to the right/bottom, so an odd shortfall
    // still reaches kMinTarget instead of landing one pixel short.
    return visual.adjusted(-dw / 2, -dh / 2, dw - dw / 2, dh - dh / 2);
}

// The gate's predicate, so the test and the code agree on one definition of
// "big enough" rather than each spelling out a comparison.
inline constexpr bool meets(int w, int h)
{
    return w >= kMinTarget && h >= kMinTarget;
}

inline bool meets(const QSize& size)
{
    return meets(size.width(), size.height());
}

// Above WCAG's AA floor but below Material's target: the honest description
// of a control that had to stay small for a reason. Used by the gate to
// separate "documented compromise" from "defect".
inline constexpr bool meetsFloor(int w, int h)
{
    return w >= kFloor && h >= kFloor;
}

} // namespace touch
