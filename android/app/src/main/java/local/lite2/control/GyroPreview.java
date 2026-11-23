package local.lite2.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.DisplayMetrics;
import android.view.View;
import android.view.WindowManager;

/** Relative pointer movement inside a scaled copy of the current display. */
final class GyroPreview extends View implements PointerBridge.GyroListener {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final DisplayMetrics screen=new DisplayMetrics();
    private float x=.5f,y=.5f;

    GyroPreview(Context context) {
        super(context);
        context.getSystemService(WindowManager.class).getDefaultDisplay().getRealMetrics(screen);
    }
    void waitForInput() {
        x=y=.5f;
        postInvalidateOnAnimation();
    }
    @Override public void changed(int dx,int dy) {
        x=Math.max(0f,Math.min(1f,x+dx/(float)Math.max(1,screen.widthPixels-1)));
        y=Math.max(0f,Math.min(1f,y+dy/(float)Math.max(1,screen.heightPixels-1)));
        postInvalidateOnAnimation();
    }
    @Override protected void onDraw(Canvas canvas) {
        float pad=4*screen.density;
        float scale=Math.min((getWidth()-2*pad)/screen.widthPixels,(getHeight()-2*pad)/screen.heightPixels);
        if(scale<=0)return;
        float w=screen.widthPixels*scale,h=screen.heightPixels*scale;
        float left=(getWidth()-w)/2,top=(getHeight()-h)/2;
        paint.setColor(ControlActivity.EDGE);
        canvas.drawRect(left,top,left+w,top+h,paint);
        float border=screen.density;
        paint.setColor(ControlActivity.BG);
        canvas.drawRect(left+border,top+border,left+w-border,top+h-border,paint);
        float radius=3*screen.density,margin=radius+border;
        paint.setColor(ControlActivity.MINT);
        canvas.drawCircle(left+margin+x*(w-2*margin),top+margin+y*(h-2*margin),radius,paint);
    }
}
