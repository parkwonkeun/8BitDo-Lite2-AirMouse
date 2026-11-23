package local.lite2.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.view.ViewGroup;
import android.widget.Button;

/** Front of the Lite 2 with both shoulder rows unfolded above the shell. */
final class AssignmentPad extends ViewGroup {
    interface Listener { void choose(AssignmentPad pad,int index); }

    private enum Shape { FACE, ARROW, SHOULDER, STICK, SYSTEM }
    private static final class Key {
        final Shape shape;
        final int x, y, width, height;
        final String legend;
        Key(Shape shape, int x, int y, int width, int height, String legend) {
            this.shape=shape; this.x=x; this.y=y; this.width=width; this.height=height;
            this.legend=legend;
        }
    }

    // Indexed by Profiles.BUTTONS so existing S and D slot assignments keep their identity.
    private static final Key[] KEYS={
        new Key(Shape.FACE,503,161,48,48,"A"), new Key(Shape.FACE,461,203,48,48,"B"),
        new Key(Shape.FACE,461,119,48,48,"X"), new Key(Shape.FACE,419,161,48,48,"Y"),
        new Key(Shape.ARROW,208,214,44,44,"↑"), new Key(Shape.ARROW,208,302,44,44,"↓"),
        new Key(Shape.ARROW,164,258,44,44,"←"), new Key(Shape.ARROW,252,258,44,44,"→"),
        new Key(Shape.SHOULDER,103,63,110,40,"LB"), new Key(Shape.SHOULDER,465,63,110,40,"RB"),
        new Key(Shape.SHOULDER,103,23,88,40,"LT"), new Key(Shape.SHOULDER,465,23,88,40,"RT"),
        new Key(Shape.STICK,101,160,89,89,"L3"), new Key(Shape.STICK,362,258,89,89,"R3"),
        new Key(Shape.SYSTEM,364,127,49,49,"+"), new Key(Shape.SYSTEM,208,127,49,49,"−"),
        new Key(Shape.SYSTEM,101,299,49,49,"★"), new Key(Shape.SYSTEM,461,299,49,49,"⌂")
    };
    // Give attached mapping labels space between neighboring controls.
    private static final int[][] LABELED={
        {789,216},{702,286},{702,145},{616,216},{308,263},{308,414},{218,340},{398,340},
        {135,70},{653,70},{135,23},{653,23},{112,225},{540,372},{494,150},{300,150},
        {110,420},{740,422}
    };
    private final boolean labeled;
    private final android.widget.TextView[] captions=new android.widget.TextView[KEYS.length];
    private final float[] tilt=new float[4];
    private int heldMask;
    private static final int WIDTH=570, HEIGHT=346;
    private static final int SHELL=0xff68cdc5, LIP=0xff379e99, INK=0xff173e42,
        CAP=0xff245254, RIM=0xff467475, LEGEND=0xffe4f5ec, HIGHLIGHT=0xffffdd93;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    private float sx, sy, left;
    private int selected=-1;

    AssignmentPad(Context context,Listener listener) {
        super(context);
        density=getResources().getDisplayMetrics().density;
        labeled=listener!=null;
        setWillNotDraw(false);
        for(int i=0;i<KEYS.length;i++) {
            final int index=i;
            Cap cap=new Cap(context,KEYS[i]);
            if(labeled)cap.setOnClickListener(v->listener.choose(this,index));
            else {cap.setClickable(false);cap.setFocusable(false);cap.setContentDescription(Profiles.BUTTONS[i]);}
            addView(cap);
        }
        if(labeled)for(int i=0;i<KEYS.length;i++) {
            final int index=i;
            android.widget.TextView label=new android.widget.TextView(context);
            label.setGravity(android.view.Gravity.CENTER);
            label.setTextSize(13);
            label.setMaxLines(2);
            label.setEllipsize(android.text.TextUtils.TruncateAt.END);
            label.setIncludeFontPadding(false);
            label.setOnClickListener(v->listener.choose(this,index));
            captions[i]=label;
            addView(label);
        }
    }

    void caption(int i,String key,String action,boolean unassigned,boolean selected) {
        captions[i].setText(action);
        boolean shoulder=KEYS[i].shape==Shape.SHOULDER;
        captions[i].setTextColor(shoulder?(selected?ControlActivity.GOLD:ControlActivity.WHITE):INK);
        captions[i].setTypeface(Typeface.DEFAULT,unassigned?Typeface.NORMAL:Typeface.BOLD);
        String description=key+" · "+action;
        captions[i].setContentDescription(description);
        getChildAt(i).setContentDescription(description);
    }
    void live(int mask,float x,float y,float rx,float ry) {
        float[] next={x,y,rx,ry};
        for(int i=0;i<4;i++) {
            float value=Math.max(-1,Math.min(1,next[i]));
            if(value!=tilt[i]) {tilt[i]=value;getChildAt(12+i/2).invalidate();}
        }
        int changed=heldMask^mask;
        heldMask=mask;
        for(int i=0;i<KEYS.length;i++)if((changed&(1<<i))!=0)
            getChildAt(i).setSelected((mask&(1<<i))!=0);
    }
    void select(int index) {
        if(selected>=0) getChildAt(selected).setSelected(false);
        selected=index;
        if(selected>=0) getChildAt(selected).setSelected(true);
    }

