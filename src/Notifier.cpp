#include "Notifier.h"

#include "DesktopNotifier.h"

#ifdef Q_OS_ANDROID
#include "AndroidNotifier.h"
#endif

namespace notify
{

// ---------------------------------------------------------------------------
// THE SINGLE SELECTION POINT for notification delivery in TickTimer.
//
// If this ever becomes the second such #ifdef, something has gone wrong:
// the interface exists precisely so that "which platform am I?" is asked
// once, here, and never again in a page, a service or a handler. A new
// platform is a new implementation and one more branch in this function —
// not an edit to the five call sites.
//
// WASM deliberately falls through to DesktopNotifier: a Qt::Tool window
// renders inside the canvas, so an open tab still gets its toast. What it
// does NOT get is anything while the tab is closed, because no timer runs
// there either. That gap is real and stays fenced in docs/WEB.md rather
// than being papered over here.
// ---------------------------------------------------------------------------
std::unique_ptr<Notifier> make(QObject* parent)
{
#ifdef Q_OS_ANDROID
    return std::make_unique<AndroidNotifier>();
#else
    return std::make_unique<DesktopNotifier>(parent);
#endif
}

} // namespace notify
