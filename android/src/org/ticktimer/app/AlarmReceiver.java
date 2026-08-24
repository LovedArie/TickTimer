package org.ticktimer.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * AlarmReceiver — what actually speaks when a block starts (v30.6).
 *
 * <p>Note what is NOT here: no Qt, no JNI, no reading of data.json, no
 * question asked of the app. Everything this needs arrived in the intent,
 * written hours or days ago by {@code alarms::upcoming}. That is the whole
 * design constraint of TickTimer's Android notifications stated in one
 * class — the app is not running when this runs, and nothing here may
 * assume otherwise.
 *
 * <p>onReceive is on the main thread with roughly ten seconds of budget and
 * no guarantee of a process afterwards, which is exactly why it does one
 * cheap thing and returns.
 */
public final class AlarmReceiver extends BroadcastReceiver
{
    @Override
    public void onReceive(Context context, Intent intent)
    {
        if (intent == null) {
            return;
        }
        final String key   = intent.getStringExtra("key");
        final String title = intent.getStringExtra("title");
        final String body  = intent.getStringExtra("body");
        if (key == null || title == null) {
            return;
        }
        TickNotifier.post(context, key, title, body == null ? "" : body);
    }
}
