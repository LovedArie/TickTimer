package org.ticktimer.app;

import android.app.AlarmManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * TickNotifier — the Android side of TickTimer's notifications (v30.6).
 *
 * <p>WHY ANY JAVA EXISTS IN THIS PROJECT AT ALL. Qt ships no notification
 * API, and the C++ that would otherwise do this work is not running when it
 * matters. Android freezes a backgrounded process, so the app's own
 * QTimer — which has rung the desktop alarm correctly since v19.7 — never
 * fires on a phone. The only thing that can wake anything at 09:00 with the
 * app closed is the OS, so the schedule has to live over here.
 *
 * <p>THE ONE RULE THIS FILE OBEYS: <b>no C++ runs at fire time.</b> Every
 * alarm arrives fully written — title, body, instant — and is stored that
 * way. {@link AlarmReceiver} needs to read nothing but its own intent, and
 * {@link BootReceiver} needs to read nothing but the copy kept here. If
 * either of them ever had to ask the app a question, the feature would cost
 * a 2–4 second cold start per alarm and would fail in exactly the cases it
 * exists for.
 *
 * <p>PUBLISH REPLACES, IT DOES NOT APPEND. Every republish from the C++ side
 * cancels the previous set before arming the new one. That is what makes
 * moving a block move its alarm rather than adding a second one, and it
 * works across app restarts because {@code alarms::Alarm::key} in Alarms.h
 * is derived (source + entity id + instant), never a counter.
 */
public final class TickNotifier
{
    private static final String CHANNEL_ID = "ticktimer_alarms";
    private static final String PREFS      = "ticktimer_notifier";
    private static final String KEY_ARMED  = "armed_schedule";

    private TickNotifier() {}

