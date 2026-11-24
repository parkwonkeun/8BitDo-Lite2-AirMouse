package local.lite2.control;

import java.util.Locale;
import org.json.JSONArray;

/** Physical input range, in HID counts, before the slot's response curve. */
final class StickCalibration {
    final int[][] axis={{-127,0,0,127},{-127,0,0,127}};

    static boolean valid(int[] a) {
        return a[0]>=-128 && a[3]<=127 && a[0]<a[1] && a[1]<=a[2] && a[2]<a[3];
    }
    float normalized(int i, float value) {
        int[] a=axis[i];
        if(value<a[1])return Math.max(-1f,(value-a[1])/(a[1]-a[0]));
        if(value>a[2])return Math.min(1f,(value-a[2])/(a[3]-a[2]));
        return 0;
    }
    JSONArray json() {
        JSONArray values=new JSONArray();
        for(int[] a:axis)for(int v:a)values.put(v);
        return values;
    }
    void read(JSONArray values) {
        if(values==null || values.length()!=8)return;
        int[][] copy=new int[2][4];
        for(int i=0;i<2;i++){
            for(int j=0;j<4;j++)copy[i][j]=values.optInt(i*4+j,999);
            if(!valid(copy[i]))return;
        }
        for(int i=0;i<2;i++)System.arraycopy(copy[i],0,axis[i],0,4);
    }
    String summary() {
        return axisText("X",axis[0])+"     "+axisText("Y",axis[1]);
    }
    private static String axisText(String name,int[] a) {
        String center=a[1]==a[2]?Integer.toString(a[1]):a[1]+"…"+a[2];
        return String.format(Locale.ROOT,"%s   %d  /  %s  /  %d",name,a[0],center,a[3]);
    }

    static final class Capture {
        final boolean center;
        final int[] low=new int[2],high=new int[2];
        long first,last;
        int count;
        Capture(boolean center){this.center=center;}
        void sample(int x,int y,long now) {
            if(x < -128 || x>127 || y < -128 || y>127)return;
            if(center && (Math.abs(x)>48 || Math.abs(y)>48)){count=0;return;}
            if(center && (now-last>300 || Math.max(high[0],x)-Math.min(low[0],x)>8 ||
                    Math.max(high[1],y)-Math.min(low[1],y)>8))count=0;
            if(count==0){first=now;low[0]=high[0]=x;low[1]=high[1]=y;}
            low[0]=Math.min(low[0],x);high[0]=Math.max(high[0],x);
            low[1]=Math.min(low[1],y);high[1]=Math.max(high[1],y);
            count++;last=now;
        }
        boolean ready(StickCalibration target,long now) {
            if(count<10 || now-last>350 || last-first<750)return false;
            for(int i=0;i<2;i++){
                int[] a=target.axis[i];
                if(center ? low[i]<=a[0] || high[i]>=a[3] :
                    a[1]-low[i]<32 || high[i]-a[2]<32)return false;
            }
            return true;
        }
        void apply(StickCalibration target) {
            for(int i=0;i<2;i++){
                target.axis[i][center?1:0]=low[i];
                target.axis[i][center?2:3]=high[i];
            }
        }
    }
}