    @Override protected void onMeasure(int widthSpec,int heightSpec) {
        int width=Math.max(MeasureSpec.getSize(widthSpec),labeled?Math.round(720*density):0);
        float padWidth=Math.min(width,(labeled?945:625)*density);
        if(!labeled && MeasureSpec.getMode(heightSpec)!=MeasureSpec.UNSPECIFIED)
            padWidth=Math.min(padWidth,MeasureSpec.getSize(heightSpec)*(float)WIDTH/HEIGHT);
        sx=padWidth/(labeled?880:WIDTH);
        sy=labeled?Math.max(sx,.87f*density):sx;
        left=(width-padWidth)/2;
        setMeasuredDimension(width,Math.round((labeled?497:HEIGHT)*sy));
        for(int i=0;i<KEYS.length;i++) {
            Key k=KEYS[i];
            if(labeled)captions[i].measure(MeasureSpec.makeMeasureSpec(Math.round((k.shape==Shape.SHOULDER?150:137)*sx),MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(Math.round(42*sy),MeasureSpec.EXACTLY));
            getChildAt(i).measure(MeasureSpec.makeMeasureSpec(Math.round(k.width*sx),MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(Math.round(k.height*sy),MeasureSpec.EXACTLY));
        }
    }
    @Override protected void onLayout(boolean changed,int l,int t,int r,int b) {
        for(int i=0;i<KEYS.length;i++) {
            Key k=KEYS[i];
            Cap cap=(Cap)getChildAt(i);
            float cx=left+(labeled?LABELED[i][0]:k.x)*sx, cy=(labeled?LABELED[i][1]:k.y)*sy;
            int x=Math.round(cx-cap.getMeasuredWidth()/2f);
            int y=Math.round(cy-cap.getMeasuredHeight()/2f);
            if(labeled) {
                android.widget.TextView label=captions[i];
                boolean side=k.shape==Shape.SHOULDER;
                int x0=Math.round(side?cx+cap.getMeasuredWidth()/2f:cx-label.getMeasuredWidth()/2f);
                int y0=Math.round(side?cy-label.getMeasuredHeight()/2f:cy+cap.getMeasuredHeight()/2f);
                label.layout(x0,y0,x0+label.getMeasuredWidth(),y0+label.getMeasuredHeight());
            }
            cap.layout(x,y,x+cap.getMeasuredWidth(),y+cap.getMeasuredHeight());
        }
    }
    @Override protected void onDraw(Canvas c) {
        int saved=c.save();
        c.translate(left,0); c.scale(sx,sy);
        if(labeled) {
            paint.setColor(LIP);c.drawRoundRect(5,99,875,494,66,66,paint);
            paint.setColor(SHELL);c.drawRoundRect(5,96,875,486,66,66,paint);
            c.restoreToCount(saved);return;
        }
        paint.setColor(0xff102123); c.drawRoundRect(5,84,565,344,60,60,paint);
        paint.setColor(LIP); c.drawRoundRect(5,77,565,338,60,60,paint);
        paint.setColor(SHELL); c.drawRoundRect(5,77,565,331,60,60,paint);
        // The connected cross is also visible in the gaps between the four hit areas.
        paint.setColor(INK);
        c.drawRoundRect(189,197,227,319,7,7,paint);
        c.drawRoundRect(147,239,269,277,7,7,paint);
        c.restoreToCount(saved);
    }

    private final class Cap extends Button {
        private final Key key;
        private final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);

        Cap(Context context,Key key) {
            super(context);
            this.key=key;
            setPadding(0,0,0,0);
            setBackground(null);
            setStateListAnimator(null);
            setFocusable(true);
            p.setTextAlign(Paint.Align.CENTER);
            p.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        }
        @Override protected void drawableStateChanged() {
            super.drawableStateChanged();
            invalidate();
        }
        @Override protected void onDraw(Canvas canvas) {
            int saved=canvas.save();
            canvas.translate(getWidth()/2f,getHeight()/2f);
            float scale=Math.min(sx,sy);
            canvas.scale(scale,scale);
            boolean lit=isSelected() || isPressed() || (isFocused() && !isInTouchMode());
            int edge=lit?HIGHLIGHT:INK;
            int face=lit?0xff665536:CAP;
            switch(key.shape) {
                case SHOULDER:
                    float w=getWidth()/(2*scale)-3, h=Math.min(17,getHeight()/(2*scale)-2);
                    p.setColor(edge); canvas.drawRoundRect(-w,-h,w,h,10,10,p);
                    p.setColor(face); canvas.drawRoundRect(-w+2,-h+2,w-2,h-3,8,8,p);
                    break;
                case STICK:
                    disc(canvas,41,edge); disc(canvas,37,0xff478b87);
                    if(!labeled) {
                        int stick=key==KEYS[12]?0:2;
                        canvas.translate(tilt[stick]*11,tilt[stick+1]*11);
                    }
                    disc(canvas,32,INK); disc(canvas,29,face); disc(canvas,24,RIM); disc(canvas,22,face);
                    break;
                case ARROW:
                    p.setColor(lit?HIGHLIGHT:INK); canvas.drawRoundRect(-18,-18,18,18,6,6,p);
                    p.setColor(face); canvas.drawRoundRect(-16,-16,16,16,5,5,p);
                    break;
                case FACE:
                    disc(canvas,22,edge); disc(canvas,19,face);
                    break;
                case SYSTEM:
                    disc(canvas,17,edge); disc(canvas,14,face);
                    break;
            }
            p.setColor(lit?HIGHLIGHT:LEGEND);
            float size=key.shape==Shape.STICK||key.shape==Shape.SHOULDER?16:21;
            p.setTextSize(Math.max(size,12*density/scale));
            canvas.drawText(key.legend,0,-(p.ascent()+p.descent())/2,p);
            canvas.restoreToCount(saved);
        }
        private void disc(Canvas canvas,int radius,int color) {
            p.setColor(color); canvas.drawCircle(0,0,radius,p);
        }
    }
}
