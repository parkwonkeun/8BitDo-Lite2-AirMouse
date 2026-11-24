package local.lite2.control;

import android.content.SharedPreferences;
import android.media.AudioManager;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

final class Profiles {
    static final String[] BUTTONS = {
        "A","B","X","Y","↑","↓","←","→","LB","RB","LT","RT",
        "L3","R3","＋","－","★","전원"
    };
    static final String[] ACTIONS = {
        "기본 입력","미할당","왼클릭","우클릭","가운데 클릭","휠 위","휠 아래",
        "Enter","뒤로","홈","화살표 위","화살표 아래","화살표 왼쪽","화살표 오른쪽",
        "포인터 모드 전환","","다음 슬롯","슬롯 1","슬롯 2","슬롯 3",
        "게임패드 A","게임패드 B","게임패드 X","게임패드 Y","십자키 위","십자키 아래",
        "십자키 왼쪽","십자키 오른쪽","게임패드 LB","게임패드 RB","게임패드 LT","게임패드 RT",
        "게임패드 L3","게임패드 R3","Start","Select","Capture","게임패드 Home","이전 슬롯","볼륨 올리기","볼륨 내리기","슬롯 전환","포인터 속도 낮추기","포인터 속도 높이기"
    };
    static final String[] CURVES = {"균형", "초반 빠르게", "초반 느리게", "직선", "직접 그리기"};
    static final int NO_CHANGE = -2, RAW = -1, SLOT_COUNT = 3, XBOX=3, SWITCH=4, STAR=16;
    static int[] slots(boolean s) {return s?new int[]{RAW,0,1,2}:new int[]{RAW,XBOX,SWITCH,0,1,2};}
    static boolean validSlot(boolean s,int slot) {return slot>=RAW && slot<=(s?2:SWITCH);}
    static String slotLabel(int language,int slot) {
        if(slot==XBOX)return "Xbox";
        if(slot==SWITCH)return "Switch";
        return slot==RAW?"RAW":Words.get(language,"슬롯 %d",slot+1);
    }
    static boolean fixedLayout(boolean airMouse,int slot) {
        return !airMouse && (slot==XBOX || slot==SWITCH);
    }
    // The printed face order is east, south, north, west on Lite 2.
    static int faceIndex(int slot,int button) {
        return slot==XBOX && button<4?button^1:button;
    }
    static boolean gamepadBinding(int id){return id==0 || (id>=20 && id<=37);}
    static final int MAX_SPEED_PERCENT = 2000, MAX_STICK_PERCENT = 2000;
    // Action IDs are stored in existing slots. Keep the removed gyro-zero ID reserved.
    static final int OLD_GYRO_ZERO=15, NEXT_SLOT=16, PREVIOUS_SLOT=38, VOLUME_UP=39, VOLUME_DOWN=40;
    static final int CYCLE_SLOT=41, SPEED_DOWN=42, SPEED_UP=43;
    static final int LB=8, RB=9;
    static int[] assignableActions(boolean airMouse) {
        return java.util.stream.IntStream.range(0,ACTIONS.length)
            .filter(id->(airMouse || id<SPEED_DOWN) && id!=OLD_GYRO_ZERO && (!airMouse || id<20 || id>=PREVIOUS_SLOT))
            .toArray();
    }
    static final int[] FIRMWARE_ACTIONS={8,7,3,2,10,11,12,13,VOLUME_DOWN,VOLUME_UP,SPEED_DOWN,SPEED_UP,14,1,1,1,9,9};
    static final int LT=10, RT=11, PLUS=14, MINUS=15;
    static int targetSlot(int active,int action) {
        return targetSlot(true,active,action);
    }
    static int targetSlot(boolean s,int active,int action) {
        int[] order=slots(s);
        int at=0;
        while(at<order.length && order[at]!=active)at++;
        if(at==order.length)return NO_CHANGE;
        if(action==PREVIOUS_SLOT)return order[(at+order.length-1)%order.length];
        if(action==NEXT_SLOT || action==CYCLE_SLOT)return order[(at+1)%order.length];
        if(action>NEXT_SLOT && action<=NEXT_SLOT+SLOT_COUNT)return action-NEXT_SLOT-1;
        return NO_CHANGE;
    }

