package local.lite2.control;

import android.content.Context;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.Toast;

final class SlotOverlay {
    private static final Handler main=new Handler(Looper.getMainLooper());
    private static TextView label;
    private static WindowManager manager;
    private static final Runnable hide=()->{
        if(label!=null && label.isAttachedToWindow())manager.removeViewImmediate(label);
        label=null;
    };
    static void show(Context context,String name) {
        if(!Settings.canDrawOverlays(context)) {
            Toast.makeText(context,name,Toast.LENGTH_SHORT).show();
            return;
        }
        main.removeCallbacks(hide);
        if(label==null) {
            manager=context.getSystemService(WindowManager.class);
            label=new TextView(context);
            float density=context.getResources().getDisplayMetrics().density;
            int pad=Math.round(12*density);
            label.setTextSize(16);label.setTextColor(0xffeaf7f3);
            label.setGravity(Gravity.CENTER);
            label.setPadding(pad*2,pad,pad*2,pad);
            GradientDrawable shape=new GradientDrawable();
            shape.setColor(0xf0192729);shape.setCornerRadius(2*pad);
            label.setBackground(shape);
            WindowManager.LayoutParams p=new WindowManager.LayoutParams(-2,-2,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE,
                PixelFormat.TRANSLUCENT);
            p.gravity=Gravity.TOP|Gravity.CENTER_HORIZONTAL;p.y=pad*2;
            p.setTitle("Lite 2 slot");
            try {manager.addView(label,p);}
            catch(SecurityException | WindowManager.BadTokenException unavailable) {
                label=null;Toast.makeText(context,name,Toast.LENGTH_SHORT).show();return;
            }
        }
        label.setText(name);
        main.postDelayed(hide,1400);
    }
}
