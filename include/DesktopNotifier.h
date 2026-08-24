#pragma once
// ---------------------------------------------------------------------------
// DesktopNotifier — the v19.9 behaviour, unchanged, behind the v30.6 seam.
//
// This is the implementation that has been shipping since v19.9: an
// app-owned NotificationToast (a window no notification pipeline can
// decline) plus a chime that degrades in three visible steps. Nothing about
// what the user sees or hears on Windows changes here; the code simply
// moved out of MainWindow::setupNotifications so that a second platform
// could exist beside it.
//
// WHY publish() DOES NOTHING HERE, which looks like a bug and is the point.
// On desktop the process is always alive, so AlarmService's own
// Qt::PreciseTimer is already the thing that will ring — publishing a
// schedule outward would be handing it to nobody. On Android publish() is
// the ONLY thing that matters, because the process will be frozen at fire
// time. That asymmetry is the whole reason the interface has two methods
// instead of one, and deliversSchedule() below is how MainWindow knows
// which half is load-bearing today.
// ---------------------------------------------------------------------------

#include "Notifier.h"

#include <QString>

#include <functional>

class QObject;

class DesktopNotifier : public Notifier
{
public:
    explicit DesktopNotifier(QObject* soundOwner);

    void announce(const ToastSpec& spec) override;
    void publish(const QVector<alarms::Alarm>& schedule) override;

    // The app's own timer rings; the platform holds nothing.
    bool deliversSchedule() const override { return false; }

    // No desktop asks permission to show a window we drew ourselves —
    // which was the entire argument for drawing it ourselves.
    bool canSpeak() const override { return true; }

private:
    std::function<void()> m_blockChime;
    std::function<void()> m_phaseChime;
};