    // Physical scanner bit number to button index used by persisted profiles.
    static final int[] SCAN_BUTTON={14,12,13,15,2,3,7,6,5,4,8,9,1,0,LT,RT,16,17};
    void applyVolumePresses(Profile profile,int active,int down) {
        if(active==RAW || profile==null || !profile.airMouse)return;
        int delta=0;
        for(int bit=0;bit<SCAN_BUTTON.length;bit++) {
            if((down&(1<<bit))==0)continue;
            int action=profile.map[SCAN_BUTTON[bit]];
            if(action==VOLUME_UP)delta++;
            if(action==VOLUME_DOWN)delta--;
        }
        if(delta!=0)context.getSystemService(AudioManager.class).adjustStreamVolume(
            AudioManager.STREAM_MUSIC,delta>0?AudioManager.ADJUST_RAISE:AudioManager.ADJUST_LOWER,AudioManager.FLAG_SHOW_UI);
    }
    static int slotForPresses(Profile profile,int active,int down) {
        int next=NO_CHANGE;
        for(int bit=0;bit<SCAN_BUTTON.length;bit++)if((down&(1<<bit))!=0) {
            int button=SCAN_BUTTON[bit];
            int action=profile!=null?profile.map[button]:
                button==MINUS?PREVIOUS_SLOT:button==PLUS?NEXT_SLOT:0;
            int target=targetSlot(profile==null || profile.airMouse,active,action);
            if(target==NO_CHANGE)continue;
            if(next!=NO_CHANGE && next!=target)return NO_CHANGE;
            next=target;
        }
        return next;
    }
    // Change routes only after a navigation button is released. Its held
    // reports can then never start a second navigation in the new route.
    static final class SlotPress {
        int pending=NO_CHANGE,buttons,origin;
        void reset(){pending=NO_CHANGE;buttons=0;}
        int update(Profile p,int active,int held,int down) {
            if(pending!=NO_CHANGE) {
                if(active!=origin || (down&~buttons)!=0){reset();return NO_CHANGE;}
                if((held&buttons)==0){int result=pending;reset();return result;}
                return NO_CHANGE;
            }
            int next=slotForPresses(p,active,down);
            if(next!=NO_CHANGE && next!=active){pending=next;buttons=down;origin=active;}
            return NO_CHANGE;
        }
    }
    static final class LegacyCurve {
        int dead = 14, gain = 100, curve = 0;
        final int[] points={25,50,75};
        float output(float mag) {
            float edge = dead / 100f;
            float t = Math.min(1f, Math.max(0f, (mag - edge) / (1f - edge)));
            return Math.min(1f, shape(t)*gain/100f);
        }
        private float shape(float input) {
            if (curve==4) {
                float scaled=Math.min(1,Math.max(0,input))*4;
                int segment=Math.min(3,(int)scaled);
                float from=segment==0?0:points[segment-1];
                float to=segment==3?100:points[segment];
                return (from+(to-from)*(scaled-segment))/100f;
            }
            double exponent=curve==1?.58:curve==2?2:curve==3?1:1.35;
            return (float)Math.pow(input,exponent);
        }
        JSONObject json() throws JSONException {
            return new JSONObject().put("dead",dead).put("gain",gain).put("curve",curve).put("points",new JSONArray().put(points[0]).put(points[1]).put(points[2]));
        }
        void read(JSONObject data) {
            if (data == null) return;
            dead = limit(data.optInt("dead",dead),0,45);
            gain = limit(data.optInt("gain",gain),25,220);
            curve = limit(data.optInt("curve",curve),0,CURVES.length-1);
            JSONArray saved=data.optJSONArray("points");
            if(saved!=null) {
                int prev=0;
                for(int i=0;i<3;i++) points[i]=prev=limit(saved.optInt(i,points[i]),prev,100);
            } else if(data.has("mid")) {
                int mid=limit(data.optInt("mid",50),10,90);
                double exponent=Math.log(mid/100.0)/Math.log(.5);
                for(int i=0;i<3;i++) points[i]=(int)Math.round(100*Math.pow((i+1)/4.0,exponent));
            }
        }
    }
    static final class Stick {
        final StickCalibration calibration=new StickCalibration();
        static final int SCALE=10000, STEP=100, MIN=3, MAX=7;
        final boolean gamepad;
        final int defaultCurve;
        final int[] x=new int[MAX], y=new int[MAX];
        int count=5, curve;
        LegacyCurve source;

