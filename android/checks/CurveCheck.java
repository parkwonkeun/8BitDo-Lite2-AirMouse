package local.lite2.control;
import org.json.JSONObject;

public final class CurveCheck {
    static void expect(boolean ok,String message) {
        if(!ok)throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
        Profiles.Profile first=new Profiles.Profile(true),second=new Profiles.Profile(true);
        Profiles.Profile balanced=new Profiles.Profile(true);
        expect(balanced.horizontalGyroPercent()==100 && balanced.verticalGyroPercent()==100,"default gyro balance");
        balanced.gyroBalance=-100;
        expect(balanced.horizontalGyroPercent()==200 && balanced.verticalGyroPercent()==100,"left gyro balance");
        balanced.gyroBalance=100;
        Profiles.Profile loadedBalance=new Profiles.Profile(true);
        loadedBalance.decode(balanced.encode());
        expect(loadedBalance.horizontalGyroPercent()==100 && loadedBalance.verticalGyroPercent()==200,"right gyro balance survives save");
        loadedBalance.decode("{\"schema\":1}");
        expect(loadedBalance.horizontalGyroPercent()==100 && loadedBalance.verticalGyroPercent()==100,"old profile defaults to centered");
        StickCalibration.Capture rest=new StickCalibration.Capture(true);
        StickCalibration.Capture travel=new StickCalibration.Capture(false);
        for(int k=0;k<24;k++) {
            rest.sample(10+(k%2),-5,k*50);
            travel.sample(k%2==0?-88:99,k%2==0?108:-104,k*50);
        }
        expect(rest.ready(first.left.calibration,1150),"stable center");
        rest.apply(first.left.calibration);
        expect(travel.ready(first.left.calibration,1150),"all directions");
        travel.apply(first.left.calibration);
        Profiles.Profile savedCalibration=new Profiles.Profile(true);savedCalibration.decode(first.encode());
        expect(savedCalibration.left.calibration.normalized(0,11)==0 && savedCalibration.left.calibration.normalized(1,-5)==0,"restored center");
        expect(savedCalibration.left.calibration.normalized(0,99)==1 && savedCalibration.left.calibration.normalized(1,-104)==-1,"restored travel");
        expect(second.left.calibration.normalized(0,11)>0 && first.right.calibration.normalized(0,11)>0,"independent slot and stick");
        expect(!rest.ready(first.left.calibration,3000),"stale sample");
        StickCalibration.Capture partial=new StickCalibration.Capture(false);
        for(int k=0;k<30;k++)partial.sample(k%2==0?100:-110,0,k*50);
        expect(!partial.ready(first.left.calibration,1450),"incomplete travel");
        int[] order=Profiles.slots(false);
        for(int i=0;i<order.length;i++)
            expect(Profiles.targetSlot(false,order[i],Profiles.CYCLE_SLOT)==order[(i+1)%order.length],"D slot cycle");
        Profiles.Profile xbox=Profiles.defaults(false,Profiles.XBOX);
        Profiles.Profile nintendo=Profiles.defaults(false,Profiles.SWITCH);
        expect(xbox.map[0]==21 && nintendo.map[0]==20 && xbox.left.output(1)==1,"gamepad layouts and range");
        xbox.map[Profiles.STAR]=Profiles.CYCLE_SLOT;
        xbox.map[Profiles.PLUS]=Profiles.NEXT_SLOT;
        xbox.decode(xbox.encode());
        expect(xbox.map[Profiles.STAR]==Profiles.CYCLE_SLOT && xbox.map[Profiles.PLUS]==0,"only star navigates D slots");
        for(int slot:order){
            Profiles.Profile d=Profiles.defaults(false,slot);
            expect(d.map[Profiles.STAR]==Profiles.CYCLE_SLOT,"default favorite cycles all D slots");
            JSONObject old=new JSONObject(d.encode());
            old.remove("gamepadDefaultsV2");
            old.getJSONArray("buttons").put(0,30).put(9,23).put(Profiles.STAR,0);
            old.put("left",new Profiles.Stick(true).json());
            Profiles.Stick custom=new Profiles.Stick(true);
            custom.move(2,custom.x[2],1200);
            old.put("right",custom.json());
            d.decode(old.toString());
            expect(d.left.deadEdge()==1000 && d.left.output(1)==1,"migrate gamepad preset");
            expect(d.right.output(1)==1 && d.right.y[2]==2400 && d.right.deadEdge()==custom.deadEdge(),"normalize saved custom range");
            expect(d.map[Profiles.STAR]==Profiles.CYCLE_SLOT,"upgrade favorite default");
            if(Profiles.fixedLayout(false,slot)){
                int[] expected=slot==Profiles.XBOX?new int[]{21,20,23,22}:new int[]{20,21,22,23};
                for(int i=0;i<4;i++)expect(d.map[i]==expected[i],"locked face mapping");
                expect(d.map[9]==0,"locked bumper");
            }else expect(d.map[0]==30 && d.map[9]==23,"user slot assignments remain editable");
            d.map[Profiles.STAR]=0;
            d.right.move(d.right.count-1,10000,9000);
            d.decode(d.encode());
            expect(d.map[Profiles.STAR]==0 && Math.abs(d.right.output(1)-.9f)<.0001f,"explicit favorite and curve edits persist");
        }
        for(boolean s:new boolean[]{true,false}) {
            Profiles.Profile p=new Profiles.Profile(s);
            expect(p.map[Profiles.LB]==(s?Profiles.VOLUME_DOWN:0) && p.map[Profiles.RB]==(s?Profiles.VOLUME_UP:0),"volume defaults");
            int[] choices=Profiles.assignableActions(s);
            expect(java.util.Arrays.binarySearch(choices,Profiles.OLD_GYRO_ZERO)<0 &&
                java.util.Arrays.binarySearch(choices,Profiles.VOLUME_UP)>=0 &&
                java.util.Arrays.binarySearch(choices,Profiles.VOLUME_DOWN)>=0,"assignable volume controls");
            JSONObject oldBindings=new JSONObject(p.encode());
            oldBindings.remove("volumeBindings");
            oldBindings.getJSONArray("buttons").put(Profiles.LB,s?1:0).put(Profiles.RB,17).put(13,15);
            p.decode(oldBindings.toString());
            expect(p.map[Profiles.LB]==(s?Profiles.VOLUME_DOWN:0) && p.map[Profiles.RB]==(s?17:0) && p.map[13]==0,"migrate defaults and retain custom bindings");
            JSONObject prior=new JSONObject(p.encode());
            prior.remove("rightVolumeUp");
            prior.getJSONArray("buttons").put(Profiles.LB,Profiles.VOLUME_UP).put(Profiles.RB,Profiles.VOLUME_DOWN);
            p.decode(prior.toString());
            expect(p.map[Profiles.LB]==(s?Profiles.VOLUME_DOWN:0) && p.map[Profiles.RB]==(s?Profiles.VOLUME_UP:0),"upgrade old volume defaults");
            p.map[Profiles.LB]=1;
            p.decode(p.encode());
            expect(p.map[Profiles.LB]==(s?1:0),"shoulder binding matches mode");
            for(Profiles.Stick stick:new Profiles.Stick[]{p.left,p.right}) {
                for(int kind=0;kind<4;kind++) {
                    stick.preset(kind);
                    expect(stick.count==5 && stick.deadEdge()==(s?1500:1000),"factory controls");
                    expect(stick.output(s?.15f:.10f)==0 && stick.output(s?.16f:.11f)>0 && stick.output(1f)==(s?.50f:1f),"preset dead zone");
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
        for(int gain:new int[]{0,100,500,995,1000}) {
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
        expect(triggers.map[Profiles.MINUS]==Profiles.PREVIOUS_SLOT && triggers.map[Profiles.PLUS]==Profiles.NEXT_SLOT && triggers.map[Profiles.LT]==Profiles.SPEED_DOWN && triggers.map[Profiles.RT]==Profiles.SPEED_UP,"S navigation defaults");
        for(int active=Profiles.RAW;active<3;active++) {
            Profiles.Profile p=active==Profiles.RAW?null:triggers;
            expect(Profiles.slotForPresses(p,active,1<<3)==(active==Profiles.RAW?2:active-1),"physical minus cycles back");
            expect(Profiles.slotForPresses(p,active,1)==(active==2?Profiles.RAW:active+1),"physical plus cycles forward");
            expect(Profiles.slotForPresses(p,active,0xc000)==Profiles.NO_CHANGE && Profiles.slotForPresses(p,active,9)==Profiles.NO_CHANGE && Profiles.slotForPresses(p,active,0)==Profiles.NO_CHANGE,"chord or held snapshot");
        }
        for(int a=Profiles.RAW;a<3;a++)expect(Profiles.targetSlot(a,Profiles.CYCLE_SLOT)==(a==2?Profiles.RAW:a+1),"cycle including RAW");
        for(int button:new int[]{1,8}) {
            Profiles.SlotPress press=new Profiles.SlotPress();
            Profiles.Profile raw=Profiles.rawDefaults(true);
            int slot=button==1?2:0;
            expect(press.update(triggers,slot,button,button)==Profiles.NO_CHANGE,"wait for release");
            for(int i=0;i<20;i++)expect(press.update(triggers,slot,button,0)==Profiles.NO_CHANGE,"held repeat");
            expect(press.update(triggers,slot,0,0)==Profiles.RAW,"enter RAW once");
            expect(press.update(raw,Profiles.RAW,0,0)==Profiles.NO_CHANGE,"RAW remains selected");
            expect(press.update(raw,Profiles.RAW,button,button)==Profiles.NO_CHANGE,"new press");
            expect(press.update(raw,Profiles.RAW,0,0)==(button==1?0:2),"leave RAW once");
            raw.map[Profiles.PLUS]=18;
            expect(Profiles.slotForPresses(raw,Profiles.RAW,1)==1,"direct RAW binding");
        }
        PadAttitude pose=new PadAttitude();
        pose.sample(0,0,0,0,0,0,0);
        expect(!pose.ready && Math.abs(2*pose.w*pose.x-1)<1e-8,"upright while waiting for valid gravity");
        pose.sample(1,0,0,4096,0,0,0);
        for(int i=1;i<=50;i++)pose.sample(i*20,0,0,4096,0,0,1283);
        expect(Math.abs(pose.z-Math.sqrt(.5))<.01 && Math.abs(pose.x)+Math.abs(pose.y)<.001,"ninety degree yaw");
        double before=pose.z;
        pose.sample(1000,0,0,4096,1400,500,0);
        expect(pose.z==before,"duplicate firmware timestamp");
        pose.reset();pose.sample(0xfffffffaL,0,-4096,0,0,0,0);
        pose.sample(14,0,-4096,0,0,0,0);
        expect(Math.abs(2*(pose.x*pose.z-pose.w*pose.y)-1)<.001,"sideways gravity and timestamp wrap");
        pose.reset();pose.sample(0,4096,0,0,0,0,0);
        expect(Math.abs(2*(pose.y*pose.z+pose.w*pose.x)-1)<1e-8,"upright from actual gravity");
        pose.sample(700,0,0,-4096,0,0,0);
        expect(Math.abs(1-2*(pose.x*pose.x+pose.y*pose.y)+1)<1e-8,"recover upside down after a sample gap");
        pose.reset();pose.sample(0,0,0,4096,0,0,0);
        pose.sample(20,0,0,0,0,0,1283);
        expect(pose.z>.01,"gyro keeps responding during acceleration dropout");
        pose.sample(800,0,0,4096,0,0,0);
        expect(pose.z>.01,"a sample gap preserves relative heading");
        // Test heading reset on a flat, upright, tilted and edge-on pad.
        for(int[] gravity:new int[][]{{0,0,4096},{4096,0,0},{2048,-2048,2896},{0,-4096,0}}){
            pose.reset();
            pose.sample(0,gravity[0],gravity[1],gravity[2],0,0,0);
            double qw=pose.w,qx=pose.x,qy=pose.y,qz=pose.z;
            double c=Math.cos(.7),s=Math.sin(.7);
            pose.w=c*qw-s*qz;pose.x=c*qx-s*qy;pose.y=c*qy+s*qx;pose.z=c*qz+s*qw;
            pose.zeroHeading();pose.zeroHeading();
            expect(Math.abs(Math.abs(qw*pose.w+qx*pose.x+qy*pose.y+qz*pose.z)-1)<1e-8,"zero heading preserves tilt and is repeatable");
        }
        pose.reset();pose.zeroHeading();
        expect(!pose.ready,"zero does not manufacture a pose");
        pose.sample(0,0,0,4096,0,0,0);pose.sample(100,0,0,4096,0,0,1283);
        pose.zeroHeading();pose.sample(200,0,0,4096,0,0,1283);
        expect(Math.abs(pose.z-Math.sin(Math.PI/40))<.002,"gyro continues from zero heading");
        Profiles.SpeedGesture gesture=new Profiles.SpeedGesture();
        triggers.gyroMax=triggers.stickSpeed=500;
        expect(gesture.adjust(triggers,1,1,32768,32768,1000) && triggers.gyroMax==505,"single relative press");
        gesture.adjust(triggers,1,1,0,0,1100);
        expect(gesture.adjust(triggers,1,1,32768,32768,1200) && triggers.gyroMax==530.25,"fast second press");
        expect(!gesture.adjust(triggers,1,1,32768,0,1599),"hold delay");
        expect(gesture.adjust(triggers,1,1,32768,0,1600) && triggers.gyroMax==556.76,"first repeat");
        expect(gesture.adjust(triggers,1,1,32768,0,1720) && triggers.gyroMax==584.60,"second repeat");
        gesture.adjust(triggers,1,1,0,0,1750);
        expect(!gesture.adjust(triggers,1,1,0,0,2000),"release");
        expect(gesture.adjust(triggers,1,2,16384,16384,2050) && triggers.stickSpeed==495,"stick decrease");
        restored.decode(triggers.encode());
        expect(restored.gyroMax==584.60 && restored.stickSpeed==495,"fractional persistence");
        Profiles.Profile scrolling=new Profiles.Profile(true);
        Profiles.SpeedGesture scrollGesture=new Profiles.SpeedGesture();
        expect(scrollGesture.adjust(scrolling,1,3,16384,16384,1000),"scroll decrease");
        expect(scrolling.gyroMax==100 && scrolling.stickSpeed==100 && scrolling.wheelSpeed==99,"isolated wheel target");
        Profiles.Profile restoredScroll=new Profiles.Profile(true);
        restoredScroll.decode(scrolling.encode());
        expect(restoredScroll.wheelSpeed==99,"slot wheel speed restored");
        Profiles.Profile fastStick=new Profiles.Profile(true);
        fastStick.stickSpeed=1999;
        Profiles.SpeedGesture fastGesture=new Profiles.SpeedGesture();
        expect(fastGesture.adjust(fastStick,1,2,32768,32768,1000) && fastStick.stickSpeed==2000,"stick caps at 2000");
        Profiles.Profile savedFastStick=new Profiles.Profile(true);
        savedFastStick.decode(fastStick.encode());
        expect(savedFastStick.stickSpeed==2000 && savedFastStick.gyroMax==100,"extended stick speed reload");
        for(int bit=0;bit<18;bit++) {
            java.util.Arrays.fill(triggers.map,1);
            triggers.map[Profiles.SCAN_BUTTON[bit]]=Profiles.NEXT_SLOT;
            expect(Profiles.slotForPresses(triggers,0,1<<bit)==1,"assign each physical button");
        }
        triggers=new Profiles.Profile(true);
        JSONObject previous=new JSONObject(triggers.encode());
        previous.remove("speedButtons"); previous.remove("triggerSlots"); previous.remove("menuSlotButtons");
        previous.getJSONArray("buttons").put(Profiles.PLUS,1).put(Profiles.MINUS,1);
        previous.getJSONArray("buttons").put(Profiles.LT,1).put(Profiles.RT,18);
        triggers.decode(previous.toString());
        expect(triggers.map[Profiles.MINUS]==Profiles.PREVIOUS_SLOT && triggers.map[Profiles.LT]==Profiles.SPEED_DOWN && triggers.map[Profiles.RT]==18,"migrate default and keep assignment");
        triggers.map[Profiles.LT]=1;
        triggers.decode(triggers.encode());
        expect(Profiles.targetSlot(2,triggers.map[Profiles.LT])==Profiles.NO_CHANGE,"saved unassigned trigger");
        Profiles.Profile gamepad=new Profiles.Profile(false);
        expect(gamepad.map[Profiles.LT]==0 && gamepad.map[Profiles.RT]==0,"D mapping unchanged");
        System.out.println("PASS: presets, interior counts 1..5, endpoint drag, snapped XY, cubic interpolation, monotonicity, saved shapes and migration");
    }
}
