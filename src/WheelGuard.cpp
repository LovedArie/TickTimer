#include "WheelGuard.h"

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QScrollBar>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>

namespace
{
// Is this a control the wheel must not touch by accident - and if so, does
// having focus earn it the wheel back? (`focusMatters` false = never.)
bool isGuarded(QObject* watched, bool* focusMatters)
{
    auto* w = qobject_cast<QWidget*>(watched);
    if (!w)
        return false;

    const QVariant optOut = w->property("wheelGuard");
    if (optOut.isValid() && !optOut.toBool())
        return false; // "I mean it" - see the header

    const auto answer = [focusMatters](bool matters) {
        if (focusMatters)
            *focusMatters = matters;
        return true;
    };

    if (qobject_cast<QComboBox*>(w))
        return answer(false); // never, focused or not
    if (qobject_cast<QAbstractSpinBox*>(w))
        return answer(true); // spin box, date, time, date-time
    // A slider is a data control too, but a SCROLLBAR is how a page scrolls:
    // guarding that would break the very gesture this feature protects.
    if (qobject_cast<QAbstractSlider*>(w) && !qobject_cast<QScrollBar*>(w))
        return answer(true);
    return false;
}

class WheelGuardFilter : public QObject
{
public:
    explicit WheelGuardFilter(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        // Polish: the widget is fully built and about to be shown. Early
        // enough that no wheel can have reached it yet, and it catches
        // widgets created long after this filter was installed.
        if (event->type() == QEvent::Polish) {
            if (isGuarded(watched, nullptr)) {
                auto* w = static_cast<QWidget*>(watched);
                // A bit test, not equality: Qt::WheelFocus is StrongFocus
                // plus one more bit, so == would miss a policy set by hand.
                if ((w->focusPolicy() & Qt::WheelFocus) == Qt::WheelFocus)
                    w->setFocusPolicy(Qt::StrongFocus);
            }
            return QObject::eventFilter(watched, event);
        }

        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);

        bool focusMatters = false;
        if (!isGuarded(watched, &focusMatters))
            return QObject::eventFilter(watched, event);

        auto* w = static_cast<QWidget*>(watched);
        if (focusMatters && w->hasFocus())
            return QObject::eventFilter(watched, event); // clicked into: yours

        // IGNORED, then stopped here. Ignoring is what sends the event on to
        // the parent - and so to the scroll area - instead of it vanishing.
        event->ignore();
        return true;
    }
};
} // namespace

void wheelguard::install(QObject* owner)
{
    QCoreApplication::instance()->installEventFilter(
        new WheelGuardFilter(owner));
}