        Stick(boolean s) {
            gamepad=!s;
            defaultCurve=s?0:3;
            preset(defaultCurve);
        }
        void preset(int kind) {
            source=new LegacyCurve();
            source.dead=gamepad?10:15;
            source.gain=gamepad?100:50;
            source.curve=curve=kind;
            sampleSource();
        }
        void migrateGamepadRange() {
            if(source!=null && curve!=4) {
                preset(curve);
                return;
            }
            int maximum=y[count-1];
            if(maximum==SCALE)return;
            startCustom();
            if(maximum>0)for(int i=0;i<count;i++)
                y[i]=snap(Math.round((float)y[i]*SCALE/maximum));
            y[count-1]=SCALE;
        }
        private boolean isPreviousFactoryPreset() {
            if(source.curve==4)return false;
            if(source.gain==40 && source.dead==15)return true;
            return source.gain==100 &&
                (source.dead==10 || source.dead==(defaultCurve==3?8:14));
        }
        private void sampleSource() {
            int start=source.dead*100;
            x[0]=0;y[0]=0;
            for(int i=1;i<count;i++) {
                x[i]=start==0?snap(SCALE*i/(count-1)):snap(start+(SCALE-start)*(i-1)/(count-2));
                y[i]=snap(Math.round(SCALE*source.output(pointInput(i))));
            }
        }
        private static int snap(int value) {return Math.round(value/(float)STEP)*STEP;}
        float pointInput(int i) {return x[i]/(float)SCALE;}
        float pointOutput(int i) {return y[i]/(float)SCALE;}

