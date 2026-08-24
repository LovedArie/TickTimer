package org.ticktimer.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * BootReceiver — the reason the alarms are still there tomorrow (v30.6).
 *
 * <p>Android discards every scheduled alarm when the device restarts. There
 * is no warning and no error: an app that does not re-arm simply goes quiet
 * some days later, and the person using it concludes the feature never
 * worked. Both of the apps that already notify the owner on this phone —
 * Daylio and TickTick — declare RECEIVE_BOOT_COMPLETED for exactly this,
 * which is how the gap was noticed before it could bite.
 *
 * <p>Re-arming reads only the copy {@link TickNotifier} persisted when the
 * app last published, so this works with the app dead and never launches
 * it. MY_PACKAGE_REPLACED is handled alongside BOOT_COMPLETED because an
 * app update clears alarms the same way a reboot does — the case that would
 * otherwise break every time a new APK is sideloaded, which for this
 * project is often.
 */
public final class BootReceiver extends BroadcastReceiver
{
    @Override
    public void onReceive(Context context, Intent intent)
    {
        if (intent == null || intent.getAction() == null) {
            return;
        }
        final String action = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action)
                && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)
                && !"android.intent.action.QUICKBOOT_POWERON".equals(action)) {
            return;
        }
        TickNotifier.rearmStored(context);
    }
}
