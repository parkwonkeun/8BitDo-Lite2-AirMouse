package local.lite2.control;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;
import java.util.ArrayList;

/** Orthographic solid model. Only a six-axis sample can change its attitude. */
final class GyroPreview extends View {
    private static final int TOP=0xff65c9bc, BASE=0xff257c79, CAP=0xff24494b;
    // Look slightly down from in front, so a level pad and an upright one
    // have clearly different silhouettes.
    private static final double ELEVATION=Math.toRadians(22);
    private static final double S=Math.sin(ELEVATION), C=Math.cos(ELEVATION);
    private final PadAttitude pose=new PadAttitude();
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path=new Path();
    private final ArrayList<Face> controls=new ArrayList<>();
    private final float[] shell=outline(0,0,2.08f,1.13f,.39f);
    private final float[] projected=new float[2];
    private final double[] matrix=new double[9];
    private static final class Face {
        final float[] xy;final float height;final int color;
        Face(float[] xy,float height,int color){this.xy=xy;this.height=height;this.color=color;}
    }
    private static float[] outline(float cx,float cy,float w,float h,float r) {
        float[] points=new float[40];
        for(int i=0;i<4;i++)for(int j=0;j<5;j++){
            double a=(i+j/4.)*Math.PI*.5;
            int k=(i*5+j)*2;
            points[k]=cx+(i==0||i==3?w-r:r-w)+(float)Math.cos(a)*r;
            points[k+1]=cy+(i<2?h-r:r-h)+(float)Math.sin(a)*r;
        }
        return points;
    }
    private void key(float x,float y,float w,float h,float r,int color,float z){
        controls.add(new Face(outline(x,y,w,h,r),z,color));
    }
    GyroPreview(Context context) {
        super(context);
        key(-1.22f,.98f,.49f,.12f,.08f,CAP,.19f);key(1.22f,.98f,.49f,.12f,.08f,CAP,.19f);
        for(float[] v:new float[][]{{-1.36f,.28f},{.68f,-.48f}}) {
            key(v[0],v[1],.33f,.33f,.33f,BASE,.18f);
            key(v[0],v[1],.24f,.24f,.24f,CAP,.26f);
        }
        key(-.64f,-.49f,.27f,.09f,.025f,CAP,.19f);
        key(-.64f,-.49f,.09f,.27f,.025f,CAP,.19f);
        key(1.40f,.30f,.135f,.135f,.135f,0xffa33f42,.20f);
        key(1.14f,.05f,.135f,.135f,.135f,0xffd4a342,.20f);
        key(1.14f,.55f,.135f,.135f,.135f,0xff3c74a0,.20f);
        key(.88f,.30f,.135f,.135f,.135f,0xff347661,.20f);
        key(-.49f,.50f,.11f,.04f,.02f,CAP,.19f);
        key(.46f,.50f,.11f,.04f,.02f,CAP,.19f);key(.46f,.50f,.04f,.11f,.02f,CAP,.19f);
        key(-1.35f,-.76f,.09f,.09f,.09f,CAP,.19f);key(1.22f,-.76f,.09f,.09f,.09f,CAP,.19f);
    }
    boolean hasAttitude(){return pose.ready;}
    void zeroHeading(){pose.zeroHeading();invalidate();}
    void waitForInput(){pose.reset();invalidate();}
    void sample(long tick,int ax,int ay,int az,int gx,int gy,int gz){
        pose.sample(tick,ax,ay,az,gx,gy,gz);postInvalidateOnAnimation();
    }
    private void project(float a,float b,float c){
        double worldY=matrix[3]*a+matrix[4]*b+matrix[5]*c,worldZ=matrix[6]*a+matrix[7]*b+matrix[8]*c;
        projected[0]=(float)(matrix[0]*a+matrix[1]*b+matrix[2]*c);
        projected[1]=(float)(-S*worldY-C*worldZ);
    }
    private void vertex(float a,float b,float c,boolean start){
        project(a,b,c);if(start)path.moveTo(projected[0],projected[1]);else path.lineTo(projected[0],projected[1]);
    }
    private void face(Canvas canvas,float[] xy,float height,int color){
        path.rewind();for(int i=0;i<xy.length;i+=2)vertex(xy[i],xy[i+1],height,i==0);
        path.close();paint.setColor(color);canvas.drawPath(path,paint);
    }
    @Override protected void onDraw(Canvas canvas){
        double w=pose.w,x=pose.x,y=pose.y,z=pose.z;
        matrix[0]=1-2*(y*y+z*z);matrix[1]=2*(x*y-w*z);matrix[2]=2*(x*z+w*y);
        matrix[3]=2*(x*y+w*z);matrix[4]=1-2*(x*x+z*z);matrix[5]=2*(y*z-w*x);
        matrix[6]=2*(x*z-w*y);matrix[7]=2*(y*z+w*x);matrix[8]=1-2*(x*x+y*y);
        double nx=-C*matrix[3]+S*matrix[6],ny=-C*matrix[4]+S*matrix[7],nz=-C*matrix[5]+S*matrix[8];
        int save=canvas.save();canvas.translate(getWidth()/2f,getHeight()/2f);
        float scale=Math.min(getWidth(),getHeight())*.207f;canvas.scale(scale,scale);
        for(int i=0;i<shell.length;i+=2) {
            int j=(i+2)%shell.length;float dx=shell[j]-shell[i],dy=shell[j+1]-shell[i+1];
            if(dy*nx-dx*ny<=0)continue;
            path.rewind();vertex(shell[i],shell[i+1],-.17f,true);vertex(shell[j],shell[j+1],-.17f,false);
            vertex(shell[j],shell[j+1],.15f,false);vertex(shell[i],shell[i+1],.15f,false);path.close();
            paint.setColor(BASE);canvas.drawPath(path,paint);
        }
        face(canvas,shell,nz>=0?.15f:-.17f,nz>=0?TOP:0xff317f79);
        if(nz>0)for(int i=0;i<controls.size();i++){
            Face f=controls.get(i);face(canvas,f.xy,f.height,f.color);
        }
        canvas.restoreToCount(save);
    }
}