        // Include the origin and the constant tail when an endpoint is inset.
        int firstKnot() {return x[0]>0?-1:0;}
        int lastKnot() {return x[count-1]<SCALE?count:count-1;}
        int knotX(int i) {return i<0?0:i==count?SCALE:x[i];}
        int knotY(int i) {return i<0?0:i==count?y[count-1]:y[i];}
        double secant(int i) {return (knotY(i+1)-knotY(i))/(double)(knotX(i+1)-knotX(i));}
        double tangent(int i) {
            if(i==firstKnot())return secant(i);
            if(i==lastKnot())return secant(i-1);
            double left=secant(i-1),right=secant(i);
            if(left==0 || right==0)return 0;
            double a=knotX(i)-knotX(i-1),b=knotX(i+1)-knotX(i);
            return 3*(a+b)/((2*b+a)/left+(b+2*a)/right);
        }
        float output(float input) {
            if(input<=0)return 0;
            double at=Math.min(1,input)*SCALE;
            for(int i=firstKnot();i<lastKnot();i++) {
                if(at>knotX(i+1))continue;
                double width=knotX(i+1)-knotX(i);
                double t=(at-knotX(i))/width, u=1-t;
                double value=u*u*(1+2*t)*knotY(i)+t*t*(3-2*t)*knotY(i+1)
                    +width*t*u*(u*tangent(i)-t*tangent(i+1));
                return (float)(value/SCALE);
            }
            return pointOutput(count-1);
        }
        int deadEdge() {
            int edge=0;
            for(int i=0;i<count;i++) {
                if(y[i]>0)return edge;
                edge=x[i];
            }
            return SCALE;
        }
        void startCustom() {source=null;curve=4;}
        boolean move(int i,int u,int v) {
            boolean endpoint=i==0 || i==count-1;
            u=endpoint?x[i]:limit(snap(u),x[i-1]+STEP,x[i+1]-STEP);
            v=limit(snap(v),i==0?0:y[i-1],i==count-1?SCALE:y[i+1]);
            if(u==x[i] && v==y[i])return false;
            startCustom();
            x[i]=u;y[i]=v;
            return true;
        }
        void resize(int next) {
            if(next<MIN || next>MAX || next==count)return;
            if(source!=null && source.curve!=4) {
                count=next;sampleSource();
                return;
            }
            startCustom();
            while(count<next) {
                int at=1;
                for(int i=2;i<count;i++)if(x[i]-x[i-1]>x[at]-x[at-1])at=i;
                int u=snap((x[at-1]+x[at])/2);
                int v=snap(Math.round(SCALE*output(u/(float)SCALE)));
                System.arraycopy(x,at,x,at+1,count-at);
                System.arraycopy(y,at,y,at+1,count-at);
                x[at]=u;y[at]=v;count++;
            }
            while(count>next) {
                int at=1;
                double smallest=Double.POSITIVE_INFINITY;
                for(int i=1;i<count-1;i++) {
                    double t=(double)(x[i]-x[i-1])/(x[i+1]-x[i-1]);
                    double error=Math.abs(y[i]-(y[i-1]+t*(y[i+1]-y[i-1])));
                    if(error<smallest) {smallest=error;at=i;}
                }
                System.arraycopy(x,at+1,x,at,count-at-1);
                System.arraycopy(y,at+1,y,at,count-at-1);
                count--;
            }
        }
        // Older slots sometimes kept the domain ends as implicit spline knots.
        private void anchorEndpoints() {
            if(x[0]==0 && x[count-1]==SCALE)return;
            int[] xs=new int[MAX+2],ys=new int[MAX+2];
            int n=x[0]>0?1:0;
            System.arraycopy(x,0,xs,n,count);System.arraycopy(y,0,ys,n,count);
            n+=count;
            if(xs[n-1]<SCALE){xs[n]=SCALE;ys[n]=ys[n-1];n++;}
            while(n>MAX) {
                int remove=1;double best=Double.POSITIVE_INFINITY;
                for(int i=1;i<n-1;i++) {
                    double f=(double)(xs[i]-xs[i-1])/(xs[i+1]-xs[i-1]);
                    double error=Math.abs(ys[i]-ys[i-1]-f*(ys[i+1]-ys[i-1]));
                    if(error<best){best=error;remove=i;}
                }
                System.arraycopy(xs,remove+1,xs,remove,n-remove-1);
                System.arraycopy(ys,remove+1,ys,remove,n-remove-1);n--;
            }
            count=n;System.arraycopy(xs,0,x,0,n);System.arraycopy(ys,0,y,0,n);
        }
        JSONObject json() throws JSONException {
            JSONObject data=new JSONObject().put("curve",curve).put("calibration",calibration.json());
            JSONArray xs=new JSONArray(),ys=new JSONArray();
            for(int i=0;i<count;i++) {xs.put(x[i]);ys.put(y[i]);}
            data.put("x",xs).put("y",ys);
            if(source!=null)data.put("source",source.json());
            return data;
        }
        void read(JSONObject data) {
            if(data==null)return;
            calibration.read(data.optJSONArray("calibration"));
            JSONArray xs=data.optJSONArray("x"),ys=data.optJSONArray("y");
            if(xs==null && ys==null) {
                source=new LegacyCurve();
                source.dead=defaultCurve==3?8:14;
                source.curve=defaultCurve;
                source.read(data);
                curve=source.curve;
                if(isPreviousFactoryPreset()) {count=5;preset(curve);return;}
                count=curve==4?5:3;
                int start=source.dead*100;
                for(int i=0;i<count;i++) {
                    x[i]=snap(start+(SCALE-start)*i/(count-1));
                    y[i]=snap(Math.round(SCALE*source.output(pointInput(i))));
                }
                anchorEndpoints();
                return;
            }
            if(xs==null || ys==null || xs.length()!=ys.length() || xs.length()<MIN || xs.length()>MAX)return;
            int n=xs.length(),prevX=-1,prevY=0;
            for(int i=0;i<n;i++) {
                int u=xs.optInt(i,-1),v=ys.optInt(i,-1);
                if(u<=prevX || u>SCALE || v<prevY || v>SCALE)return;
                prevX=u;prevY=v;
            }
            count=n;
            for(int i=0;i<count;i++){
                int lowest=i==0?0:x[i-1]+STEP;
                x[i]=limit(snap(xs.optInt(i)),lowest,SCALE-(count-1-i)*STEP);
                y[i]=snap(ys.optInt(i));
            }
            anchorEndpoints();
            JSONObject original=data.optJSONObject("source");
            source=null;curve=4;
            if(original!=null) {
                source=new LegacyCurve();source.read(original);curve=source.curve;
                if(isPreviousFactoryPreset())preset(curve);
            }
        }
    }
    static final class Profile {
        final Stick left, right;
        final boolean airMouse;
        final int slot;
        boolean localFavorite;
        boolean clickHold=true;
        double gyroMax=100, stickSpeed=100, wheelSpeed=100;
        // -100 doubles horizontal, +100 doubles vertical.
        int gyroBalance;
        int holdMs=2000;

