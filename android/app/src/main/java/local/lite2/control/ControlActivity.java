package local.lite2.control;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import java.util.Locale;

public final class ControlActivity extends Activity implements InputManager.InputDeviceListener {
    static final int BG=0xff101719, PANEL=0xff192729, EDGE=0xff304448, WHITE=0xffeaf7f3;
    static final int MUTED=0xffa4bab8, MINT=0xff74ebc2, GOLD=0xfff4c778;
    Profiles store;
    PointerBridge pointerBridge;
    String bridgeToken;
    final Runnable bridgeStatus=()->runOnUiThread(this::onBridgeState);
    Profiles.Profile profile;
    String savedProfile;
    boolean dirty;
    Button saveButton, restoreButton, activeSlotButton;
    TextView editSlotLabel;
    TextView draftStatus;
    ScrollView scroll;
    final int[] adjustScroll=new int[2];
    boolean renderedS, renderedAdjust;
    boolean modeS=true, adjustment=false;
    int slot, language;
    int selectedPadButton=-1;
    InputManager inputs;
    TextView status, modeLabel, monitorValues, lastInput;
    LinearLayout root, body, header;
    AssignmentPad monitorPad;
    GyroPreview gyroPreview;
    int pressedMask;
    CurveView leftGraph, rightGraph;
    float lx,ly,rx,ry;
    int activeMouse, hatBits;
    float lt,rt;
    String last="입력 대기 중";
    String preview="";

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        store=new Profiles(this);
        language=Profiles.limit(store.preferences.getInt("language",0),0,Words.COUNT-1);
        inputs=(InputManager)getSystemService(INPUT_SERVICE);
        detectMode();
        if(state!=null) {
            modeS=state.getBoolean("mode",modeS);
            adjustment=state.getBoolean("adjust");
            adjustScroll[0]=state.getInt("scrollS");
            adjustScroll[1]=state.getInt("scrollD");
        }
        int active=store.active(modeS);
        int editing=state==null?active:state.getInt("slot",active);
        openSlot(editing<0 || editing>=Profiles.SLOT_COUNT?active:editing);
        if(state!=null && slot!=Profiles.RAW) {
            profile.decode(state.getString("draft"));
            dirty=!profile.encode().equals(savedProfile);
        }
        inputs.registerInputDeviceListener(this,null);
        drawScreen();
        startBridge(getIntent());
    }
    @Override protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        startBridge(intent);
    }
    private void startBridge(android.content.Intent intent) {
        android.content.SharedPreferences prefs=getSharedPreferences("bridge",MODE_PRIVATE);
        String token=intent==null?null:intent.getStringExtra("bridgeToken");
        if(token!=null && token.matches("[0-9a-f]{32}")) {
            prefs.edit().putString("token",token).apply();
            intent.removeExtra("bridgeToken");
        } else token=prefs.getString("token",null);
        bridgeToken=token;
        if(token!=null)startForegroundService(new android.content.Intent(this,SlotService.class));
        pushPointerDraft();
    }
    private void onBridgeState() {
        if(pointerBridge==null || !pointerBridge.connected) {
            cancelMonitorHold();
            resetPad();
            if(gyroPreview!=null)gyroPreview.waitForInput();
        }
        updateStatus();
        updateMonitor();
    }
    private static final int PLUS_MINUS=(1<<14)|(1<<15), LB_RB=(1<<8)|(1<<9);
    private final android.os.Handler shortcutTimer=new android.os.Handler(android.os.Looper.getMainLooper());
    private int pendingChord,monitorButtons;
    private final Runnable heldShortcut=this::finishMonitorHold;

    private boolean monitorFocused() {
        return resumed && !adjustment && hasWindowFocus() && !isFinishing();
    }
    private void cancelMonitorHold() {
        if(pendingChord!=0)shortcutTimer.removeCallbacks(heldShortcut);
        pendingChord=0;
    }
    private void finishMonitorHold() {
        int chord=pendingChord;
        pendingChord=0;
        if(chord==0 || !monitorFocused() || (monitorButtons&chord)!=chord)return;
        if(chord==PLUS_MINUS) {
            adjustment=true;
            drawScreen();
        } else {
            leaveDraft(this::finish);
        }
    }
    private void checkMonitorHold(int held,int newlyPressed) {
        monitorButtons=held;
        boolean settings=(held&PLUS_MINUS)==PLUS_MINUS;
        boolean exit=(held&LB_RB)==LB_RB;
        int chord=settings==exit?0:settings?PLUS_MINUS:LB_RB;
        if(pendingChord!=chord || !monitorFocused())cancelMonitorHold();
        if(chord!=0 && pendingChord==0 && (newlyPressed&chord)!=0 && monitorFocused()) {
            pendingChord=chord;
            shortcutTimer.postDelayed(heldShortcut,3000);
        }
    }
    private static int displayButtons(int scan) {
        int mask=0;
        for(int bit=0;bit<Profiles.SCAN_BUTTON.length;bit++)
            if((scan&(1<<bit))!=0)mask|=1<<Profiles.SCAN_BUTTON[bit];
        return mask;
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if(!focused)cancelMonitorHold();
    }
    private boolean resumed;
    private void onPhysicalButtons(int held,int down,int up) {
        if(resumed && !adjustment && !dirty && !modeS)openMode(true);
        checkMonitorHold(displayButtons(held),displayButtons(down));
        if(modeS) {
            pressedMask=displayButtons(held);
            drawPad();
        }
        if(modeS && !adjustment && (down|up)!=0) {
            StringBuilder names=new StringBuilder();
            int shown=held==0?up:held;
            for(int bit=0;bit<Profiles.SCAN_BUTTON.length;bit++)if((shown&(1<<bit))!=0) {
                if(names.length()>0)names.append(" + ");
                names.append(t(Profiles.BUTTONS[Profiles.SCAN_BUTTON[bit]]));
            }
            last=t(held==0?"%s · 뗌":"%s · 입력",names.toString());preview="";
            updateMonitor();
        }
        handleSlotButtons(true,down);
    }
    private void onGamepad(int[] values,int down,int up) {
        if(resumed && !adjustment && !dirty && modeS)openMode(false);
        checkMonitorHold(displayButtons(values[0]),displayButtons(down));
        if(!modeS && resumed && !adjustment) {
            pressedMask=displayButtons(values[0]);
            hatBits=0;lx=values[1]/127f;ly=values[2]/127f;
            rx=values[3]/127f;ry=values[4]/127f;lt=values[5]/127f;rt=values[6]/127f;
            if((down|up)!=0) {
                int index=Profiles.SCAN_BUTTON[Integer.numberOfTrailingZeros(down!=0?down:up)];
                last=t(down!=0?"%s · 입력":"%s · 뗌",Profiles.BUTTONS[index]);
            }
            drawPad();updateMonitor();
        }
        handleSlotButtons(false,down);
    }
    private boolean useActiveOnResume;
    private void handleSlotButtons(boolean s,int down) {
        // The monitor displays press edges, including slot bindings, without acting on them.
        if(down==0 || (resumed && (!adjustment || !hasWindowFocus())))return;
        int active=store.active(s);
        Profiles.Profile current=resumed && modeS==s && active!=Profiles.RAW?profile:store.navigation(s);
        int next=Profiles.slotForPresses(current,active,down);
        if(next==Profiles.NO_CHANGE || next==active)return;
        if(resumed && modeS==s)setSlot(s,next);
        else {
            store.select(s,next);
            if(s==modeS)useActiveOnResume=true;
            pushPointerDraft();
            updateStatus();
        }
    }
    private void pushPointerDraft() {
        if(bridgeToken==null)return;
        int active=store.active(true);
        Profiles.Profile current=active==Profiles.RAW?null:modeS && !useActiveOnResume?profile:store.load(true,active);
        char route=resumed && !adjustment?PointerBridge.MONITOR:
            active==Profiles.RAW?PointerBridge.DIRECT:PointerBridge.REMAP;
        pointerBridge=PointerBridge.attach(bridgeToken,bridgeStatus,this::onPhysicalButtons,this::onSticks,store,current,route);
        pointerBridge.gamepad=this::onGamepad;
        pointerBridge.wheel=route==PointerBridge.MONITOR?direction->{
            last=direction>0?"휠 위":"휠 아래";
            preview="";
            updateMonitor();
        }:null;
        pointerBridge.monitorGyro(resumed && modeS && !adjustment && gyroPreview!=null?gyroPreview:null);
        pointerBridge.set(current);
    }
    private void drawPad() {
        int mask=pressedMask;
        if(!modeS)mask|=(hatBits<<4) | (lt>.05f?1<<10:0) | (rt>.05f?1<<11:0);
        if(monitorPad!=null)monitorPad.live(mask,lx,ly,rx,ry);
    }
    private void resetPad() {
        cancelMonitorHold();
        pressedMask=hatBits=0; lx=ly=rx=ry=lt=rt=0;drawPad();
    }
    private void onSticks(int x,int y,int rightX,int rightY) {
        if(!modeS)return;
        if(activeMouse==0 && (x!=0 || y!=0 || rightX!=0 || rightY!=0)){activeMouse=1;updateMonitor();}
        lx=x/127f;ly=y/127f;rx=rightX/127f;ry=rightY/127f;
        drawPad();
    }
    @Override protected void onResume() {
        super.onResume();resumed=true;
        if(useActiveOnResume && !dirty){useActiveOnResume=false;loadSlot();drawScreen();}
        pushPointerDraft();
    }
    @Override protected void onPause() {cancelMonitorHold();resumed=false;pushPointerDraft();super.onPause();}
    @Override public void onDestroy() {
        if(pointerBridge!=null) {
            if(isFinishing()) {
                Profiles.Profile active=store.load(true,store.active(true));
                pointerBridge=PointerBridge.attach(bridgeToken,bridgeStatus,null,null,store,active,
                    active==null?PointerBridge.DIRECT:PointerBridge.REMAP);
                pointerBridge.set(active);
            }
            pointerBridge.detach(bridgeStatus);
        }
        inputs.unregisterInputDeviceListener(this);
        super.onDestroy();
    }
    @Override public void onInputDeviceAdded(int id) { updateDevice(); }
    @Override public void onInputDeviceRemoved(int id) { updateDevice(); }
    @Override public void onInputDeviceChanged(int id) { updateDevice(); }

    private boolean isLite(InputDevice device) {
        if (device==null) return false;
        String name=device.getName().toLowerCase(Locale.ROOT);
        return name.contains("lite2 air mouse") || name.contains("lite 2") || name.contains("lite2")
            || (device.getVendorId()==0x2dc8 && device.getProductId()==0x5112)
            || (device.getVendorId()==0x057e && device.getProductId()==0x2009);
    }
    private boolean isS(InputDevice d) {
        return d.getName().toLowerCase(Locale.ROOT).contains("air mouse")
            || d.getProductId()==0x2009
            || (d.supportsSource(InputDevice.SOURCE_MOUSE) && !d.supportsSource(InputDevice.SOURCE_GAMEPAD));
    }
    private InputDevice connected() {
        for(int id:InputDevice.getDeviceIds()) {
            InputDevice d=InputDevice.getDevice(id);
            if(isLite(d) && !d.getName().startsWith("Lite 2 Control")) return d;
        }
        return null;
    }
    private void detectMode() {
        InputDevice d=connected();
        if(d!=null) modeS=isS(d);
    }
    private void updateDevice() {
        InputDevice device=connected();
        if(device==null)resetPad();
        if(device!=null && isS(device)!=modeS && !dirty) openMode(isS(device));
        else updateStatus();
    }
    private void switchMode(boolean s) {
        if(modeS!=s) leaveDraft(() -> openMode(s));
    }
    private void openMode(boolean s) {
        modeS=s; loadSlot(); drawScreen();
    }
    private void loadSlot() { openSlot(store.active(modeS)); }
    private void openSlot(int selected) { openSlot(selected,false); }
    private void openSlot(int selected,boolean keepInput) {
        useActiveOnResume=false;
        slot=selected;
        profile=store.load(modeS,slot);
        savedProfile=profile==null?null:profile.encode();
        dirty=false;
        if(!keepInput) {
            last="입력 대기 중";preview="";
            resetPad(); activeMouse=0;
        }
        pushPointerDraft();
    }
    private void chooseSlotToEdit(int selected) {
        if(selected!=slot) leaveDraft(()->{openSlot(selected);drawScreen();});
    }
    private void setSlot(int selected) {
        setSlot(modeS,selected);
    }
    private void setSlot(boolean selectedMode,int selected) {
        if(modeS==selectedMode && selected==slot && selected==store.active(selectedMode)) return;
        leaveDraft(() -> {
            boolean keepInput=resumed && !adjustment && modeS==selectedMode;
            modeS=selectedMode;
            store.select(modeS,selected);
            openSlot(selected,keepInput);
            drawScreen();
        });
    }
    private void edited() {
        dirty=!profile.encode().equals(savedProfile);
        updateSaveState();
        updateMonitor();
        pushPointerDraft();
    }
    private void updateSaveState() {
        if(saveButton!=null) {
            saveButton.setEnabled(dirty);
            saveButton.setAlpha(dirty?1f:.46f);
        }
        if(restoreButton!=null) {
            restoreButton.setEnabled(dirty);
            restoreButton.setAlpha(dirty?1f:.46f);
        }
        if(draftStatus!=null) draftStatus.setText(dirty
            ? t(store.active(modeS)==Profiles.RAW?"저장하지 않은 변경 사항":"저장하지 않은 변경 사항 · 미리보기에 적용 중")
            : t("슬롯 %d의 저장값",slot+1));
    }
    private boolean save() {
        if(!dirty) return true;
        store.save(modeS,slot,profile);
        savedProfile=profile.encode();
        dirty=false;
        updateSaveState();
        Toast.makeText(this,t("슬롯 %d에 저장했습니다",slot+1),Toast.LENGTH_SHORT).show();
        return true;
    }
    private void discardDraft() {
        profile.decode(savedProfile);
        dirty=false;
        pushPointerDraft();
    }
    private void leaveDraft(Runnable next) {
        if(!dirty) {next.run();return;}
        new AlertDialog.Builder(this).setTitle(t("변경 사항을 저장할까요?"))
            .setMessage(t("저장하지 않으면 마지막으로 저장한 값으로 돌아갑니다."))
            .setNegativeButton(t("계속 편집"),null)
            .setNeutralButton(t("저장 안 함"),(dialog,which)->{discardDraft();next.run();})
            .setPositiveButton(t("저장"),(dialog,which)->{if(save())next.run();}).show();
    }
    @Override public void onBackPressed() { leaveDraft(this::finish); }
    @Override public void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        rememberScroll();
        state.putBoolean("mode",modeS);
        state.putBoolean("adjust",adjustment);
        state.putInt("slot",slot);
        state.putInt("scrollS",adjustScroll[0]);
        state.putInt("scrollD",adjustScroll[1]);
        if(dirty) state.putString("draft",profile.encode());
    }
    private void rememberScroll() {
        if(scroll!=null && renderedAdjust) adjustScroll[renderedS?0:1]=scroll.getScrollY();
    }

    private String t(String key,Object... args) { return Words.get(language,key,args); }
    private String[] translated(String[] keys) {
        String[] out=new String[keys.length];
        for(int i=0;i<keys.length;i++) out[i]=t(keys[i]);
        return out;
    }
    private void chooseLanguage() {
        AlertDialog picker=new AlertDialog.Builder(this).setTitle(t("언어"))
            .setSingleChoiceItems(Words.menu(),Words.selection(language),(dialog,which)->{
                language=Words.languageAt(which);
                store.preferences.edit().putInt("language",language).apply();
                last="입력 대기 중";preview="";
                dialog.dismiss();drawScreen();
            }).setNegativeButton(t("닫기"),null).create();
        picker.show();
        picker.getListView().setSelection(0);
    }

    int dp(float v) { return Math.round(v*getResources().getDisplayMetrics().density); }
    private GradientDrawable bg(int color,int stroke,int radius) {
        GradientDrawable d=new GradientDrawable();
        d.setColor(color); d.setCornerRadius(dp(radius));
        if(stroke!=0) d.setStroke(dp(1),stroke);
        return d;
    }
    private LinearLayout column() {
        LinearLayout l=new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); return l;
    }
    private LinearLayout row() {
        LinearLayout l=new LinearLayout(this); l.setOrientation(LinearLayout.HORIZONTAL);
        l.setBaselineAligned(false); return l;
    }
    private TextView text(String value,int size,int color,boolean bold) {
        TextView t=new TextView(this); t.setText(t(value)); t.setTextColor(color); t.setTextSize(size);
        if(bold) t.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        return t;
    }
    private Button button(String label, boolean checked, Runnable action) {
        Button b=new Button(this); b.setAllCaps(false); b.setText(t(label));
        b.setTextColor(checked?BG:WHITE); b.setTextSize(14);
        b.setMinHeight(dp(40)); b.setMinimumHeight(0);
        b.setPadding(dp(12),dp(7),dp(12),dp(7));
        b.setBackground(bg(checked?MINT:0xff223538,checked?0:EDGE,12));
        b.setOnClickListener(v->action.run());
        return b;
    }
    private void item(LinearLayout container,View v) {
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);
        p.bottomMargin=dp(10); container.addView(v,p);
    }
    private void split(LinearLayout container,View v) {
        LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);
        p.setMargins(dp(3),0,dp(3),0); container.addView(v,p);
    }
    private LinearLayout card(LinearLayout dest,String title,String subtitle) {
        LinearLayout c=column(); c.setBackground(bg(PANEL,EDGE,20));
        c.setPadding(dp(16),dp(14),dp(16),dp(13));
        if(title!=null) item(c,text(title,20,WHITE,true));
        if(subtitle!=null) item(c,text(subtitle,13,MUTED,false));
        item(dest,c); return c;
    }
    private String modeName(boolean s) { return t(s?"S · 에어마우스":"D · 게임패드"); }
    private void updateStatus() {
        if(status==null || modeLabel==null)return;
        InputDevice device=connected();
        boolean headerS=device==null?modeS:isS(device);
        int active=store.active(headerS);
        modeLabel.setText(modeName(headerS));
        String activeName=active==Profiles.RAW?"RAW":t("활성 슬롯 %d",active+1);
        activeSlotButton.setText(activeName+"  ▾");
        activeSlotButton.setContentDescription(activeName);
        boolean otherSlot=modeS!=headerS || slot!=active;
        editSlotLabel.setVisibility(otherSlot?View.VISIBLE:View.GONE);
        if(otherSlot)editSlotLabel.setText((modeS!=headerS?(modeS?"S · ":"D · "):"")+(slot==Profiles.RAW?"RAW":t(store.active(modeS)==Profiles.RAW?"슬롯 %d":"편집 슬롯 %d 미리보기",slot+1)));
        if(device==null) status.setText(t("연결 대기 · 프로필을 미리 편집할 수 있습니다"));
        else status.setText(device.getName()+(isS(device)==modeS?"":"  ·  "+t("다른 모드 미리보기"))
            +(active!=Profiles.RAW && modeS && isS(device)?"  ·  "+t(pointerBridge!=null && pointerBridge.connected?"Lite 2 커서 보정 연결":"연결 안 됨"):""));
    }
    private void chooseActiveSlot() {
        InputDevice device=connected();
        chooseActiveSlot(device==null?modeS:isS(device));
    }
    private void chooseActiveSlot(boolean selectedMode) {
        String[] slots=new String[Profiles.SLOT_COUNT+1];
        slots[0]="RAW";
        for(int i=1;i<slots.length;i++)slots[i]=t("슬롯 %d",i);
        new AlertDialog.Builder(this).setTitle(modeName(selectedMode))
            .setSingleChoiceItems(slots,store.active(selectedMode)+1,(dialog,index)->{
                dialog.dismiss();
                setSlot(selectedMode,index-1);
            }).setNegativeButton(t("닫기"),null).show();
    }
    private void chooseEditMode() {
        new AlertDialog.Builder(this).setTitle(t("편집 프로필"))
            .setSingleChoiceItems(new String[]{modeName(true),modeName(false)},modeS?0:1,
                (dialog,which)->{dialog.dismiss();switchMode(which==0);})
            .setNegativeButton(t("닫기"),null).show();
    }
    @Override public void onConfigurationChanged(android.content.res.Configuration config) {
        super.onConfigurationChanged(config);
        if(!adjustment)drawScreen();
    }
    private void drawScreen() {
        cancelMonitorHold();
        rememberScroll();
        renderedS=modeS; renderedAdjust=adjustment;
        draftStatus=null; restoreButton=null;
        gyroPreview=null;monitorPad=null;monitorValues=lastInput=null;
        leftGraph=rightGraph=null;
        root=column(); root.setBackgroundColor(BG);
        if(android.os.Build.VERSION.SDK_INT>=30) root.setOnApplyWindowInsetsListener((view,insets)->{
            android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars());
            view.setPadding(bars.left,bars.top,bars.right,bars.bottom);
            return insets;
        });
        header=column(); header.setPadding(dp(20),dp(10),dp(20),0);
        root.addView(header);
        LinearLayout titleRow=row();
        titleRow.setGravity(android.view.Gravity.CENTER_VERTICAL);
        boolean compact=getResources().getConfiguration().screenWidthDp<520;
        TextView title=text(compact?"Lite 2":"Lite 2 Control",compact?22:30,WHITE,true);
        title.setSingleLine();
        title.setEllipsize(android.text.TextUtils.TruncateAt.END);
        titleRow.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        LinearLayout center=column();
        center.setGravity(android.view.Gravity.CENTER);
        titleRow.addView(center,new LinearLayout.LayoutParams(0,-2,1));
        boolean stacked=getResources().getConfiguration().screenWidthDp<840;
        LinearLayout modeAndSlot=stacked?column():row();
        modeAndSlot.setGravity(android.view.Gravity.CENTER);
        center.addView(modeAndSlot);
        modeLabel=text("",compact?14:17,MINT,true);
        modeLabel.setGravity(android.view.Gravity.CENTER);
        modeAndSlot.addView(modeLabel);
        activeSlotButton=button("",false,this::chooseActiveSlot);
        activeSlotButton.setTextColor(MINT);
        LinearLayout.LayoutParams slotSize=new LinearLayout.LayoutParams(-2,-2);
        if(!stacked)slotSize.setMarginStart(dp(8));
        modeAndSlot.addView(activeSlotButton,slotSize);
        editSlotLabel=text("",13,MUTED,false);
        editSlotLabel.setGravity(android.view.Gravity.CENTER);
        center.addView(editSlotLabel);
        LinearLayout headerActions=row();
        headerActions.setGravity(android.view.Gravity.END|android.view.Gravity.CENTER_VERTICAL);
        titleRow.addView(headerActions,new LinearLayout.LayoutParams(0,-2,1));
        saveButton=button("저장",true,this::save);
        if(slot==Profiles.RAW)saveButton.setVisibility(View.GONE);
        saveButton.setContentDescription(t("현재 슬롯 저장"));
        Button globe=button("◎",false,this::chooseLanguage);
        globe.setText("🌐");globe.setTextSize(19);
        globe.setContentDescription(t("언어"));
        LinearLayout.LayoutParams globeSize=new LinearLayout.LayoutParams(dp(compact?42:48),-2);
        globeSize.setMarginEnd(dp(8));
        headerActions.addView(globe,globeSize);
        headerActions.addView(saveButton,new LinearLayout.LayoutParams(dp(compact?64:82),-2));
        item(header,titleRow);
        status=text("",14,MINT,false); item(header,status); updateStatus();
        LinearLayout tabs=row();
        split(tabs,button("모니터",!adjustment,()->{adjustment=false;drawScreen();}));
        split(tabs,button("설정",adjustment,()->{adjustment=true;drawScreen();}));
        item(header,tabs);
        body=column(); body.setPadding(dp(20),dp(4),dp(20),dp(12));
        if(adjustment) {
            scroll=new ScrollView(this);
            scroll.setFillViewport(true);
            root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
            scroll.addView(body);
            makeAdjust();
            restoreScrollBeforeDraw(scroll,adjustScroll[modeS?0:1]);
        } else {
            scroll=null;
            root.addView(body,new LinearLayout.LayoutParams(-1,0,1));
            makeMonitor();
        }
        setContentView(root);
        root.requestApplyInsets();
        pushPointerDraft();
        updateSaveState();
        updateMonitor();
    }
    private void restoreScrollBeforeDraw(ScrollView view,int position) {
        if(position<=0)return;
        view.getViewTreeObserver().addOnPreDrawListener(new android.view.ViewTreeObserver.OnPreDrawListener(){
            @Override public boolean onPreDraw() {
                view.getViewTreeObserver().removeOnPreDrawListener(this);
                int before=view.getScrollY();
                view.scrollTo(0,position);
                if(view.getScrollY()==before)return true;
                // Do not present a frame laid out at the top of the page.
                view.postInvalidateOnAnimation();
                return false;
            }
        });
    }

    private void makeMonitor() {
        boolean raw=store.active(modeS)==Profiles.RAW;
        boolean landscape=getResources().getConfiguration().orientation==
            android.content.res.Configuration.ORIENTATION_LANDSCAPE;
        LinearLayout panel=landscape?row():column();
        panel.setBackground(bg(PANEL,EDGE,20));
        panel.setPadding(dp(12),dp(10),dp(12),dp(10));
        body.addView(panel,new LinearLayout.LayoutParams(-1,-1));
        LinearLayout diagram=column();
        diagram.setGravity(android.view.Gravity.CENTER);
        panel.addView(diagram,landscape?new LinearLayout.LayoutParams(0,-1,1.25f):
            new LinearLayout.LayoutParams(-1,0,1));
        monitorPad=new AssignmentPad(this,null);
        diagram.addView(monitorPad,new LinearLayout.LayoutParams(-1,-2));
        drawPad();

        LinearLayout info=column();
        boolean graphs=!modeS && !raw;
        info.setGravity(android.view.Gravity.CENTER_VERTICAL);
        panel.addView(info,landscape?new LinearLayout.LayoutParams(0,-1,1):
            new LinearLayout.LayoutParams(-1,graphs?0:-2,graphs?.8f:0));
        info.addView(text(modeS?"에어마우스 모니터":"게임패드 모니터",20,WHITE,true));
        info.addView(text("＋와 −를 함께 3초 누르면 설정이 열립니다.",14,MUTED,false));
        info.addView(text("LB와 RB를 함께 3초 누르면 앱 화면이 닫힙니다.",14,MUTED,false));
        if(raw)info.addView(text("패드의 기본 입력을 그대로 사용합니다.",13,MUTED,false));
        monitorValues=text("",17,WHITE,false); info.addView(monitorValues);
        lastInput=text("",15,GOLD,false); info.addView(lastInput);
        if(modeS) {
            LinearLayout gyro=row();
            gyro.setGravity(android.view.Gravity.CENTER_VERTICAL);
            gyro.addView(text("자이로",14,MUTED,true));
            gyroPreview=new GyroPreview(this);
            gyroPreview.setContentDescription(t("자이로"));
            gyro.addView(gyroPreview,new LinearLayout.LayoutParams(dp(156),dp(88)));
            info.addView(gyro);
        }
        if(graphs) {
            LinearLayout plots=row();
            info.addView(plots,new LinearLayout.LayoutParams(-1,0,1));
            leftGraph=monitorCurve(plots,profile.left,"왼쪽 스틱");
            rightGraph=monitorCurve(plots,profile.right,"오른쪽 스틱");
        }
        updateMonitor();
    }

    private CurveView monitorCurve(LinearLayout plots,Profiles.Stick stick,String title) {
        LinearLayout section=column();
        plots.addView(section,new LinearLayout.LayoutParams(0,-1,1));
        section.addView(text(title,14,MUTED,true));
        CurveView graph=new CurveView(this,stick,false);
        graph.setContentDescription(t("%s 반응 곡선",t(title)));
        section.addView(graph,new LinearLayout.LayoutParams(-1,0,1));
        return graph;
    }

    private void makeAdjust() {
        LinearLayout profiles=card(body,"저장 슬롯",store.active(modeS)==Profiles.RAW?
            "패드의 기본 입력을 그대로 사용합니다.":"모드별 슬롯 3개 · 우상단 저장 버튼으로 변경값을 기록합니다.");
        item(profiles,button(t("편집 프로필 · %s",modeName(modeS)),false,this::chooseEditMode));
        LinearLayout slots=row();
        for(int i=0;i<Profiles.SLOT_COUNT;i++){
            int index=i;
            split(slots,button(t("슬롯 %d",i+1),slot==i,()->chooseSlotToEdit(index)));
        }
        item(profiles,slots);
        if(!android.provider.Settings.canDrawOverlays(this))
            item(profiles,button("다른 앱 위에 슬롯 표시",false,()->startActivity(new android.content.Intent(
                android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                android.net.Uri.parse("package:"+getPackageName())))));
        if(slot==Profiles.RAW)return;
        if(slot!=store.active(modeS))
            item(profiles,button("이 슬롯 사용",false,()->setSlot(slot)));
        draftStatus=text("",13,MINT,false); item(profiles,draftStatus);
        LinearLayout resets=row();
        split(resets,button("기본값",false,()->{
            profile=new Profiles.Profile(modeS);
            edited();
            drawScreen();
        }));
        restoreButton=button("저장 전 값",false,()->{
            discardDraft();
            drawScreen();
        });
        split(resets,restoreButton);
        item(profiles,resets);
        if(!modeS)item(profiles,text("D 프로필 미리보기 · 설정이 게임패드 입력에 직접 적용되지 않습니다.",13,GOLD,false));
        makeButtonAssignments();
        makeStickGraphs(body,true);
        if(modeS) {
            LinearLayout imu=card(body,"자이로",null);
            slider(imu,"최대 감도",profile.gyroMax,0,Profiles.MAX_GYRO_PERCENT,"%",n->{profile.gyroMax=n;edited();},100);
            android.widget.Switch hold=new android.widget.Switch(this);
            hold.setText(t("클릭 시 자이로 고정 (0.2초)"));hold.setTextColor(WHITE);
            hold.setChecked(profile.clickHold);
            hold.setOnCheckedChangeListener((button,checked)->{profile.clickHold=checked;edited();});
            item(imu,hold);
            slider(imu,"자동 모드 스틱 유지",profile.holdMs/100,0,50," × 0.1초",n->{profile.holdMs=n*100;edited();});
        }
    }
    private void makeButtonAssignments() {
        LinearLayout map=card(body,"버튼 할당",null);
        TextView detail=text("",15,MUTED,false);
        item(map,detail);
        AssignmentOverview pad=new AssignmentOverview(this,(current,index)->{
            selectedPadButton=index;
            refreshAssignment(current,detail);
            pickAction(index,()->refreshAssignment(current,detail));
        });
        map.addView(pad,new LinearLayout.LayoutParams(-1,-2));
        refreshAssignment(pad,detail);
    }
    private void refreshAssignment(AssignmentOverview pad,TextView detail) {
        for(int i=0;i<Profiles.BUTTONS.length;i++) {
            pad.show(i,t(Profiles.BUTTONS[i]),t(Profiles.ACTIONS[profile.map[i]]),
                profile.map[i]==1,i==selectedPadButton);
        }
        pad.select(selectedPadButton);
        detail.setTextColor(selectedPadButton<0?MUTED:GOLD);
        detail.setText(selectedPadButton<0?t("패드에서 설정할 버튼을 누르세요."):
            t(Profiles.BUTTONS[selectedPadButton])+"   →   "+t(Profiles.ACTIONS[profile.map[selectedPadButton]]));
    }
    private void pickAction(int index,Runnable refresh) {
        int count=modeS?21:Profiles.ACTIONS.length;
        String[] choices=new String[count];
        int[] ids=new int[count];
        int checked=-1;
        for(int i=0;i<count;i++) {
            ids[i]=modeS && i==20?Profiles.PREVIOUS_SLOT:i;
            choices[i]=t(Profiles.ACTIONS[ids[i]]);
            if(ids[i]==profile.map[index])checked=i;
        }
        new AlertDialog.Builder(this)
            .setTitle(t("%s 동작",t(Profiles.BUTTONS[index])))
            .setSingleChoiceItems(choices,checked,(d,i)->{
                profile.map[index]=ids[i];edited();
                refresh.run();
                d.dismiss();
            }).setNegativeButton(t("닫기"),null).show();
    }
    interface Changed { void set(int value); }
    private void slider(LinearLayout l,String label,int value,int min,int max,String suffix,Changed changed) {
        slider(l,label,value,min,max,suffix,changed,null);
    }
    private void slider(LinearLayout l,String label,int value,int min,int max,String suffix,Changed changed,Integer defaultValue) {
        TextView text=text(t(label)+"  "+value+t(suffix),14,MUTED,false);
        l.addView(text);
        SeekBar seek=new SeekBar(this); seek.setMax(max-min); seek.setProgress(value-min);
        seek.setContentDescription(t(label));
        if(defaultValue==null) item(l,seek);
        else {
            LinearLayout controls=row(); controls.setGravity(android.view.Gravity.CENTER_VERTICAL);
            controls.addView(seek,new LinearLayout.LayoutParams(0,dp(48),1));
            Button defaults=button(t("기본값")+" "+defaultValue+"%",false,()->seek.setProgress(defaultValue-min));
            defaults.setContentDescription(t(label)+" · "+t("기본값")+" "+defaultValue+"%");
            controls.addView(defaults);
            item(l,controls);
        }
        seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
            public void onStartTrackingTouch(SeekBar s){}
            public void onStopTrackingTouch(SeekBar s){}
            public void onProgressChanged(SeekBar s,int p,boolean user) {
                text.setText(t(label)+"  "+(p+min)+t(suffix));
                changed.set(p+min);
            }
        });
    }
    private void makeStickGraphs(LinearLayout content,boolean adjust) {
        boolean wide=getResources().getConfiguration().screenWidthDp>=660;
        if(wide) {
            LinearLayout r=row();
            LinearLayout left=column(),right=column();
            split(r,left); split(r,right);
            item(content,r);
            stickCard(left,true,adjust); stickCard(right,false,adjust);
        } else {
            stickCard(content,true,adjust); stickCard(content,false,adjust);
        }
    }
    private void stickCard(LinearLayout container,boolean left,boolean adjust) {
        Profiles.Stick stick=left?profile.left:profile.right;
        String title=left?"왼쪽 스틱":modeS?"오른쪽 스틱 · 휠":"오른쪽 스틱";
        LinearLayout c=card(container,title,"가로축은 기울기, 세로축은 적용할 반응입니다.");
        CurveView plot=curveEditor(c,stick,title,adjust);
        if(left)leftGraph=plot;else rightGraph=plot;
    }
    private CurveView curveEditor(LinearLayout c,Profiles.Stick stick,String title,boolean adjust) {
        CurveView plot=new CurveView(this,stick,adjust);
        plot.setContentDescription(t("%s 반응 곡선",t(title)));
        LinearLayout.LayoutParams size=new LinearLayout.LayoutParams(-1,dp(156));
        size.bottomMargin=dp(8); c.addView(plot,size);
        if(!adjust)return plot;
        plot.readout=text("",13,GOLD,true);
        item(c,plot.readout);
        Button countButton=button(t("내부 점 %d개",stick.count-2),false,()->{});
        countButton.setOnClickListener(v->{
            String[] counts=new String[Profiles.Stick.MAX-Profiles.Stick.MIN+1];
            for(int i=0;i<counts.length;i++)counts[i]=t("내부 점 %d개",Profiles.Stick.MIN+i-2);
            new AlertDialog.Builder(this).setTitle(t("%s 반응 곡선",t(title)))
                .setSingleChoiceItems(counts,stick.count-Profiles.Stick.MIN,(dialog,index)->{
                    stick.resize(Profiles.Stick.MIN+index);
                    countButton.setText(t("내부 점 %d개",stick.count-2));
                    plot.curveButton.setText(t("곡선 · %s",t(Profiles.CURVES[stick.curve])));
                    edited();plot.invalidate();dialog.dismiss();
                }).setNegativeButton(t("닫기"),null).show();
        });
        item(c,countButton);
        Button curveButton=button(t("곡선 · %s",t(Profiles.CURVES[stick.curve])),false,()->{});
        plot.curveButton=curveButton;
        curveButton.setOnClickListener(v->new AlertDialog.Builder(this).setTitle(t("%s 반응 곡선",t(title)))
            .setSingleChoiceItems(translated(Profiles.CURVES),stick.curve,(d,i)->{
                if(i==4) stick.startCustom(); else stick.preset(i);
                edited();
                curveButton.setText(t("곡선 · %s",t(Profiles.CURVES[i])));
                plot.invalidate();
                d.dismiss();
            }).setNegativeButton(t("닫기"),null).show());
        item(c,curveButton);
        return plot;
    }

    private void updateMonitor() {
        if(monitorValues!=null) {
            if(modeS) {
                boolean captured=pointerBridge!=null && pointerBridge.route==PointerBridge.MONITOR && pointerBridge.connected;
                monitorValues.setText(t(captured?"모니터 입력 수신":connected()==null?"입력 대기 중":"모니터 연결 대기 · 커서가 움직일 수 있습니다"));
            }
            else monitorValues.setText(t("왼쪽 X %+.2f Y %+.2f   오른쪽 X %+.2f Y %+.2f",lx,ly,rx,ry)
                +String.format(Locale.ROOT,"\nLT %.2f    RT %.2f",lt,rt));
        }
        if(lastInput!=null) lastInput.setText(t(last)+(preview.isEmpty()?"":"\n"+preview));
        updatePlot(leftGraph,Math.min(1f,(float)Math.hypot(lx,ly)));
        updatePlot(rightGraph,Math.min(1f,(float)Math.hypot(rx,ry)));
    }
    private void updatePlot(CurveView graph,float physicalInput) {
        if(graph==null)return;
        graph.live=physicalInput;
        if(graph.readout!=null) graph.readout.setText(t("입력 %d%% → 출력 %d%%",
            Math.round(graph.live*100),Math.round(graph.stick.output(graph.live)*100))
            +"   ·   "+t("데드존")+" 0–"+graph.percent(graph.stick.deadEdge()));
        graph.invalidate();
    }
    private int buttonIndex(int code,boolean s) {
        switch(code) {
            case KeyEvent.KEYCODE_BUTTON_A: return 0;
            case KeyEvent.KEYCODE_BUTTON_B: return 1;
            case KeyEvent.KEYCODE_BUTTON_X: return 2;
            case KeyEvent.KEYCODE_BUTTON_Y: return 3;
            case KeyEvent.KEYCODE_DPAD_UP: return 4;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 5;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 6;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 7;
            case KeyEvent.KEYCODE_BUTTON_L1: return 8;
            case KeyEvent.KEYCODE_BUTTON_R1: return 9;
            case KeyEvent.KEYCODE_BUTTON_L2: return 10;
            case KeyEvent.KEYCODE_BUTTON_R2: return 11;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 12;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 13;
            case KeyEvent.KEYCODE_BUTTON_START: return 14;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return 15;
            case KeyEvent.KEYCODE_BUTTON_MODE: return 17;
            case KeyEvent.KEYCODE_BACK: return s?0:-1;
            case KeyEvent.KEYCODE_ENTER: return s?1:-1;
            default:return -1;
        }
    }
    private void previewButton(int index) {
        int action=profile.map[index];
        last=t("%s · 입력",t(Profiles.BUTTONS[index]));
        preview=t("할당 미리보기 · %s",t(Profiles.ACTIONS[action]));
        updateMonitor();
    }
    @Override public boolean dispatchKeyEvent(KeyEvent e) {
        InputDevice device=e.getDevice();
        if(!isLite(device)) return super.dispatchKeyEvent(e);
        boolean s=isS(device);
        if(!adjustment && s!=modeS && !dirty) openMode(s);
        int index=buttonIndex(e.getKeyCode(),s);
        if(s==modeS && index>=0 && (!s || pointerBridge==null || !pointerBridge.hasButtonReports)) {
            int before=pressedMask;
            if(e.getAction()==KeyEvent.ACTION_DOWN)pressedMask|=1<<index;
            if(e.getAction()==KeyEvent.ACTION_UP)pressedMask&=~(1<<index);
            checkMonitorHold(pressedMask,pressedMask & ~before);
            drawPad();
        }
        if(!adjustment) {
            if(s==modeS && index>=0 && profile!=null && e.getAction()==KeyEvent.ACTION_DOWN
                && !(s && pointerBridge!=null && pointerBridge.hasButtonReports))previewButton(index);
            return true;
        }
        return super.dispatchKeyEvent(e);
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e) {
        InputDevice device=e.getDevice();
        if(!isLite(device))return super.dispatchGenericMotionEvent(e);
        boolean s=isS(device);
        if(!adjustment && s!=modeS && !dirty) openMode(s);
        if(!s && !modeS && e.isFromSource(InputDevice.SOURCE_JOYSTICK)) {
            lx=e.getAxisValue(MotionEvent.AXIS_X); ly=e.getAxisValue(MotionEvent.AXIS_Y);
            rx=e.getAxisValue(MotionEvent.AXIS_Z); ry=e.getAxisValue(MotionEvent.AXIS_RZ);
            if(device.getMotionRange(MotionEvent.AXIS_Z)==null) rx=e.getAxisValue(MotionEvent.AXIS_RX);
            if(device.getMotionRange(MotionEvent.AXIS_RZ)==null) ry=e.getAxisValue(MotionEvent.AXIS_RY);
            lt=Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER),e.getAxisValue(MotionEvent.AXIS_BRAKE));
            rt=Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER),e.getAxisValue(MotionEvent.AXIS_GAS));
            float hx=e.getAxisValue(MotionEvent.AXIS_HAT_X), hy=e.getAxisValue(MotionEvent.AXIS_HAT_Y);
            int hats=(hy<-.5f?1:0)|(hy>.5f?2:0)|(hx<-.5f?4:0)|(hx>.5f?8:0);
            int pressed=hats & ~hatBits;
            hatBits=hats;drawPad();
            boolean raw=store.active(false)==Profiles.RAW;
            if(!adjustment && !raw) for(int i=0;i<4;i++) if((pressed&(1<<i))!=0) {previewButton(4+i);break;}
            updateMonitor();
            return !adjustment || !raw || super.dispatchGenericMotionEvent(e);
        }
        if(!adjustment && s && modeS && e.isFromSource(InputDevice.SOURCE_MOUSE)) {
            activeMouse=1;
            if(e.getAction()==MotionEvent.ACTION_SCROLL) {
                last=e.getAxisValue(MotionEvent.AXIS_VSCROLL)>0?"휠 위":"휠 아래";
                preview="";
            } else if(e.getAction()==MotionEvent.ACTION_BUTTON_PRESS) {
                last=e.getActionButton()==MotionEvent.BUTTON_SECONDARY?"우클릭":"왼클릭";preview="";
            }
            updateMonitor();
        }
        return !adjustment || super.dispatchGenericMotionEvent(e);
    }

    @Override public boolean dispatchTouchEvent(MotionEvent e) {
        if(!adjustment && isLite(e.getDevice()))return true;
        return super.dispatchTouchEvent(e);
    }

    final class CurveView extends View {
        final Profiles.Stick stick;
        final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        float live=-1;
        int dragging=-1, pointerId=-1;
        float touchStartX,touchStartY,handleStartX,handleStartY;
        boolean moved;
        TextView readout;
        final boolean editable;
        Button curveButton;
        CurveView(Context context,Profiles.Stick stick,boolean editable) {
            super(context); this.stick=stick; this.editable=editable;
            setContentDescription(t("스틱 입력과 출력 곡선"));
        }
        @Override public boolean onTouchEvent(MotionEvent event) {
            if(!editable)return super.onTouchEvent(event);
            int action=event.getActionMasked();
            float top=dp(12),height=getHeight()-top-dp(34);
            float left=dp(29),width=getWidth()-left-dp(21);
            if(height<=0 || width<=0)return false;
            if(action==MotionEvent.ACTION_DOWN) {
                dragging=-1;
                float radius=dp(18),nearest=radius*radius;
                for(int i=0;i<stick.count;i++) {
                    float input=stick.pointInput(i);
                    float handleY=top+height*(1-stick.pointOutput(i));
                    float dx=event.getX()-(left+width*input),dy=event.getY()-handleY;
                    float distance=dx*dx+dy*dy;
                    if(distance<=nearest) {
                        dragging=i;nearest=distance;handleStartY=handleY;handleStartX=left+width*input;
                    }
                }
                if(dragging<0)return false;
                pointerId=event.getPointerId(0);
                touchStartX=event.getX();touchStartY=event.getY();
                moved=false;
                getParent().requestDisallowInterceptTouchEvent(true);
                invalidate();
                return true;
            }
            if(dragging<0)return false;
            int index=event.findPointerIndex(pointerId);
            if(action==MotionEvent.ACTION_CANCEL || index<0) {
                releaseHandle();
                return true;
            }
            if(action==MotionEvent.ACTION_POINTER_UP && event.getActionIndex()!=index)return true;
            if(action==MotionEvent.ACTION_MOVE || action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) {
                float dy=event.getY(index)-touchStartY;
                float dx=event.getX(index)-touchStartX;
                if(!moved && Math.hypot(dx,dy)>android.view.ViewConfiguration.get(getContext()).getScaledTouchSlop())
                    moved=true;
                if(moved) {
                    int x=Math.round(100*(handleStartX+dx-left)/width)*100;
                    int y=Math.round(100*(1-(handleStartY+dy-top)/height))*100;
                    if(stick.move(dragging,x,y)) {
                        curveButton.setText(t("곡선 · %s",t(Profiles.CURVES[4])));
                        edited();
                    }
                }
                if(action!=MotionEvent.ACTION_MOVE) {
                    if(!moved)performClick();
                    releaseHandle();
                }
            }
            return true;
        }
        private void releaseHandle() {
            dragging=-1;pointerId=-1;
            getParent().requestDisallowInterceptTouchEvent(false);
            invalidate();
        }
        @Override public boolean performClick() { super.performClick(); return true; }
        @Override public void onDraw(Canvas c) {
            super.onDraw(c);
            float x=dp(29), y=dp(12), w=getWidth()-x-dp(21), h=getHeight()-y-dp(34);
            if(w<1||h<1)return;
            paint.setColor(0xff111d1f); paint.setStyle(Paint.Style.FILL);
            c.drawRoundRect(0,0,getWidth(),getHeight(),dp(12),dp(12),paint);
            paint.setColor(0xff243e3a);
            c.drawRect(x,y,x+w*stick.deadEdge()/Profiles.Stick.SCALE,y+h,paint);
            paint.setColor(EDGE); paint.setStrokeWidth(dp(1));
            for(int i=0;i<=4;i++){
                float f=i/4f; c.drawLine(x+w*f,y,x+w*f,y+h,paint);
                c.drawLine(x,y+h*f,x+w,y+h*f,paint);
            }
            paint.setColor(MUTED); paint.setTextSize(dp(10));
            c.drawText("100",dp(5),y+dp(6),paint);
            c.drawText("0",x-dp(17),y+h,paint);
            Path path=new Path(); path.moveTo(x,y+h*(1-stick.pointOutput(0)));
            for(int i=1;i<=100;i++){
                float f=i/100f; path.lineTo(x+w*f,y+h*(1-stick.output(f)));
            }
            paint.setColor(MINT);paint.setStrokeWidth(dp(3));paint.setStyle(Paint.Style.STROKE);
            c.drawPath(path,paint);
            paint.setStyle(Paint.Style.FILL);
            if(editable)drawHandles(c,x,y,w,h);
            else {
                paint.setColor(MUTED);paint.setTextSize(dp(10));
                for(int tick=0;tick<=100;tick+=25) {
                    String label=tick+"%";
                    c.drawText(label,x+w*tick/100-paint.measureText(label)/2,y+h+dp(20),paint);
                }
            }
            if(live>=0){
                paint.setColor(GOLD); c.drawCircle(x+w*live,y+h*(1-stick.output(live)),dp(4),paint);
            }
        }
        String percent(int units) {return (units/Profiles.Stick.STEP)+"%";}
        private void drawHandles(Canvas c,float x,float y,float w,float h) {
            paint.setTextSize(dp(11));
            float axis=y+h;
            int n=stick.count;
            // Keep close X labels readable on the axis and lead each one to its tick.
            float[] centers=new float[n],half=new float[n];
            android.graphics.RectF[] outputs=new android.graphics.RectF[n];
            for(int i=0;i<n;i++) {
                centers[i]=x+w*stick.pointInput(i);
                half[i]=paint.measureText(percent(stick.x[i]))/2;
                if(i>0)centers[i]=Math.max(centers[i],centers[i-1]+half[i-1]+dp(4)+half[i]);
            }
            centers[n-1]=Math.min(centers[n-1],getWidth()-dp(2)-half[n-1]);
            for(int i=n-2;i>=0;i--)centers[i]=Math.min(centers[i],centers[i+1]-half[i+1]-dp(4)-half[i]);
            for(int i=0;i<n;i++) {
                float cx=x+w*stick.pointInput(i),cy=y+h*(1-stick.pointOutput(i));
                paint.setColor(i==dragging?GOLD:MINT);
                c.drawCircle(cx,cy,dp(6),paint);
                paint.setStrokeWidth(dp(1));
                c.drawLine(cx,axis+dp(2),cx,axis+dp(6),paint);
                c.drawLine(cx,axis+dp(6),centers[i],axis+dp(12),paint);
                c.drawText(percent(stick.x[i]),centers[i]-half[i],axis+dp(25),paint);

                String label=percent(stick.y[i]);
                float lw=paint.measureText(label);
                float px=cx+dp(9);
                if(px+lw>getWidth()-dp(3))px=cx-dp(9)-lw;
                float baseline=Math.max(y+dp(9),Math.min(axis-dp(2),cy-dp(5)));
                android.graphics.RectF box=new android.graphics.RectF(px,baseline-dp(11),px+lw,baseline+dp(2));
                for(int step=0;step<2*n;step++) {
                    boolean free=true;
                    for(int j=0;j<i;j++)if(android.graphics.RectF.intersects(box,outputs[j])){free=false;break;}
                    if(free)break;
                    float shift=(step/2+1)*dp(13)*(step%2==0?1:-1);
                    float top=Math.max(y-dp(7),Math.min(axis-dp(14),baseline-dp(11)+shift));
                    box.offsetTo(px,top);
                }
                outputs[i]=box;
                paint.setColor(0xff111d1f);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(dp(2));
                c.drawText(label,box.left,box.bottom-dp(2),paint);
                paint.setStyle(Paint.Style.FILL);paint.setColor(WHITE);
                c.drawText(label,box.left,box.bottom-dp(2),paint);
            }
        }
    }
}
