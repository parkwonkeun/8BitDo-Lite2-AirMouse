package local.lite2.control;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.os.Looper;
import android.os.Process;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Method;
import java.util.Locale;

/** HID host output for the existing Android debugging-session helper.
 * Reads a byte from its parent for each update; closing the pipe releases the pad.
 */
public final class SlotHidOutput {
    private static final int HID_HOST=4;
    public static void main(String[] args) throws Exception {
        if(Process.myUid()!=2000 || args.length!=0)return;
        Looper.prepareMainLooper();
        Class<?> activity=Class.forName("android.app.ActivityThread");
        Object thread=activity.getMethod("systemMain").invoke(null);
        Context system=(Context)activity.getMethod("getSystemContext").invoke(thread);
        Context shell=system.createPackageContext("com.android.shell",0);
        // Standalone app_process does not inherit the zygote's Bluetooth module setup.
        if(android.os.Build.VERSION.SDK_INT>=31) {
            Class<?> init=Class.forName("android.bluetooth.BluetoothFrameworkInitializer");
            Class<?> manager=Class.forName("android.os.BluetoothServiceManager");
            if(init.getMethod("getBluetoothServiceManager").invoke(null)==null)
                init.getMethod("setBluetoothServiceManager",manager).invoke(null,manager.getConstructor().newInstance());
        }
        BluetoothAdapter adapter=shell.getSystemService(BluetoothManager.class).getAdapter();
        final BluetoothProfile[] host={null};
        Method send=Class.forName("android.bluetooth.BluetoothHidHost").getMethod("sendData",BluetoothDevice.class,String.class);
        BluetoothProfile.ServiceListener listener=new BluetoothProfile.ServiceListener() {
            public void onServiceConnected(int profile,BluetoothProfile proxy) {
                synchronized(host){host[0]=proxy;host.notifyAll();}
            }
            public void onServiceDisconnected(int profile){synchronized(host){host[0]=null;}}
        };
        if(!adapter.getProfileProxy(shell,listener,HID_HOST))throw new IllegalStateException("HID host unavailable");
        Thread worker=new Thread(()->{
            try(BufferedReader in=new BufferedReader(new InputStreamReader(System.in))) {
                String line;
                boolean announced=false;
                BluetoothDevice pad=null;
                while((line=in.readLine())!=null) {
                    int state=Integer.parseInt(line);
                    if(state<0 || state>31)break;
                    BluetoothProfile proxy;
                    synchronized(host){if(host[0]==null)host.wait(1500);proxy=host[0];}
                    if(proxy==null)continue;
                    if(pad==null) {
                        for(BluetoothDevice candidate:proxy.getConnectedDevices())
                            if("Lite2 Air Mouse Plus".equals(candidate.getName())) {
                                if(pad!=null)throw new IllegalStateException("Multiple Lite 2 controllers");
                                pad=candidate;
                            }
                        if(pad==null)continue;
                    }
                    boolean sent=(Boolean)send.invoke(proxy,pad,String.format(Locale.ROOT,"04%02X",state));
                    if(sent && !announced){System.out.println("Lite 2 slot HID output accepted");System.out.flush();announced=true;}
                }
                synchronized(host){if(host[0]!=null && pad!=null)send.invoke(host[0],pad,"0400");}
            } catch(Exception e){System.err.println("Lite 2 HID output: "+e);}
            finally{System.exit(0);}
        },"slot-hid-output");
        worker.start();
        Looper.loop();
    }
}
