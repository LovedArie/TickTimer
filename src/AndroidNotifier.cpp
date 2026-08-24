#include "AndroidNotifier.h"

#ifdef Q_OS_ANDROID

#include <QCoreApplication>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtCore/qcoreapplication_platform.h>

namespace
{

// The one Java class this file talks to. Written out as a constant because
// a typo in a JNI class name is a runtime NoClassDefFoundError on a phone,
// not a compile error on this machine — the single worst debugging loop
// this feature has.
constexpr auto kTickNotifier = "org/ticktimer/app/TickNotifier";

QJniObject androidContext()
{
    // PUBLIC API, deliberately. Qt's QPermission classes (qpermissions.h)
    // cover camera, microphone, bluetooth, contacts, calendar and location
    // — and NOT notifications, so the tempting route here is
    // QtAndroidPrivate::requestPermission from a private header. We do not
    // take it: the permission request lives in our own Java class instead,
    // which needs no private Qt API and cannot break on a Qt upgrade.
    return QNativeInterface::QAndroidApplication::context();
}

} // namespace

AndroidNotifier::AndroidNotifier()
{
    // Creating the channel is idempotent and must happen before the first
    // notification — including one posted by a receiver while the app is
    // dead, which is why the Java side also creates it defensively.
    QJniObject::callStaticMethod<void>(kTickNotifier, "ensureChannel",
                                       "(Landroid/content/Context;)V",
                                       androidContext().object());
}

void AndroidNotifier::announce(const ToastSpec& spec)
{
    // Post immediately. This is the minority path on a phone: the majority
    // of what this app says was scheduled hours ago and arrives from
    // AlarmReceiver with no C++ involved.
    const QJniObject title = QJniObject::fromString(spec.title);
    const QJniObject body  = QJniObject::fromString(spec.body);
    QJniObject::callStaticMethod<void>(
        kTickNotifier, "notifyNow",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        androidContext().object(), title.object(), body.object());
}

void AndroidNotifier::publish(const QVector<alarms::Alarm>& schedule)
{
    // JSON, because the receiver that reads this back may be running in a
    // process with no Qt in it, hours after we are gone. Instants cross as
    // epoch MILLISECONDS — AlarmManager's own unit, so nothing has to be
    // converted on the far side where a mistake would be silent.
    QJsonArray items;
    for (const alarms::Alarm& a : schedule) {
        QJsonObject o;
        o["key"]   = a.key;
        o["at"]    = a.at.toMSecsSinceEpoch();
        o["title"] = a.title;
        o["body"]  = a.body;
        items.append(o);
    }
    const QString json =
        QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));

    // REPLACES, never appends. TickNotifier.publish cancels every alarm it
    // previously armed before arming this set, which is what makes editing
    // a block move its alarm instead of adding a second one. The stable
    // alarms::Alarm::key is what lets it do that across app restarts.
    const QJniObject payload = QJniObject::fromString(json);
    QJniObject::callStaticMethod<void>(
        kTickNotifier, "publish",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        androidContext().object(), payload.object());
}

bool AndroidNotifier::canSpeak() const
{
    return QJniObject::callStaticMethod<jboolean>(
        kTickNotifier, "canNotify", "(Landroid/content/Context;)Z",
        androidContext().object());
}

void AndroidNotifier::requestPermission()
{
    // Fire and forget: the result arrives as a system dialog the user
    // answers whenever they like, and we simply ask canSpeak() again later.
    // Wanting the async callback would mean overriding
    // onRequestPermissionsResult on Qt's own activity, which is a great
    // deal of machinery for a question we can just re-ask.
    QJniObject::callStaticMethod<void>(kTickNotifier, "requestNotifyPermission",
                                       "(Landroid/content/Context;)V",
                                       androidContext().object());
}

#endif // Q_OS_ANDROID
