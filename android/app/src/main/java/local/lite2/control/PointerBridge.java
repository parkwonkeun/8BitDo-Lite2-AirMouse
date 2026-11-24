package local.lite2.control;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;

final class PointerBridge implements AutoCloseable {
    private static PointerBridge instance;
    interface ButtonListener { void changed(int held,int down,int up); }
    private final android.os.Handler main=new android.os.Handler(android.os.Looper.getMainLooper());
    private final Profiles store;
    interface StickListener { void changed(int x,int y,int rx,int ry); }
    private StickListener sticks;
    interface GamepadListener { void changed(int[] values,int down,int up); }
    GamepadListener gamepad;
    interface WheelListener { void changed(int direction); }
    WheelListener wheel;
    private int previousGamepad=-1;
    interface GyroListener { void changed(int dx,int dy); }
    private volatile GyroListener gyro;
    private boolean supportsGyro;
    void monitorGyro(GyroListener listener) { gyro=listener; }
    static final char DIRECT='?', MONITOR='!', REMAP='\n';
    final char route;
    private ButtonListener buttons;
    volatile boolean hasButtonReports;
    private int previousButtons=-1;
    private final String token;
    private volatile Runnable changed;
    private volatile String profileLine;
    private volatile boolean running=true;
    volatile boolean connected;
    private volatile Socket socket;
    private final Thread thread;

