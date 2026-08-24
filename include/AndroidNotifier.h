#pragma once
// ---------------------------------------------------------------------------
// AndroidNotifier — the phone half of the v30.6 seam.
//
// Everything in the .cpp is inside `#ifdef Q_OS_ANDROID`, and the class is
// still listed unconditionally in CMakeLists. That is the same shape
// MainWindow used for its winmm chime, and it is deliberate: a file that
// only exists on one platform is a file nobody on the other platform ever
// reads, and this one is the security-and-lifetime-sensitive part of the
// feature. Compiling to nothing is cheaper than compiling conditionally.
//
// WHAT MAKES THIS DIFFERENT FROM DesktopNotifier, in one sentence: the app
// is not running when the alarm rings, so nothing here may require the app
// to be running when the alarm rings. announce() is for the rare case where
// something happens while the user is actually looking; publish() is the
// real feature.
//
// THE JAVA SIDE. Three small classes in android/src/org/ticktimer/app/:
// TickNotifier (channel, permission, arming), AlarmReceiver (posts one
// notification from the intent's extras — no Qt, no data.json), and
// BootReceiver (Android wipes every scheduled alarm on reboot; this re-arms
// them from the copy TickNotifier persisted). The schedule crosses the
// boundary as JSON because a string is the one thing JNI never argues
// about, and because whatever holds it must be readable by a receiver
// running in a process with no C++ in it at all.
// ---------------------------------------------------------------------------

#include "Notifier.h"

class AndroidNotifier : public Notifier
{
public:
    AndroidNotifier();

    void announce(const ToastSpec& spec) override;
    void publish(const QVector<alarms::Alarm>& schedule) override;

    // The OS holds these and will post them with this process dead. The
    // in-process voices must therefore stay quiet about anything scheduled,
    // or an alarm that lands while the app is open arrives twice.
    bool deliversSchedule() const override { return true; }

    bool canSpeak() const override;
    void requestPermission() override;
};
