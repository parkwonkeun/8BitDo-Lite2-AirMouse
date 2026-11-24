#include "mouse_core.h"

/* Switch IMU units: 4096 counts/g and nominally 14.25 counts/(degree/s).
   The covariance is isotropic: rotation preserves P*I, so the three gravity
   components share a scalar Kalman gain. Normalization constrains unit length. */
enum { ONE = 4096, R_ACCEL = 8192 };
#define FUSION __attribute__((section(".gravity"),noinline))

static FUSION unsigned norm(const int32_t v[3]) {
    unsigned squared = 0, root = 0;
    for (int i=0; i<3; ++i) squared += (unsigned)v[i]*(unsigned)v[i];
    for (unsigned bit=32768; bit; bit>>=1) {
        unsigned trial=root|bit;
        if (trial*trial<=squared) root=trial;
    }
    return root;
}

static int rounded(int value, int divisor) {
    return (value+(value<0 ? -divisor/2 : divisor/2))/divisor;
}

static __attribute__((noinline)) void predict_up(int32_t u[3], const int32_t omega[3], unsigned dt) {
    /* In the body frame du/dt = up cross omega. Short substeps bound the
       rotation and keep all arithmetic within signed 32-bit on Cortex-M0. */
    for (unsigned remaining=dt;remaining;) {
        unsigned step=remaining>8?8:remaining;
        int32_t rotation[3];
        for (int i=0;i<3;++i) rotation[i]=rounded(omega[i]*(int)step,199);
        int32_t old[3]={u[0],u[1],u[2]};
        for (int i=0;i<3;++i) {
            int j=(i+1)%3,k=(i+2)%3;
            u[i]+=rounded(old[j]*rotation[k]-old[k]*rotation[j],ONE);
        }
        remaining-=step;
        if (remaining && (rotation[0]*rotation[0]+rotation[1]*rotation[1]+rotation[2]*rotation[2]>65536)) {
            int n=(int)norm(u);
            for (int i=0;i<3;++i) u[i]=rounded(u[i]*ONE,n);
        }
    }
}

FUSION void gravity_rates(MouseState *s, const int16_t imu[6], unsigned dt) {
    int32_t omega[3]={imu[3]-s->bias_roll,imu[4]-s->bias_x,imu[5]-s->bias_z};
    int32_t a[3]={imu[0],imu[1],imu[2]}, u[3];
    int length=(int)norm(a);
    int error=length-ONE;
    if (error<0) error=-error;
    if (!s->tilt_variance) {
        s->yaw_rate=s->pitch_rate=0;
        if (error>ONE/5) return;
        for (int i=0;i<3;++i) u[i]=a[i];
        s->tilt_variance=256;
    } else {
        for (int i=0;i<3;++i) u[i]=s->up[i];
        predict_up(u,omega,dt);
        int p=s->tilt_variance+(int)dt*2;
        if (p>R_ACCEL) p=R_ACCEL;
        if (length && error<ONE/5 && dt) {
            int innovation[3], max_delta=0;
            for (int i=0;i<3;++i) {
                innovation[i]=a[i]*ONE/length-u[i];
                int size=innovation[i]<0?-innovation[i]:innovation[i];
                if (size>max_delta) max_delta=size;
            }
            /* Increase measurement noise during linear acceleration. */
            int noise=R_ACCEL+error*16+max_delta*12;
            for (int i=0;i<3;++i) u[i]+=rounded(p*innovation[i],p+noise);
            p-=rounded(p*p,p+noise);
        }
        s->tilt_variance=p;
        length=(int)norm(u);
    }
    if (!length) {s->tilt_variance=0;s->yaw_rate=s->pitch_rate=0;return;}
    int dot=0;
    for (int i=0;i<3;++i) {
        s->up[i]=rounded(u[i]*ONE,length);
        dot+=s->up[i]*omega[i];
    }
    int vertical=rounded(dot,ONE);
    s->yaw_rate=-vertical;
    /* Transport the normal-grip pitch axis onto the horizontal plane.
       Near fully inverted, use projected body Y to avoid the pole. */
    int z=s->up[2], y=s->up[1];
    s->pitch_rate=omega[1]-(z < -4000 ? rounded(y*vertical,ONE) :
        rounded(y*(vertical+omega[2]),ONE+z));
}