    /**
     * Creates the notification channel if it is not already there.
     *
     * <p>IMPORTANCE_HIGH is not decoration — it is the single flag that
     * decides whether this app gets a heads-up banner with sound or a
     * silent line in the shade. It was chosen by reading, with adb, what
     * the two apps that already notify the owner on this phone actually
     * use: Daylio's reminder channel and TickTick's task-reminder channel
     * are both importance 4.
     *
     * <p>Called from three places on purpose — the C++ notifier at startup,
     * and both receivers before posting — because a receiver may run in a
     * cold process where nothing else has had the chance.
     */
    public static void ensureChannel(Context context)
    {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return; // pre-26 has no channels; minSdk is 28, so this is belt
        }
        NotificationManager nm = context.getSystemService(NotificationManager.class);
        if (nm == null || nm.getNotificationChannel(CHANNEL_ID) != null) {
            return;
        }
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "Block alarms", NotificationManager.IMPORTANCE_HIGH);
        channel.setDescription(
                "Planned blocks starting and finishing, Pomodoro phases, "
                        + "and the morning check-in.");
        channel.enableVibration(true);
        nm.createNotificationChannel(channel);
    }

    /** True when this app is allowed to post notifications at all. */
    public static boolean canNotify(Context context)
    {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return true; // POST_NOTIFICATIONS did not exist before API 33
        }
        return context.checkSelfPermission(
                       android.Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Asks for POST_NOTIFICATIONS, once, if we are running on an Activity.
     *
     * <p>Doing this here rather than in C++ is what lets the whole feature
     * avoid Qt's private API. Qt's public QPermission classes cover camera,
     * microphone, bluetooth, contacts, calendar and location — not
     * notifications — so the C++ route would be
     * QtAndroidPrivate::requestPermission out of a private header, which is
     * one Qt upgrade away from breaking. We are already writing Java; four
     * lines of it here costs nothing and cannot rot.
     *
     * <p>The result is deliberately not plumbed back. Nobody awaits it:
     * the app simply asks {@link #canNotify} again next time it wants to
     * know, which needs no callback and no override of Qt's own Activity.
     */
    public static void requestNotifyPermission(Context context)
    {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return;
        }
        if (canNotify(context) || !(context instanceof android.app.Activity)) {
            return;
        }
        ((android.app.Activity) context).requestPermissions(
                new String[] { android.Manifest.permission.POST_NOTIFICATIONS },
                /* requestCode = */ 4711);
    }

    /** Post something right now — the rare path; see the class comment. */
    public static void notifyNow(Context context, String title, String body)
    {
        post(context, "immediate:" + System.currentTimeMillis(), title, body);
    }

    /**
     * Replace the held schedule with this one.
     *
     * @param json array of {key, at (epoch millis), title, body}
     */
    public static void publish(Context context, String json)
    {
        ensureChannel(context);
        cancelArmed(context);
        SharedPreferences prefs =
                context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        prefs.edit().putString(KEY_ARMED, json).apply();
        arm(context, json);
    }

    /** Re-arm whatever was last published. Used by {@link BootReceiver}. */
    public static void rearmStored(Context context)
    {
        ensureChannel(context);
        SharedPreferences prefs =
                context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        arm(context, prefs.getString(KEY_ARMED, "[]"));
    }

    // ---------------------------------------------------------------------

    private static void arm(Context context, String json)
    {
        AlarmManager am = context.getSystemService(AlarmManager.class);
        if (am == null) {
            return;
        }
        long now = System.currentTimeMillis();
        try {
            JSONArray items = new JSONArray(json);
            for (int i = 0; i < items.length(); ++i) {
                JSONObject o = items.getJSONObject(i);
                long at = o.getLong("at");
                if (at <= now) {
                    // A past instant would fire the moment it is armed. The
                    // C++ side already filters these out; this is the second
                    // lock on the same door, because the failure is loud.
                    continue;
                }
                PendingIntent pi = intentFor(context, o.getString("key"),
                                             o.getString("title"),
                                             o.getString("body"));
                try {
                    // ...AndAllowWhileIdle is the part that survives Doze.
                    // A plain setExact is silently deferred to the next
                    // maintenance window, which on an idle phone overnight
                    // can be hours — indistinguishable from "it didn't work".
                    am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi);
                } catch (SecurityException denied) {
                    // Exact alarms refused (an OEM build, or a future policy
                    // change). Inexact still rings, just not to the second —
                    // degrade in steps, and never silently: the fallback is
                    // visible in `adb shell dumpsys alarm`.
                    am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi);
                }
            }
        } catch (Exception malformed) {
            // A schedule we cannot parse is a schedule we do not arm. There
            // is nothing useful to do with half of one.
        }
    }

    private static void cancelArmed(Context context)
    {
        AlarmManager am = context.getSystemService(AlarmManager.class);
        SharedPreferences prefs =
                context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String previous = prefs.getString(KEY_ARMED, "[]");
        if (am == null) {
            return;
        }
        try {
            JSONArray items = new JSONArray(previous);
            for (int i = 0; i < items.length(); ++i) {
                JSONObject o = items.getJSONObject(i);
                am.cancel(intentFor(context, o.getString("key"), "", ""));
            }
        } catch (Exception ignored) {
            // Nothing parseable to cancel; the new set is about to replace
            // it wholesale anyway.
        }
    }

    private static PendingIntent intentFor(Context context, String key,
                                           String title, String body)
    {
        Intent intent = new Intent(context, AlarmReceiver.class);
        intent.putExtra("key", key);
        intent.putExtra("title", title);
        intent.putExtra("body", body);
        // The request code is what AlarmManager uses to tell one pending
        // intent from another — so it must come from the alarm's stable key
        // and nothing else. String.hashCode is specified by the Java
        // language, not left to an implementation, which is what makes it
        // safe to rely on across app versions and reboots.
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            flags |= PendingIntent.FLAG_IMMUTABLE; // required from API 31
        }
        return PendingIntent.getBroadcast(context, key.hashCode(), intent, flags);
    }

    /**
     * The status-bar icon, resolved defensively.
     *
     * <p>setSmallIcon is mandatory: posting a notification without a valid
     * one throws IllegalArgumentException and kills the receiver process, so
     * the alarm rings by crashing and the user sees nothing. That is exactly
     * what the first build did — it passed
     * {@code getApplicationInfo().icon}, which is <b>0</b> here, because Qt
     * strips the icon attribute from the manifest when no app icon is
     * configured.
     *
     * <p>So: look our own drawable up by NAME rather than through the
     * generated R class (no compile-time coupling to a resource id that a
     * packaging change could renumber), and if it is somehow absent, fall
     * back to a framework drawable that is guaranteed to exist. The one
     * thing this must never do is return 0.
     */
    private static int smallIcon(Context context)
    {
        int id = context.getResources().getIdentifier(
                "ic_stat_ticktimer", "drawable", context.getPackageName());
        if (id != 0) {
            return id;
        }
        // Ugly, present on every Android since forever, and infinitely
        // better than an exception at 09:00.
        return android.R.drawable.ic_popup_reminder;
    }

    static void post(Context context, String key, String title, String body)
    {
        ensureChannel(context);
        NotificationManager nm = context.getSystemService(NotificationManager.class);
        if (nm == null) {
            return;
        }
        // Tapping it opens the app where it left off. FLAG_IMMUTABLE for the
        // same API-31 reason as above.
        Intent open = context.getPackageManager()
                              .getLaunchIntentForPackage(context.getPackageName());
        PendingIntent tap = null;
        if (open != null) {
            open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                          | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                flags |= PendingIntent.FLAG_IMMUTABLE;
            }
            tap = PendingIntent.getActivity(context, 0, open, flags);
        }

        Notification.Builder b = new Notification.Builder(context, CHANNEL_ID)
                .setContentTitle(title)
                .setContentText(body)
                .setStyle(new Notification.BigTextStyle().bigText(body))
                .setAutoCancel(true)
                .setSmallIcon(smallIcon(context))
                .setCategory(Notification.CATEGORY_REMINDER);
        if (tap != null) {
            b.setContentIntent(tap);
        }
        nm.notify(key.hashCode(), b.build());
    }
}