        int horizontalGyroPercent() { return 100+Math.max(0,-gyroBalance); }
        int verticalGyroPercent() { return 100+Math.max(0,gyroBalance); }
        final int[] map=new int[BUTTONS.length];
        Profile(boolean s) {this(s,0);}
        Profile(boolean s,int slot) {
            airMouse=s;
            this.slot=slot;
            left=new Stick(s);right=new Stick(s);
            if (s) {
                int[] defaults={8,7,3,2,10,11,12,13,1,1,SPEED_DOWN,SPEED_UP,14,0,NEXT_SLOT,PREVIOUS_SLOT,9,9};
                System.arraycopy(defaults,0,map,0,defaults.length);

            }
            if(s){map[LB]=VOLUME_DOWN;map[RB]=VOLUME_UP;}
            else {map[STAR]=CYCLE_SLOT;enforceLayout();}
        }
        private void enforceLayout(){
            if(!fixedLayout(airMouse,slot))return;
            for(int i=0;i<map.length;i++)if(i!=STAR)
                map[i]=i<4?20+faceIndex(slot,i):0;
        }
        String encode() {
            try {
                JSONArray keys=new JSONArray();
                for (int action:map) keys.put(action);
                return new JSONObject().put("schema",1).put("gamepadDefaultsV2",!airMouse).put("volumeBindings",true).put("rightVolumeUp",true).put("triggerSlots",true).put("menuSlotButtons",true).put("speedButtons",true).put("left",left.json())
                    .put("right",right.json()).put("gyroMax",gyroMax).put("gyroBalance",gyroBalance).put("stickSpeed",stickSpeed).put("wheelSpeed",wheelSpeed)
                    .put("clickHold",clickHold).put("hold",holdMs).put("buttons",keys).toString();
            } catch(JSONException e) { throw new AssertionError(e); }
        }
        void decode(String json) {
            if (json==null) return;
            try {
                JSONObject data=new JSONObject(json);
                if (data.optInt("schema")!=1) return;
                left.read(data.optJSONObject("left")); right.read(data.optJSONObject("right"));
                localFavorite=data.optBoolean("gamepadDefaultsV2");
                if(!airMouse && !localFavorite){
                    left.migrateGamepadRange();
                    right.migrateGamepadRange();
                }
                int oldX=limit(data.optInt("gx",100),0,MAX_SPEED_PERCENT);
                int oldY=limit(data.optInt("gy",100),0,MAX_SPEED_PERCENT);
                gyroMax=Math.max(0,Math.min(MAX_SPEED_PERCENT,data.optDouble("gyroMax",(oldX+oldY+1)/2)));
                gyroBalance=limit(data.optInt("gyroBalance",0),-100,100);
                wheelSpeed=Math.max(5,Math.min(300,data.optDouble("wheelSpeed",100)));
                stickSpeed=Math.max(5,Math.min(MAX_STICK_PERCENT,data.optDouble("stickSpeed",100)));
                clickHold=data.optBoolean("clickHold",true);
                holdMs=limit(data.optInt("hold",holdMs),0,5000);
                JSONArray keys=data.optJSONArray("buttons");
                if (keys!=null) for(int i=0;i<map.length;i++) {
                    int id=keys.optInt(i,map[i]);
                    map[i]=id<0 || id>=ACTIONS.length || id==OLD_GYRO_ZERO?0:id;
                }
                if(!data.optBoolean("volumeBindings") && keys!=null) {
                    int oldDefault=airMouse?1:0;
                    if(keys.optInt(LB,-1)==oldDefault)map[LB]=VOLUME_DOWN;
                    if(keys.optInt(RB,-1)==oldDefault)map[RB]=VOLUME_UP;
                } else if(keys!=null && !data.optBoolean("rightVolumeUp")) {
                    if(map[LB]==VOLUME_UP)map[LB]=VOLUME_DOWN;
                    if(map[RB]==VOLUME_DOWN)map[RB]=VOLUME_UP;
                }
                if(airMouse && !data.optBoolean("triggerSlots") && keys!=null) {
                    if(keys.optInt(LT,-1)==1)map[LT]=PREVIOUS_SLOT;
                    if(keys.optInt(RT,-1)==1)map[RT]=NEXT_SLOT;
                }
                if(airMouse && !data.optBoolean("menuSlotButtons") && keys!=null) {
                    migrateNavigation(LT,MINUS,PREVIOUS_SLOT);
                    migrateNavigation(RT,PLUS,NEXT_SLOT);
                }
                if(airMouse && keys!=null && !data.optBoolean("speedButtons")) {
                    if(map[LT]==1)map[LT]=SPEED_DOWN;
                    if(map[RT]==1)map[RT]=SPEED_UP;
                }
                if(!airMouse && !localFavorite && map[STAR]==0)map[STAR]=CYCLE_SLOT;
            } catch(JSONException ignored) { }
            enforceLayout();
            if(!airMouse)for(int i=0;i<map.length;i++)
                if(i!=STAR && !gamepadBinding(map[i]))map[i]=0;
        }
        private void migrateNavigation(int oldButton,int newButton,int action) {
            if(map[oldButton]==action && (map[newButton]==1 || map[newButton]==action)) {
                map[newButton]=action;
                map[oldButton]=1;
            }
        }
    }
    static int limit(int n,int lo,int hi) { return Math.min(hi,Math.max(lo,n)); }
    final SharedPreferences preferences;
    private final android.content.Context context;
    Profiles(android.content.Context owner) {
        context=owner.getApplicationContext();
        preferences=context.getSharedPreferences("profiles.v1",android.content.Context.MODE_PRIVATE);
    }
    private Profile stored(boolean s,int slot) {
        Profile profile=defaults(s,slot);
        profile.decode(preferences.getString((s?"S":"D")+slot,null));
        return profile;
    }
    static Profile defaults(boolean s,int slot){
        if(slot==RAW)return rawDefaults(s);
        return new Profile(s,slot);
    }
    Profile load(boolean s,int slot) {
        Profile p=stored(s,slot);
        if(p!=null)applyCycleButtons(p);
        return p;
    }
    private void applyCycleButtons(Profile p) {
        int mask=preferences.getInt(p.airMouse?"cycleButtonsS":"cycleButtonsD",0);
        if(!p.airMouse)mask=p.localFavorite?0:mask & (1<<STAR);
        for(int i=0;i<p.map.length;i++)if((mask&(1<<i))!=0)p.map[i]=CYCLE_SLOT;
    }
    Profile navigation(boolean s) {
        return load(s,active(s));
    }
    static Profile rawDefaults(boolean s) {
        Profile p=new Profile(s,RAW);
        java.util.Arrays.fill(p.map,0);
        if(s){p.map[PLUS]=NEXT_SLOT;p.map[MINUS]=PREVIOUS_SLOT;}else p.map[STAR]=CYCLE_SLOT;
        return p;
    }
    void save(boolean s,int slot,Profile profile) {
        Profile local=defaults(s,slot);
        local.decode(profile.encode());
        if(!s){
            preferences.edit().putString("D"+slot,local.encode()).apply();
            return;
        }
        Profile before=stored(s,slot);
        int mask=0;
        for(int i=0;i<local.map.length;i++)if(local.map[i]==CYCLE_SLOT) {
            mask|=1<<i;
            local.map[i]=before.map[i];
        }
        preferences.edit().putInt(s?"cycleButtonsS":"cycleButtonsD",mask)
            .putString((s?"S":"D")+slot,local.encode()).apply();
    }

