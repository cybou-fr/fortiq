package fr.cybou.fortiq;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * Keeps the process (and with it the loopback gateway and the SSH tunnel of the Browser) alive while the app is minimised. It starts
 * while a browser view exists and stops with it. It holds no data and opens no connection; it exists only to be a foreground service.
 * Without the notification permission the service still runs (the system lists it); with it the user sees that the browser is active.
 */
public final class GatewayService extends Service {
    private static final String CHANNEL = "fortiq.gateway";
    private static final int ID = 7101;

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Navigateur FORTIQ", NotificationManager.IMPORTANCE_LOW));
        Intent open = new Intent(this, org.qtproject.qt.android.bindings.QtActivity.class).setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pending = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Notification n = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(getApplicationInfo().icon)
                .setContentTitle("FORTIQ — navigateur actif")
                .setContentText("Les pages passent par votre serveur Linux (tunnel SSH).")
                .setContentIntent(pending)
                .setOngoing(true)
                .build();
        if (Build.VERSION.SDK_INT >= 34) startForeground(ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        else startForeground(ID, n);
        return START_NOT_STICKY;   // the gateway dies with the process: a restarted service would keep nothing alive
    }
}
