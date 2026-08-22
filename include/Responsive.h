#pragma once
// ---------------------------------------------------------------------------
// Responsive.h — "how much room do I have?", as a pure function.
//
// This is the one real judgement behind every phone/desktop layout difference
// in the app, extracted so a table of microsecond tests can pin it — the same
// shape as SyncPlan::decide, version::decideBanner and MissedBlocks. It has
// NO Qt includes at all, which is why it can be asserted in the Core-only
// test suite (test_nlp), and why nothing here can quietly learn about widgets.
//
// WHAT IT REPLACES, AND WHY THAT WAS WRONG. The app used to ask
// isCompactScreen() — "is the SCREEN small?" — once, during construction.
// Three problems, and only the last is about tidiness:
//
//   1. Binary. Phone or desktop. A tablet, a landscape phone and a half-width
//      desktop window had no answer between them.
//   2. Sampled once and never again. The mode was decided before the first
//      pixel was drawn, so rotating a phone changed nothing.
//   3. It asked about the SCREEN. But a page does not care how big the screen
//      is; it cares how much width IT was handed. Those differ constantly —
//      most obviously when the nav rail opens and takes 190px away from the
//      page without the window changing size at all.
//
// Asking about the container instead fixes (3), and (1) and (2) fall out of
// it for free. That is the whole idea: the question was wrong, not the answer.
//
// WIDTH ONLY — NOT A QSize. Deliberate, and it is not laziness:
//
//   * Width is the only axis that CLIPS. A layout too tall for its window is
//     a scroll; a layout too wide is content nobody can reach.
//   * Android's soft keyboard resizes the window (adjustResize): height
//     collapses, width does not. A size-driven mode would change class in the
//     middle of typing a chat message. Being width-only is PROTECTIVE here.
//   * Height does matter for exactly one thing — landscape, where a phone is
//     roughly 854x384 and a wide arrangement gets handed 384px of height.
//     That deserves its own heightClassFor() when it is built, not a second
//     meaning smuggled into this one.
//
// THE BREAKPOINTS are the standard window size classes (600 / 840 logical
// px). Borrowed rather than invented, so "why 600?" has an answer that is not
// "it looked right on the author's phone".
// ---------------------------------------------------------------------------

namespace responsive {

// Three classes, not two. The middle one is what a small tablet, a landscape
// phone and a half-screen desktop window all deserve: more than a phone gets,
// less than a maximised window earns.
enum class Mode { Compact, Medium, Expanded };

constexpr int kMediumMin   = 600;
constexpr int kExpandedMin = 840;

// The deadband, in px, on EITHER side of a breakpoint. See modeFor(int, Mode).
constexpr int kStickyPx = 24;

// The judgement with no memory: which class does this width fall in?
//
// A width of 0 answers Compact. That is not a special case being papered
// over — a widget genuinely has zero width before its first layout pass, and
// "no room at all" is the honest classification of no room at all. Callers
// that must not act on a pre-layout width guard it themselves; see the
// first-dispatch note in ResponsiveWatcher.h.
constexpr Mode modeFor(int widthPx)
{
    if (widthPx >= kExpandedMin)
        return Mode::Expanded;
    if (widthPx >= kMediumMin)
        return Mode::Medium;
    return Mode::Compact;
}

// The judgement WITH memory — hysteresis.
//
// THE PROBLEM IT SOLVES: a user dragging a window edge sweeps a width across
// a breakpoint one pixel at a time. Without a deadband, a hand resting at
// exactly 600px would flip the layout back and forth on every mouse jitter —
// the layout equivalent of a rattling relay.
//
// THE RULE, stated once so the tests and the reader agree:
//   * to WIDEN into the next class, the width must reach threshold + kStickyPx
//   * to NARROW into the previous one, it must fall below threshold - kStickyPx
// so each breakpoint is really a 48px band that the mode refuses to cross
// twice. Concretely, around the 600 boundary: Compact holds until 624, and
// Medium holds down to 576.
//
// NOTE THE SIGNATURE: `current` is an ARGUMENT, not stored state. The memory
// lives in the caller (ResponsiveWatcher), so this function stays pure and a
// test can hand it any history it likes, including ones no real user could
// produce. Same split the window-memory code already uses — overlapsAnyScreen()
// takes the screen rectangles rather than asking for them (Widgets.h).
constexpr Mode modeFor(int widthPx, Mode current)
{
    switch (current) {
    case Mode::Compact:
        if (widthPx >= kExpandedMin + kStickyPx)
            return Mode::Expanded;
        if (widthPx >= kMediumMin + kStickyPx)
            return Mode::Medium;
        return Mode::Compact;

    case Mode::Medium:
        if (widthPx >= kExpandedMin + kStickyPx)
            return Mode::Expanded;
        if (widthPx < kMediumMin - kStickyPx)
            return Mode::Compact;
        return Mode::Medium;

    case Mode::Expanded:
        if (widthPx < kMediumMin - kStickyPx)
            return Mode::Compact;
        if (widthPx < kExpandedMin - kStickyPx)
            return Mode::Medium;
        return Mode::Expanded;
    }
    return modeFor(widthPx); // unreachable; keeps every compiler quiet
}

// For test failure messages and the layout probe. Deliberately not tr()'d:
// this is diagnostic output for developers, never user-facing text.
constexpr const char* name(Mode m)
{
    switch (m) {
    case Mode::Compact:  return "Compact";
    case Mode::Medium:   return "Medium";
    case Mode::Expanded: return "Expanded";
    }
    return "?";
}

} // namespace responsive
