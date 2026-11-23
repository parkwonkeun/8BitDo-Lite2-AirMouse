package local.lite2.control;
import org.json.JSONObject;

public final class CurveCheck {
    static void expect(boolean ok,String message) {
        if(!ok)throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        for(boolean s:new boolean[]{true,false}) {
            Profiles.Profile p=new Profiles.Profile(s);
            for(Profiles.Stick stick:new Profiles.Stick[]{p.left,p.right}) {
                for(int kind=0;kind<4;kind++) {
                    stick.preset(kind);
                    expect(stick.count==5 && stick.deadEdge()==1500,"factory controls");
                    expect(stick.output(.15f)==0 && stick.output(.16f)>0 && stick.output(1f)==.50f,"preset dead zone");
                }
                stick.resize(4);stick.resize(5);
                expect(stick.count==5,"5 points");
                expect(!stick.move(0,2500,0) && stick.x[0]==0,"left X is fixed");
                expect(!stick.move(0,2500,600) && stick.y[1]==0,"flat neighbor blocks left endpoint");
                stick.move(1,stick.x[1],1200);
                expect(stick.move(0,2500,600) && stick.y[0]==600 && stick.y[1]==1200,"left Y editable");
                stick.move(0,2500,0);
                expect(stick.move(4,9000,9200) && Math.abs(stick.output(1)-.92f)<.001f,"right endpoint");
                stick.move(1,3000,0);
                expect(stick.deadEdge()==3000 && stick.output(.30f)==0 && stick.output(.31f)>0,"derived zero span");
                Profiles.Stick copy=new Profiles.Stick(s);
                copy.read(stick.json());
                expect(copy.count==5 && copy.deadEdge()==3000 && copy.y[4]==9200,"slot round trip");
                copy.resize(3);
                expect(copy.count==3 && copy.x[0]==0 && copy.x[2]==10000,"resizing keeps endpoints");
            }
        }
        for(int n=3;n<=7;n++)for(int i=0;i<n;i++)for(boolean high:new boolean[]{false,true}) {
            Profiles.Stick stick=new Profiles.Stick(true);
            stick.resize(n);
            int[] xs=stick.x.clone(),ys=stick.y.clone();
            int target=high?20000:-10000;
            stick.move(i,target,target);
            for(int j=0;j<n;j++)if(i!=j)
                expect(stick.x[j]==xs[j] && stick.y[j]==ys[j],"only selected handle moves");
            int x=i==0 || i==n-1?xs[i]:high?xs[i+1]-100:xs[i-1]+100;
            int y=high?(i==n-1?10000:ys[i+1]):(i==0?0:ys[i-1]);
            expect(stick.x[i]==x && stick.y[i]==y,"stop at input and output boundaries");
            expect(!stick.move(i,target,target),"repeated drag against boundary has no effect");
        }
        for(int kind=0;kind<4;kind++)for(int n=3;n<=7;n++) {
            Profiles.Stick stick=new Profiles.Stick(true);
            stick.preset(kind);stick.resize(n);
            for(int percent=0;percent<=100;percent++) {
                float y=stick.output(percent/100f);
                expect(percent<=15?y==0:y>0 && y<=.50f,"preset bounds at every point count");
            }
            JSONObject saved=stick.json();
            saved.getJSONObject("source").put("gain",100).put("dead",10);
            saved.getJSONArray("y").put(n-1,10000);
            stick.read(saved);
            expect(stick.count==n && stick.curve==kind && stick.deadEdge()==1500 && stick.output(1)==.50f,"upgrade saved factory preset");
            stick.read(new JSONObject().put("gain",100).put("dead",14).put("curve",kind));
            expect(stick.deadEdge()==1500 && stick.output(1)==.50f,"upgrade legacy factory preset");
        }
        Profiles.Profile gyroSlot=new Profiles.Profile(true);
        gyroSlot.decode(new JSONObject().put("schema",1).put("gx",100).put("gy",200).put("gyroAcceleration",true).toString());
        expect(gyroSlot.gyroMax==150,"legacy axis gains migrate to a single gain");
        for(int gain:new int[]{0,100,500}) {
            gyroSlot.gyroMax=gain;
            Profiles.Profile reloaded=new Profiles.Profile(true);
            reloaded.decode(gyroSlot.encode());
            expect(reloaded.gyroMax==gain,"saved linear gain");
        }
        Profiles.Stick migrated40=new Profiles.Stick(true);
        migrated40.preset(2);
        JSONObject factory40=migrated40.json();
        factory40.getJSONObject("source").put("gain",40);
        factory40.getJSONArray("y").put(4,4000);
        migrated40.read(factory40);
        expect(migrated40.deadEdge()==1500 && migrated40.output(1f)==.5f,"40 percent factory migration");
        Profiles.LegacyCurve old=new Profiles.LegacyCurve();
        old.read(new JSONObject("{\"dead\":45,\"gain\":32,\"curve\":4,\"points\":[18,99,100]}"));
        Profiles.Stick migrated=new Profiles.Stick(true);
        migrated.read(old.json());
        expect(migrated.count==6 && migrated.deadEdge()==4500,"preserve saved curve");
        for(int n=3;n<=7;n++){
            migrated.resize(n);
            expect(migrated.count==n,"all interior point counts");
            float previous=0;
            for(int i=0;i<n;i++) {
                expect(migrated.x[i]%100==0 && migrated.y[i]%100==0,"integer grid");
                expect(Math.abs(migrated.output(migrated.pointInput(i))-migrated.pointOutput(i))<.00001f,"curve passes through handle");
            }
            for(int i=0;i<=1000;i++) {
                float val=migrated.output(i/1000f);
                expect(val>=previous-.00001f && val<=1,"monotone bounded spline");
                previous=val;
            }
        }
        Profiles.Stick bent=new Profiles.Stick(true);
        bent.resize(3);bent.move(1,5500,1000);
        float left=bent.output(.55f)-bent.output(.5499f);
        float right=bent.output(.5501f)-bent.output(.55f);
        expect(Math.abs(left-right)<.000002,"continuous slope");
        expect(Math.abs(bent.output(.275f)-.05f)>.005,"curved between handles");
        Profiles.Stick packed=new Profiles.Stick(false);
        packed.read(new JSONObject("{\"x\":[9801,9802,9810],\"y\":[0,1151,1812]}"));
        packed.resize(7);
        for(int i=1;i<7;i++)expect(packed.x[i]>=packed.x[i-1]+100 && packed.x[i]<=10000,"crowded integer knots");
        Profiles.Profile triggers=new Profiles.Profile(true);
        expect(triggers.clickHold,"old/default profiles stabilize clicks");
        triggers.clickHold=false;
        Profiles.Profile restored=new Profiles.Profile(true);
        restored.decode(triggers.encode());
        expect(!restored.clickHold,"saved click stabilization off");
        JSONObject oldProfile=new JSONObject(triggers.encode());
        oldProfile.remove("clickHold");restored.decode(oldProfile.toString());
        expect(restored.clickHold,"legacy profile click stabilization defaults on");
        expect(triggers.map[Profiles.LT]==Profiles.PREVIOUS_SLOT && triggers.map[Profiles.RT]==Profiles.NEXT_SLOT,"S trigger defaults");
        for(int active=Profiles.RAW;active<3;active++) {
            Profiles.Profile p=active==Profiles.RAW?null:triggers;
            expect(Profiles.slotForPresses(p,active,1<<14)==(active==Profiles.RAW?2:active-1),"physical LT cycles back");
            expect(Profiles.slotForPresses(p,active,1<<15)==(active==2?Profiles.RAW:active+1),"physical RT cycles forward");
            expect(Profiles.slotForPresses(p,active,0xc000)==Profiles.NO_CHANGE && Profiles.slotForPresses(p,active,0)==Profiles.NO_CHANGE,"chord or held snapshot");
        }
        for(int bit=0;bit<18;bit++) {
            java.util.Arrays.fill(triggers.map,1);
            triggers.map[Profiles.SCAN_BUTTON[bit]]=Profiles.NEXT_SLOT;
            expect(Profiles.slotForPresses(triggers,0,1<<bit)==1,"assign each physical button");
        }
        triggers=new Profiles.Profile(true);
        JSONObject previous=new JSONObject(triggers.encode());
        previous.remove("triggerSlots");
        previous.getJSONArray("buttons").put(Profiles.LT,1).put(Profiles.RT,18);
        triggers.decode(previous.toString());
        expect(triggers.map[Profiles.LT]==Profiles.PREVIOUS_SLOT && triggers.map[Profiles.RT]==18,"migrate default and keep assignment");
        triggers.map[Profiles.LT]=1;
        triggers.decode(triggers.encode());
        expect(Profiles.targetSlot(2,triggers.map[Profiles.LT])==Profiles.NO_CHANGE,"saved unassigned trigger");
        Profiles.Profile gamepad=new Profiles.Profile(false);
        expect(gamepad.map[Profiles.LT]==0 && gamepad.map[Profiles.RT]==0,"D mapping unchanged");
        System.out.println("PASS: presets, interior counts 1..5, endpoint drag, snapped XY, cubic interpolation, monotonicity, saved shapes and migration");
    }
}
