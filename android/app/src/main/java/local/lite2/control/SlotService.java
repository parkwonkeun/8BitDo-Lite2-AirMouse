package local.lite2.control;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

public final class SlotService extends Service {
    @Override public void onCreate() {
        super.onCreate();
        String channel="lite2-controller";
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(channel,"Lite 2 Control",NotificationManager.IMPORTANCE_LOW));
        PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,ControlActivity.class),
            PendingIntent.FLAG_IMMUTABLE|PendingIntent.FLAG_UPDATE_CURRENT);
        startForeground(21,new Notification.Builder(this,channel)
            .setContentTitle("Lite 2 Control").setSmallIcon(getApplicationInfo().icon)
            .setContentIntent(open).setOngoing(true).build());
    }
    @Override public int onStartCommand(Intent intent,int flags,int id) {
        String token=getSharedPreferences("bridge",MODE_PRIVATE).getString("token",null);
        if(token==null){stopSelf();return START_NOT_STICKY;}
        PointerBridge.ensureBackground(token,new Profiles(this));
        return START_STICKY;
    }
    @Override public IBinder onBind(Intent intent){return null;}
}
