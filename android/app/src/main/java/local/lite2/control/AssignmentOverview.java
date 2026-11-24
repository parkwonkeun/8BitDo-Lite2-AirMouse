package local.lite2.control;

import android.content.Context;
import android.widget.HorizontalScrollView;
import android.widget.FrameLayout;

/** Keep each mapping attached to its control, including in split-screen. */
final class AssignmentOverview extends HorizontalScrollView {
    interface Listener { void choose(AssignmentOverview view,int index); }
    private final AssignmentPad pad;
    AssignmentOverview(Context context,int slot,Listener listener) {
        super(context);
        setFillViewport(true);
        pad=new AssignmentPad(context,(p,i)->listener.choose(this,i),slot);
        int minWidth=Math.round(720*getResources().getDisplayMetrics().density);
        addView(pad,new FrameLayout.LayoutParams(minWidth,-2));
    }
    void show(int i,String key,String action,boolean unassigned,boolean selected) {
        pad.caption(i,key,action,unassigned,selected);
    }
    void select(int index) {pad.select(index);}
}