    static String speedText(double value) {
        return java.math.BigDecimal.valueOf(value).stripTrailingZeros().toPlainString();
    }
    static final class SpeedGesture {
        int lastBit, heldBit, target=-1;
        long lastPress, repeatAt;
        void reset() {lastBit=heldBit=0;target=-1;}
        boolean adjust(Profile p,int slot,int source,int held,int down,long now) {
            if(target!=slot) {reset();target=slot;}
            int bit=0,action=0;
            for(int i=0;i<SCAN_BUTTON.length;i++) if((held&(1<<i))!=0) {
                int binding=p.map[SCAN_BUTTON[i]];
                if(binding!=SPEED_UP && binding!=SPEED_DOWN)continue;
                if(bit!=0) {reset();return false;}
                bit=1<<i;action=binding;
            }
            if(bit==0) {heldBit=0;return false;}
            if(source<1 || source>3) return false;
            int step;
            if((down&bit)!=0) {
                step=lastBit==bit && now-lastPress<=200 ? 5 : 1;
                lastBit=heldBit=bit;lastPress=now;repeatAt=now+400;
            } else {
                if(heldBit!=bit || now<repeatAt)return false;
                step=5;repeatAt=now+120;
            }
            double previous=source==3?p.wheelSpeed:source==2?p.stickSpeed:p.gyroMax;
            long basis=Math.round(previous*100);
            long change=(basis*step+50)/100;
            int maximum=(source==3 ? 300 : source==2 ? MAX_STICK_PERCENT : MAX_SPEED_PERCENT)*100;
            double value=Math.max(500,Math.min(maximum,basis+(action==SPEED_UP?change:-change)))/100.;
            if(value==previous)return false;
            if(source==3)p.wheelSpeed=value;else if(source==2)p.stickSpeed=value;else p.gyroMax=value;
            return true;
        }
    }
    void saveSpeed(int slot,Profile p,int source) {
        Profile saved=stored(true,slot);
        if(source==3)saved.wheelSpeed=p.wheelSpeed;else if(source==2)saved.stickSpeed=p.stickSpeed;else saved.gyroMax=p.gyroMax;
        preferences.edit().putString("S"+slot,saved.encode()).apply();
        android.util.Log.i("Lite2Slots","Saved S"+(slot+1)+" "+(source==3?"wheel ":source==2?"stick ":"gyro ")+
            (source==3?saved.wheelSpeed:source==2?saved.stickSpeed:saved.gyroMax)+"%");
    }
    int active(boolean s) {
        int slot=preferences.getInt(s?"activeS":"activeD",0);
        return validSlot(s,slot)?slot:0;
    }
    void select(boolean s,int slot) {
        if(!validSlot(s,slot) || active(s)==slot)return;
        SharedPreferences.Editor edit=preferences.edit();
        edit.putInt(s?"activeS":"activeD",slot).apply();
        int language=limit(preferences.getInt("language",0),0,Words.COUNT-1);
        String label=slotLabel(language,slot);
        SlotOverlay.show(context,(s?"S  ·  ":"D  ·  ")+label);
        android.util.Log.i("Lite2Slots",(s?"S":"D")+" active slot "+(slot+1));
    }
}