    static synchronized PointerBridge attach(String token, Runnable changed, ButtonListener buttons, StickListener sticks, Profiles store, Profiles.Profile profile,char route) {
        if(instance==null || !instance.token.equals(token) || instance.route!=route) {
            if(instance!=null)instance.close();
            instance=new PointerBridge(token,store,profile,route);
        }
        instance.changed=changed;instance.buttons=buttons;instance.sticks=sticks;
        return instance;
    }
    static synchronized void ensureBackground(String token,Profiles store) {
        if(instance==null && token!=null)background(token,store);
    }
    private static void background(String token,Profiles store) {
        Profiles.Profile profile=store.load(true,store.active(true));
        PointerBridge bridge=attach(token,null,null,null,store,profile,profile==null?DIRECT:REMAP);
        bridge.set(profile);
    }
    static synchronized void release() {
        if(instance!=null) {
            instance.close();
            instance=null;
        }
    }
    boolean usesToken(String value) {return token.equals(value);}
    void detach(Runnable listener) {if(changed==listener){changed=null;buttons=null;sticks=null;gyro=null;gamepad=null;wheel=null;}}
    private PointerBridge(String token,Profiles store,Profiles.Profile profile,char route) {
        this.token=token;this.store=store;this.route=route;
        set(profile);
        thread=new Thread(this::run,"lite2-pointer-bridge");
        thread.start();
    }
    void set(Profiles.Profile profile) {
        if(profile==null || route==DIRECT){profileLine=route==MONITOR?"P RAW":"H";return;}
        StringBuilder line=new StringBuilder("F ").append(profile.gyroMax).append(' ').append(profile.gyroMax);
        appendCurve(line,profile.left);
        appendCurve(line,profile.right);
        line.append(profile.clickHold?" 1":" 0");
        for(Profiles.Stick stick:new Profiles.Stick[]{profile.left,profile.right})
            for(int[] axis:stick.calibration.axis)for(int value:axis)line.append(' ').append(value);
        profileLine=route==MONITOR?"P "+line:line.toString();
    }
    void sampleStick(boolean left){profileLine=left?"K 1":"K 2";}
    // Send the same Hermite knots and slopes used to draw the graph.
    private static void appendCurve(StringBuilder line,Profiles.Stick stick) {
        int first=stick.firstKnot(),last=stick.lastKnot();
        line.append(' ').append(last-first+1);
        for(int i=first;i<=last;i++)
            line.append(' ').append(stick.knotX(i)/Profiles.Stick.STEP)
                .append(' ').append(stick.knotY(i)/Profiles.Stick.STEP)
                .append(' ').append(stick.tangent(i));
    }
    private void state(boolean value) {
        if(connected==value)return;
        connected=value;
        Runnable callback=changed;
        if(callback!=null)callback.run();
    }
    private boolean inputEvent(String line) {
        if(line!=null && line.startsWith("D ")) {
            String[] fields=line.substring(2).split(" ");
            if(fields.length!=7)return false;
            int[] values=new int[7];
            try {for(int i=0;i<7;i++)values[i]=Integer.parseInt(fields[i]);}
            catch(NumberFormatException invalid){return false;}
            int held=values[0],old=previousGamepad;
            previousGamepad=held;
            int down=old<0?0:held & ~old, up=old<0?0:old & ~held;
            main.post(()->{
                if(!running)return;
                if(gamepad!=null)gamepad.changed(values,down,up);
                else if(route!=MONITOR && down!=0)backgroundSlot(false,down);
            });
            return true;
        }
        if(line!=null && line.startsWith("T ")) {
            String[] axis=line.substring(2).split(" ");
            if(axis.length!=4)return false;
            int[] value=new int[4];
            try {for(int i=0;i<4;i++)value[i]=Integer.parseInt(axis[i]);}
            catch(NumberFormatException invalid){return false;}
            main.post(()->{if(running && sticks!=null)sticks.changed(value[0],value[1],value[2],value[3]);});
            return true;
        }
        if("W 1".equals(line) || "W -1".equals(line)) {
            int direction=line.charAt(2)=='-'?-1:1;
            main.post(()->{if(running && wheel!=null)wheel.changed(direction);});
            return true;
        }
        if("G CAPS3".equals(line)){supportsGyro=true;return true;}
        if("G CAPS2".equals(line))return true;
        if(line!=null && line.startsWith("G ")) {
            if(!supportsGyro || gyro==null)return true;
            String[] axes=line.substring(2).split(" ");
            if(axes.length!=2)return false;
            try {
                final int dx=Integer.parseInt(axes[0]),dy=Integer.parseInt(axes[1]);
                final GyroListener target=gyro;
                main.post(()->{if(running && target!=null && target==gyro)target.changed(dx,dy);});
            } catch(NumberFormatException invalid){return false;}
            return true;
        }
        if(line==null || !line.startsWith("B "))return false;
        int mask;
        try {mask=Integer.parseInt(line.substring(2));}catch(NumberFormatException bad){return false;}
        if(mask<0 || mask>=1<<18)return false;
        int old=previousButtons;
        previousButtons=mask;hasButtonReports=true;
        int down=old<0?0:mask & ~old;
        int up=old<0?0:old & ~mask;
        main.post(()->{
            if(!running)return;
            if(buttons!=null) {buttons.changed(mask,down,up);return;}
            if(route==MONITOR)return;
            if(down!=0)backgroundSlot(true,down);
        });
        return true;
    }
    private void backgroundSlot(boolean s,int down) {
        int active=store.active(s);
        Profiles.Profile current=store.navigation(s);
        store.applyVolumePresses(current,active,down);
        int next=Profiles.slotForPresses(current,active,down);
        if(next==Profiles.NO_CHANGE || next==active)return;
        store.select(s,next);
        if(s)background(token,store);
    }
    private void run() {
        while(running) {
            try(Socket connection=new Socket()) {
                socket=connection;
                connection.connect(new InetSocketAddress(InetAddress.getByName("127.0.0.1"),39728),1200);
                connection.setSoTimeout(2500);
                connection.setTcpNoDelay(true);
                BufferedReader in=new BufferedReader(new InputStreamReader(connection.getInputStream(),StandardCharsets.US_ASCII));
                BufferedWriter out=new BufferedWriter(new OutputStreamWriter(connection.getOutputStream(),StandardCharsets.US_ASCII));
                out.write(token);out.write(route);out.flush();
                if(!"OK".equals(in.readLine()))throw new IOException("No physical input");
                supportsGyro=false;
                boolean subscribed=false;
                String previous=null;
                long last=0;
                while(running) {
                    long now=android.os.SystemClock.uptimeMillis();
                    while(in.ready())if(!inputEvent(in.readLine()))throw new IOException("Invalid event");
                    String line=route==MONITOR && !supportsGyro?"H":profileLine;
                    boolean requested=gyro!=null;
                    if(supportsGyro && requested!=subscribed) {
                        out.write(requested?"M 1\n":"M 0\n");out.flush();
                        String reply;
                        do {reply=in.readLine();} while(inputEvent(reply));
                        if(!"OK".equals(reply))break;
                        subscribed=requested;
                    }
                    if(!line.equals(previous) || now-last>=450) {
                        out.write(line.equals(previous)?"H":line);out.newLine();out.flush();
                        String reply;
                        do {reply=in.readLine();} while(inputEvent(reply));
                        if(!"OK".equals(reply))break;
                        previous=line;last=now;state(true);
                    }
                    Thread.sleep(16);
                }
            } catch(IOException disconnected) {
                // Reopen after Bluetooth or app lifecycle changes.
            } catch(InterruptedException stopped) {break;}
            finally {socket=null;hasButtonReports=false;previousButtons=previousGamepad=-1;state(false);}
            try {Thread.sleep(750);} catch(InterruptedException stopped) {break;}
        }
    }
    @Override public void close() {
        running=false;
        Socket active=socket;
        if(active!=null)try {active.close();}catch(IOException ignored){}
        thread.interrupt();
    }
}
