#include "DesktopNotifier.h"

#include "NotificationToast.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QString>
#include <QUrl>

#ifdef TICKTIMER_HAS_MULTIMEDIA
#include <QSoundEffect>
#elif defined(Q_OS_WIN)
#include <windows.h>
// mmsystem.h must follow windows.h — it depends on types declared there.
#include <mmsystem.h>
#endif

namespace
{

// Chimes in three tiers (v19.9 — the owner's build fell through the v19.8
// version SILENTLY: their Qt kit has no Multimedia module, so the ifdef
// quietly took the beep. The lesson is the fix: degrade in STEPS, and never
// past a step the platform is known to have):
//   1. QSoundEffect    — any platform, when Qt Multimedia exists;
//   2. winmm PlaySound — Windows' own API, ALWAYS present there: the real
//      chime with zero extra installs (WAV bytes read from the resource and
//      kept alive — SND_ASYNC plays from OUR buffer);
//   3. QApplication::beep() — the floor, for platforms with neither.
//
// The chosen tier is printed at configure time by the ticktimer_sound
// INTERFACE library, so a silent degrade is impossible to ship unnoticed.
std::function<void()> makeChime(QObject* owner, const QString& qrcPath)
{
#ifdef TICKTIMER_HAS_MULTIMEDIA
    auto* fx = new QSoundEffect(owner);
    fx->setSource(QUrl(QStringLiteral("qrc") + qrcPath));
    fx->setVolume(0.85);
    return [fx]() { fx->play(); };
#elif defined(Q_OS_WIN)
    Q_UNUSED(owner);
    // The QByteArray is captured BY VALUE into the lambda and the lambda
    // lives as long as the notifier: the async player reads from a buffer
    // that provably outlives every playback.
    QFile wav(qrcPath);
    // open()'s verdict is checked (Qt 6.11 marks it nodiscard — the owner's
    // compiler flagged it, and the zero-warning policy covers THEIR machine,
    // not just the CI's): a failed open yields empty bytes, and empty bytes
    // already fall back to the beep below.
    const QByteArray bytes =
        wav.open(QIODevice::ReadOnly) ? wav.readAll() : QByteArray();
    return [bytes]() {
        if (bytes.isEmpty()) {
            QApplication::beep();
            return;
        }
        PlaySoundW(reinterpret_cast<LPCWSTR>(bytes.constData()), nullptr,
                   SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    };
#else
    Q_UNUSED(owner);
    Q_UNUSED(qrcPath);
    return []() { QApplication::beep(); };
#endif
}

} // namespace

DesktopNotifier::DesktopNotifier(QObject* soundOwner)
    : m_blockChime(makeChime(soundOwner,
                             QStringLiteral(":/sounds/notify_block.wav")))
    , m_phaseChime(makeChime(soundOwner,
                             QStringLiteral(":/sounds/notify_phase.wav")))
{
}

void DesktopNotifier::announce(const ToastSpec& spec)
{
    // Sound first, then the card: the ear beats the eye to a notification
    // that appeared in the corner of a screen nobody was looking at.
    switch (spec.sound) {
    case ToastSpec::Sound::Block:
        m_blockChime();
        break;
    case ToastSpec::Sound::Phase:
        m_phaseChime();
        break;
    case ToastSpec::Sound::None:
        break;
    }

    // Our own card, not QSystemTrayIcon::showMessage (v19.9): the tray API
    // only SUBMITS a balloon to the OS pipeline, and Windows may decline in
    // silence (Focus Assist, per-app settings, full-screen suppression) —
    // which is precisely how the owner's 12:00 went unheard.
    NotificationToast::show(spec);
}

void DesktopNotifier::publish(const QVector<alarms::Alarm>& schedule)
{
    // Deliberately nothing. See the header: AlarmService's own precise
    // timer is what rings on a desktop, and there is no second party to
    // hand a schedule to. Kept as an explicit empty body rather than an
    // omission so the asymmetry with AndroidNotifier reads as a decision.
    Q_UNUSED(schedule);
}
