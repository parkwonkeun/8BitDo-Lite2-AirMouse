package local.lite2.control;

import android.app.Activity;
import android.bluetooth.*;
import android.content.pm.PackageManager;
import android.os.*;
import java.util.UUID;

/** Reads a single paired pad only while its monitor is visible. */
final class PadMonitorGatt extends BluetoothGattCallback {
    interface Listener {void sample(byte[] bytes);void disconnected();}
    static final String PERMISSION="android.permission.BLUETOOTH_CONNECT";
    private static final UUID SERVICE=UUID.fromString("ee145e00-f84b-45df-834e-df1163140054");
    private static final UUID SAMPLE=UUID.fromString("ee145e01-f84b-45df-834e-df1163140054");
    private final Activity activity;
    private final Listener listener;
    private final Handler main=new Handler(Looper.getMainLooper());
    private BluetoothDevice device;
    private BluetoothGatt link;
    private BluetoothGattCharacteristic sample;
    private String peer;
    private boolean requested,enabled,announced;
    PadMonitorGatt(Activity activity,Listener listener){this.activity=activity;this.listener=listener;}
    void configure(boolean show,String address) {
        if(show==enabled && java.util.Objects.equals(peer,address) && (link!=null || !show))return;
        if(!show || !java.util.Objects.equals(peer,address))stop();
        enabled=show;peer=address;
        if(!show || address==null || !BluetoothAdapter.checkBluetoothAddress(address))return;
        if(Build.VERSION.SDK_INT>=31 && activity.checkSelfPermission(PERMISSION)!=PackageManager.PERMISSION_GRANTED) {
            if(!requested){requested=true;activity.requestPermissions(new String[]{PERMISSION},54);}
            return;
        }
        BluetoothAdapter adapter=activity.getSystemService(BluetoothManager.class).getAdapter();
        if(adapter==null)return;
        device=adapter.getRemoteDevice(address);
        if(link==null)connect();
    }
    private void stop() {
        main.removeCallbacksAndMessages(null);
        if(link!=null){BluetoothGatt old=link;link=null;old.close();}
        device=null;sample=null;announced=false;listener.disconnected();
    }
    private void connect() {
        main.removeCallbacksAndMessages(null);
        if(!enabled || device==null)return;
        link=device.connectGatt(activity,false,this,BluetoothDevice.TRANSPORT_LE,BluetoothDevice.PHY_LE_1M_MASK,main);
        main.postDelayed(this::retry,5000);
    }
    private void retry() {
        main.removeCallbacksAndMessages(null);
        if(link!=null){link.close();link=null;}
        sample=null;announced=false;listener.disconnected();
        if(enabled && device!=null)main.postDelayed(this::connect,2000);
    }
    @Override public void onConnectionStateChange(BluetoothGatt g,int status,int state) {
        if(g!=link)return;
        if(status!=0 || state!=BluetoothProfile.STATE_CONNECTED || !g.discoverServices())retry();
    }
    @Override public void onServicesDiscovered(BluetoothGatt g,int status) {
        if(g!=link)return;
        BluetoothGattService svc=g.getService(SERVICE);
        sample=svc==null?null:svc.getCharacteristic(SAMPLE);
        if(status!=0 || sample==null){android.util.Log.i("Lite2Monitor","Sensor service unavailable: "+status+" / "+(svc!=null));main.removeCallbacksAndMessages(null);return;}
        main.removeCallbacksAndMessages(null);read();
    }
    private void read() {
        if(link==null || sample==null)return;
        if(!link.readCharacteristic(sample)){retry();return;}
        main.postDelayed(this::retry,2000);
    }
    @Override public void onCharacteristicRead(BluetoothGatt g,BluetoothGattCharacteristic c,int status) {
        received(g,c,c.getValue(),status);
    }
    @Override public void onCharacteristicRead(BluetoothGatt g,BluetoothGattCharacteristic c,byte[] data,int status) {
        received(g,c,data,status);
    }
    private void received(BluetoothGatt g,BluetoothGattCharacteristic c,byte[] data,int status) {
        if(g!=link || c!=sample)return;
        main.removeCallbacksAndMessages(null);
        if(status!=0 || data==null || data.length!=20){retry();return;}
        if(!announced){android.util.Log.i("Lite2Monitor","Receiving stick and six-axis samples");announced=true;}
        listener.sample(data);
        main.postDelayed(this::read,20);
    }
}
