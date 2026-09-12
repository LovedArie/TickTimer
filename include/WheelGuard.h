#pragma once
// ---------------------------------------------------------------------------
// WheelGuard - a control that MUTATES DATA must ignore the mouse wheel unless
// it has been clicked into (B8, 31.2.0).
//
// THE BUG THIS EXISTS FOR. EventDialog's "Repeats" combo is wired to
// currentIndexChanged, and a Qt combo acts on a wheel event while merely
// HOVERED - no click, no focus. One notch over it while scrolling the dialog
// chose "Does not repeat", which calls AppData::removeSchedule: a whole
// recurrence rule and its future occurrences gone, with no confirmation and
// nothing said. The field report that produced this guard had lost five
// schedules and 73 Event.scheduleId links.
//
// WHY ONE FILTER ON THE APPLICATION. `grep wheelEvent` across this tree
// returned nothing before this file: no control anywhere guarded against the
// wheel, and there are 37 combos, spin boxes and date/time edits sitting
// inside scrollable dialogs. A line in each of 37 constructors is 37 chances
// to forget, and a 38th control arrives next month.
// installCompactDialogFitter makes the same argument for the same reason.
//
// WHY THE FOCUS POLICY HAS TO CHANGE TOO. QComboBox and QAbstractSpinBox
// default to Qt::WheelFocus, and QApplication hands focus to a WheelFocus
// widget under the pointer BEFORE the event reaches any filter - so a
// hasFocus() test on its own would always answer "yes it does" and guard
// nothing. At Polish time the guard downgrades WheelFocus to StrongFocus:
// clicking and Tab still focus the control, the wheel no longer does.
//
// WHAT EACH KIND GETS. A combo NEVER takes the wheel: its list is a separate
// popup window, so scrolling an OPEN list is unaffected, and "nudge the value
// by hovering" is not a gesture worth one destroyed schedule. A spin box, date
// or time edit takes the wheel only while focused, because nudging a value you
// are already editing is genuinely useful. A scrollbar is left alone - the
// wheel is how you scroll.
//
// HOW THE PAGE STILL SCROLLS. The guarded event is IGNORED, not swallowed:
// QApplication offers an ignored wheel event to the parent, and up the chain
// to the scroll area, which is what turning the wheel meant in the first
// place.
//
// OPT-OUT: setProperty("wheelGuard", false) on a control that genuinely wants
// Qt's default behaviour.
// ---------------------------------------------------------------------------

class QObject;

namespace wheelguard
{
// Install for the life of `owner`; main() passes the application itself.
void install(QObject* owner);
} // namespace wheelguard
