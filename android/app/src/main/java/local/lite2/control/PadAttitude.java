package local.lite2.control;

/** Orientation in a level frame, with relative heading and body axes right, forward, up. */
final class PadAttitude {
    private static final double ONE_G=4096.0, RAD_PER_COUNT=Math.PI/(180*14.25);
    double w,x,y,z;
    private long previous;
    private double bx,by,bz;
    private boolean needsTilt;
    boolean ready;

    PadAttitude(){reset();}

    void reset(){
        // Until gravity is measured, show the controls facing the user.
        w=x=Math.sqrt(.5);y=z=0;
        bx=by=bz=0;previous=-1;ready=false;needsTilt=true;
    }

    void sample(long tick,int ax,int ay,int az,int gx,int gy,int gz) {
        if(tick==previous)return;
        long elapsed=(tick-previous)&0xffffffffL;
        previous=tick;
        double a=-ay,b=ax,c=az;
        double magnitude=Math.sqrt(a*a+b*b+c*c);
        double trust=Math.max(0,1-Math.abs(magnitude-ONE_G)/(ONE_G*.20));
        if(trust>0){a/=magnitude;b/=magnitude;c/=magnitude;}
        if(!ready) {
            if(trust<=0)return;
            // Resolve absolute roll and pitch without needing to start level.
            double roll=.5*Math.atan2(b,c),pitch=-.5*Math.atan2(a,Math.hypot(b,c));
            double cr=Math.cos(roll),sr=Math.sin(roll),cp=Math.cos(pitch),sp=Math.sin(pitch);
            w=cp*cr;x=cp*sr;y=sp*cr;z=-sp*sr;
            ready=true;needsTilt=false;return;
        }
        double dt=elapsed*.001;
        double rx=-gy*RAD_PER_COUNT,ry=gx*RAD_PER_COUNT,rz=gz*RAD_PER_COUNT;
        double speed=Math.sqrt(rx*rx+ry*ry+rz*rz);
        if(elapsed>300)needsTilt=true;
        else {
            if(trust>.90 && speed<.017) {
                double f=-Math.expm1(-dt*.7);
                bx+=f*(rx-bx);by+=f*(ry-by);bz+=f*(rz-bz);
            }
            rotate(rx-bx,ry-by,rz-bz,dt);
        }
        if(trust<=0)return;
        // Correct only tilt. Gravity contains no observation of absolute yaw.
        double ux=2*(x*z-w*y),uy=2*(y*z+w*x),uz=1-2*(x*x+y*y);
        double cx=b*uz-c*uy,cy=c*ux-a*uz,cz=a*uy-b*ux;
        double sine=Math.sqrt(cx*cx+cy*cy+cz*cz),dot=a*ux+b*uy+c*uz;
        double amount=needsTilt?1:-Math.expm1(-dt*trust*(speed<.12?8:2.5));
        needsTilt=false;
        if(sine>1e-8)rotate(cx/sine,cy/sine,cz/sine,amount*Math.atan2(sine,dot));
        else if(dot<0) {
            // Opposite up vectors have no unique correction axis.
            if(Math.abs(a)<.8)rotate(0,c,-b,amount*Math.PI/Math.hypot(b,c));
            else rotate(-c,0,a,amount*Math.PI/Math.hypot(a,c));
        }
    }

    void zeroHeading() {
        if(!ready)return;
        // Use the same zero-yaw convention as the initial gravity alignment.
        // When the right edge points vertically, the forward edge defines heading.
        double rightX=1-2*(y*y+z*z),rightY=2*(x*y+w*z);
        double yaw=Math.hypot(rightX,rightY)>.05 ? Math.atan2(rightY,rightX)
                : Math.atan2(-2*(x*y-w*z),1-2*(x*x+z*z));
        double c=Math.cos(-yaw*.5),s=Math.sin(-yaw*.5);
        // Left multiplication rotates the reference about world up.
        double ow=w,ox=x,oy=y,oz=z;
        w=c*ow-s*oz;x=c*ox-s*oy;y=c*oy+s*ox;z=c*oz+s*ow;
    }

    private void rotate(double rx,double ry,double rz,double interval) {
        double speed=Math.sqrt(rx*rx+ry*ry+rz*rz);
        if(speed<1e-12)return;
        double half=.5*speed*interval,scale=Math.sin(half)/speed,rw=Math.cos(half);
        rx*=scale;ry*=scale;rz*=scale;
        double ow=w,ox=x,oy=y,oz=z;
        w=ow*rw-ox*rx-oy*ry-oz*rz;x=ow*rx+ox*rw+oy*rz-oz*ry;
        y=ow*ry+oy*rw+oz*rx-ox*rz;z=ow*rz+oz*rw+ox*ry-oy*rx;
        double n=Math.sqrt(w*w+x*x+y*y+z*z);w/=n;x/=n;y/=n;z/=n;
    }
}
