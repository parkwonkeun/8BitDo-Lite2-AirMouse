#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int signal_number) { (void)signal_number; stopped=1; }
static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int numeric_suffix(const char *path) {
    const char *prefix="/dev/input/event";
    size_t n=strlen(prefix);
    if(strncmp(path,prefix,n) || !path[n]) return 0;
    for(const char *p=path+n;*p;p++) if(*p<'0'||*p>'9')return 0;
    return 1;
}
static int physical_node(const char *node,const char *expected) {
    if(!numeric_suffix(node))return -1;
    int fd=open(node,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    char name[128]={0};
    struct input_id id;
    if(fd<0)return -1;
    if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0 || ioctl(fd,EVIOCGID,&id)<0 ||
       id.vendor!=0x2dc8 ||
       (id.bustype!=BUS_BLUETOOTH && id.bustype!=BUS_USB) ||
       (expected?(strcmp(name,expected) || id.product!=0x5112):
        (!strcasestr(name,"lite 2") || strcasestr(name,"air mouse")))) {
        close(fd);errno=ENODEV;return -1;
    }
    if(!expected) {
        struct input_absinfo info;
        unsigned long bits[KEY_MAX/(8*sizeof(unsigned long))+1]={0};
        if(ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(bits)),bits)<0 ||
           !(bits[BTN_GAMEPAD/(8*sizeof(unsigned long))]&(1UL<<(BTN_GAMEPAD%(8*sizeof(unsigned long))))) ||
           ioctl(fd,EVIOCGABS(ABS_X),&info)<0 || ioctl(fd,EVIOCGABS(ABS_Y),&info)<0) {
            close(fd);return -1;
        }
    }
    return fd;
}
static int virtual_mouse(void) {
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0)return -1;
    struct uinput_setup config={.id={BUS_VIRTUAL,0,0,1}};
    strcpy(config.name,"Lite 2 Control · remapped pointer");
    const int rel[]={REL_X,REL_Y,REL_WHEEL,REL_WHEEL_HI_RES,REL_HWHEEL,REL_HWHEEL_HI_RES};
    const int keys[]={BTN_LEFT,BTN_RIGHT,BTN_MIDDLE};
    if(ioctl(fd,UI_SET_EVBIT,EV_REL)<0 || ioctl(fd,UI_SET_EVBIT,EV_KEY)<0)goto fail;
    for(size_t i=0;i<sizeof(rel)/sizeof(*rel);i++)if(ioctl(fd,UI_SET_RELBIT,rel[i])<0)goto fail;
    for(size_t i=0;i<sizeof(keys)/sizeof(*keys);i++)if(ioctl(fd,UI_SET_KEYBIT,keys[i])<0)goto fail;
    if(ioctl(fd,UI_DEV_SETUP,&config)<0 || ioctl(fd,UI_DEV_CREATE)<0)goto fail;
    return fd;
fail:
    close(fd);
    return -1;
}
static int listener(void) {
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0),on=1;
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(39728),.sin_addr={htonl(INADDR_LOOPBACK)}};
    if(fd<0)return -1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
    if(bind(fd,(struct sockaddr*)&addr,sizeof(addr))<0 || listen(fd,1)<0) {
        close(fd);return -1;
    }
    return fd;
}
static int accept_app(int server,const char *token,int *watch) {
    struct pollfd p={.fd=server,.events=POLLIN};
    if(poll(&p,1,1000)<=0)return -1;
    int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC);
    struct timeval limit={.tv_sec=2};
    if(fd<0)return -1;
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&limit,sizeof(limit));
    char supplied[34]={0};
    size_t total=0;
    while(total<33) {
        ssize_t n=read(fd,supplied+total,33-total);
        if(n<=0)break;
        total+=n;
    }
    if(total!=33 || (supplied[32]!='\n' && supplied[32]!='?' && supplied[32]!='!') || memcmp(supplied,token,32)) {
        close(fd);errno=EACCES;return -1;
    }
    *watch=supplied[32]=='!'?2:supplied[32]=='?';
    return fd;
}
static int is_hex_token(const char *p) {
    if(strlen(p)!=32)return 0;
    for(int i=0;i<32;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;
    return 1;
}
static int allowed(struct input_event e) {
    if(e.type==EV_SYN)return e.code==SYN_REPORT;
    if(e.type==EV_KEY)return e.code==BTN_LEFT || e.code==BTN_RIGHT || e.code==BTN_MIDDLE;
    if(e.type!=EV_REL)return 0;
    return e.code==REL_Z || e.code==REL_DIAL || e.code==REL_RX || e.code==REL_RY || e.code==REL_RZ ||
        e.code==REL_X || e.code==REL_Y || e.code==REL_WHEEL ||
        e.code==REL_WHEEL_HI_RES || e.code==REL_HWHEEL || e.code==REL_HWHEEL_HI_RES;
}

enum { CURVE_SIZE=33 };
struct spline {
    int count;
    double x[9],y[9],slope[9];
};
struct profile {
    struct spline cursor_curve,scroll_curve;
    int click_hold;
    int gx,gy,pointer_dead,wheel_dead;
    int pointer[CURVE_SIZE], wheel[CURVE_SIZE];
};
struct gyro_zero {
    double ms, sum[2], square[2], bias[2], noise[2], trend[2];
    unsigned samples;
    int ready;
};

static void correct_gyro(struct gyro_zero *z,double axis[2],double dt) {
    if(!z->ready) {
        if(fmax(fabs(axis[0]),fabs(axis[1]))>24) {
            *z=(struct gyro_zero){0};
            return;
        }
        z->samples++;
        z->ms+=dt;
        for(int i=0;i<2;i++) {
            z->sum[i]+=axis[i]*dt;
            z->square[i]+=axis[i]*axis[i]*dt;
            axis[i]=0;
        }
        if(z->ms<1200 || z->samples<24)return;
        for(int i=0;i<2;i++) {
            z->bias[i]=z->sum[i]/z->ms;
            z->noise[i]=sqrt(fmax(0,z->square[i]/z->ms-z->bias[i]*z->bias[i]));
        }
        if(fmax(z->noise[0],z->noise[1])>4) {
            *z=(struct gyro_zero){0};
            return;
        }
        z->ready=1;
        printf("Gyro rest offset %.2f %.2f, noise %.2f %.2f\n",z->bias[0],z->bias[1],z->noise[0],z->noise[1]);
        fflush(stdout);
        return;
    }
    for(int i=0;i<2;i++) {
        double residual=axis[i]-z->bias[i];
        // Use the same capped direction evidence as the standalone pad.
        // Count elapsed sensor time, never time spent waiting on a late report.
        double level=fabs(residual),elapsed=fmin(10.,dt);
        double score=residual<0 ? -z->trend[i] : z->trend[i];
        if(score<0)score*=.5;
        if(level>=7)elapsed*=2;
        if(level>=16)elapsed*=2;
        if(level<1.75)elapsed*=.25;
        if(level<.75)score*=.5;
        else score+=elapsed;
        score=fmin(80.,score);
        z->trend[i]=residual<0 ? -score : score;
        if(level>=32. || score==80.)axis[i]=residual;
        else {
            if(fabs(score)<=4.)z->bias[i]+=residual*dt/(12000.+dt);
            axis[i]=0;
        }
    }
}

struct motion {
    struct gyro_zero zero;
    int dx,dy,notches,hires;
    int preview_client;
    double rx,ry,rwheel;
    double wheel_velocity;
    uint64_t wheel_until,wheel_updated,wheel_due;
    int wheel_phase;
    unsigned samples;
    int logged_raw_gyro;
    int raw_x,raw_y,source,last_source;
    double vx,vy,speed;
    uint64_t integrated_until,lease_end;
    unsigned clicks,pressed_clicks,released_clicks;
    uint64_t click_until;
    uint64_t previous_motion,previous_wheel,previous_raw;
};
static int find_node(const char *name) {
    DIR *dir=opendir("/dev/input");
    if(!dir)return -1;
    struct dirent *entry;
    int fd=-1;
    while((entry=readdir(dir))) {
        if(strncmp(entry->d_name,"event",5)!=0)continue;
        char node[300];
        snprintf(node,sizeof(node),"/dev/input/%s",entry->d_name);
        fd=physical_node(node,name);
        if(fd>=0)break;
    }
    closedir(dir);
    return fd;
}
static int number(const char **pos,double low,double high,double *value) {
    if(**pos!=' ')return 0;
    char *end;
    const char *start=*pos+1;
    *value=strtod(start,&end);
    if(end==start || !isfinite(*value) || *value<low || *value>high)return 0;
    *pos=end;
    return 1;
}
static int read_spline(const char **pos,struct spline *curve) {
    double count;
    if(!number(pos,3,9,&count) || count!=floor(count))return 0;
    curve->count=(int)count;
    for(int i=0;i<curve->count;i++) {
        if(!number(pos,0,100,&curve->x[i]) || !number(pos,0,100,&curve->y[i]) ||
           !number(pos,0,100,&curve->slope[i]))return 0;
        if(curve->x[i]!=floor(curve->x[i]) || curve->y[i]!=floor(curve->y[i]) ||
           (i && (curve->x[i]<=curve->x[i-1] || curve->y[i]<curve->y[i-1])))return 0;
    }
    return curve->x[0]==0 && curve->x[curve->count-1]==100;
}
static double spline_output(const struct spline *curve,double tilt) {
    double at=100*fmin(1.,tilt);
    if(at<=0)return 0;
    int i=0;
    while(i+2<curve->count && at>curve->x[i+1])i++;
    double width=curve->x[i+1]-curve->x[i];
    double t=(at-curve->x[i])/width,u=1.-t;
    return (u*u*(1+2*t)*curve->y[i]+t*t*(3-2*t)*curve->y[i+1]
        +width*t*u*(u*curve->slope[i]-t*curve->slope[i+1]))/100.;
}
static double spline_gain(const struct spline *curve,double tilt) {
    double at=100*fmin(1.,tilt);
    double wanted=spline_output(curve,tilt);
    // Convert desired output to a correction of the already-shaped HID reports.
    double reference=pow(fmax((at-14.)/86.,0.),1.35);
    reference=fmax(reference,pow(2./86.,1.35));
    return fmin(8.,fmax(0.,wanted/reference));
}
enum { MAX_GYRO_PERCENT = 500 };
static int parse_profile(const char *line,struct profile *settings) {
    if(line[0]=='C' || line[0]=='F') {
        struct profile update={.click_hold=1};
        const char *pos=line+1;
        double gx,gy;
        if(!number(&pos,0,MAX_GYRO_PERCENT,&gx) || !number(&pos,0,MAX_GYRO_PERCENT,&gy) ||
           !read_spline(&pos,&update.cursor_curve) || !read_spline(&pos,&update.scroll_curve))return 0;
        if(line[0]=='F') {
            double hold;
            if(!number(&pos,0,1,&hold) || hold!=floor(hold) || *pos)return 0;
            update.click_hold=(int)hold;
        } else if(*pos) {
            double flag;
            if(!number(&pos,0,1,&flag) || flag!=floor(flag))return 0;
            struct spline legacy_gyro;
            if(*pos && (!read_spline(&pos,&legacy_gyro) || *pos))return 0;
        }
        update.gx=(int)gx;update.gy=(int)gy;
        *settings=update;
        return 1;
    }
    if(strncmp(line,"P ",2))return 0;
    const char *pos=line+2;
    struct profile update={0};
    for(int i=0;i<2+2*CURVE_SIZE;i++) {
        char *end;
        long n=strtol(pos,&end,10);
        if(end==pos || n<0 || n>(i<2?MAX_GYRO_PERCENT:8000))return 0;
        if(i==0)update.gx=(int)n;
        else if(i==1)update.gy=(int)n;
        else if(i<2+CURVE_SIZE)update.pointer[i-2]=(int)n;
        else update.wheel[i-2-CURVE_SIZE]=(int)n;
        if(i+1<2+2*CURVE_SIZE && *end!=' ')return 0;
        pos=end;
    }
    if(*pos) {
        char extra;
        if(sscanf(pos," %d %d %c",&update.pointer_dead,&update.wheel_dead,&extra)!=2 ||
           update.pointer_dead<0 || update.pointer_dead>10000 ||
           update.wheel_dead<0 || update.wheel_dead>10000)return 0;
    }
    *settings=update;
    return 1;
}
static double ratio(const int *curve,double tilt,int dead) {
    if(fmin(tilt,1.)*10000<=dead)return 0.;
    double pos=fmax(0,fmin(CURVE_SIZE-1,tilt*CURVE_SIZE-.5));
    int i=(int)pos;
    double gain=curve[i];
    if(i+1<CURVE_SIZE)gain+=(curve[i+1]-curve[i])*(pos-i);
    return gain/1000.;
}
static int quantize(double value,double *remainder) {
    double total=value+*remainder;
    int result=(int)floor(total+0.5);
    *remainder=total-result;
    return result;
}
static int emit(int fd,uint16_t type,uint16_t code,int value) {
    if(fd<0)return 1;  // Monitor sessions have no virtual output device.
    struct input_event event={.type=type,.code=code,.value=value};
    return write(fd,&event,sizeof(event))==sizeof(event);
}
static int preview_delta(int client,int x,int y) {
    if(!(x || y))return 1;
    char line[48];
    int length=snprintf(line,sizeof(line),"G %d %d\n",x,y);
    return write(client,line,length)==length;
}
static int pointer_delta(int out,struct motion *m,int x,int y) {
    if(out<0)
        return !m->preview_client || m->last_source==2 || preview_delta(m->preview_client,x,y);
    return (!x || emit(out,EV_REL,REL_X,x)) && (!y || emit(out,EV_REL,REL_Y,y));
}
// Integrate the latest physical pointer velocity on the tablet clock. The short
// lease bounds movement if the pad stops reporting without a release frame.
static int advance_pointer(int out,struct motion *m,uint64_t now) {
    if(!m->lease_end)return 1;
    uint64_t end=now<m->lease_end?now:m->lease_end;
    if(end<=m->integrated_until)return 1;
    double ms=(double)(end-m->integrated_until);
    m->integrated_until=end;
    if(now>=m->lease_end)m->lease_end=0;
    int x=quantize(m->vx*ms,&m->rx),y=quantize(m->vy*ms,&m->ry);
    return pointer_delta(out,m,x,y)
        && (!(x||y) || emit(out,EV_SYN,SYN_REPORT,0));
}
// Integrate the last right-stick sample on the tablet clock. Send fractions of
// a detent at most every 8 ms, including between wireless input reports.
static int advance_wheel(int out,struct motion *m,uint64_t now,int finish) {
    if(!m->wheel_until)return 1;
    uint64_t end=now<m->wheel_until?now:m->wheel_until;
    m->rwheel+=m->wheel_velocity*(end-m->wheel_updated);
    m->wheel_updated=end;
    finish=finish || now>=m->wheel_until;
    if(!finish && now<m->wheel_due)return 1;
    int units=quantize(0,&m->rwheel);
    m->wheel_phase+=units;
    int steps=m->wheel_phase/120;
    m->wheel_phase-=steps*120;
    m->wheel_due=now+8;
    if(finish){m->wheel_until=0;m->rwheel=0;}
    return (!units || emit(out,EV_REL,REL_WHEEL_HI_RES,units)) &&
           (!steps || emit(out,EV_REL,REL_WHEEL,steps)) &&
           (!units || emit(out,EV_SYN,SYN_REPORT,0));
}
static double measured_speed(struct motion *m,uint64_t us) {
    uint64_t elapsed=us-m->previous_motion;
    int fresh=!m->previous_motion || elapsed>250000;
    double ms=fresh?10.:fmax(1.,elapsed/1000.);
    double instant=hypot((double)m->dx,m->dy)*1000./ms;
    m->speed=fresh?instant:m->speed+(instant-m->speed)*ms/(ms+26.);
    m->previous_motion=us;
    return m->speed;
}
static int flush_motion(int out,struct motion *m,const struct profile *p,uint64_t us) {
    uint64_t now=now_ms();
    if(!advance_pointer(out,m,now))return 0;
    int raw_frame=m->source>=1024 && m->source<1792;
    int wheel_offset=0;
    double raw_ms=0;
    if(raw_frame) {
        if(!m->previous_raw){puts("r19 raw input connected");fflush(stdout);}
        int tag=m->source-1024;
        wheel_offset=128-(tag & 255);
        m->source=tag>>8;
        if(!m->source && m->last_source) m->zero=(struct gyro_zero){0};
        uint64_t delta=us-m->previous_raw;
        raw_ms=m->previous_raw && delta<=100000?delta/1000.:10.;
        m->previous_raw=us;
    }
    if(m->source!=m->last_source && (m->source || raw_frame)) {
        m->rx=m->ry=0;m->previous_motion=0;m->lease_end=0;
        m->last_source=m->source;
        printf("Physical source: %s\n",m->source==2?"stick":"gyro");fflush(stdout);
    }
    int gyro=m->source==1 || (raw_frame && m->source==0);
    if(!gyro || !p->click_hold)m->click_until=0;
    else if(m->pressed_clicks)m->click_until=us+200000;
    else if(m->released_clicks)m->click_until=0;
    if(!m->clicks)m->click_until=0;
    int hold=gyro && us<m->click_until;
    m->pressed_clicks=m->released_clicks=0;
    if(hold){m->rx=m->ry=0;m->dx=m->dy=0;}
    unsigned hold_ms=45;
    if(m->source==2 && p->cursor_curve.count) {
        double radius=hypot((double)m->raw_x,m->raw_y);
        double response=spline_output(&p->cursor_curve,radius/127.);
        double scale=radius>0 ? 2.44*response/radius : 0;
        m->vx=scale*m->raw_x;m->vy=scale*m->raw_y;

    } else if(raw_frame && m->source==1) {
        double axis[]={m->raw_x,m->raw_y};
        correct_gyro(&m->zero,axis,raw_ms);
        double vx=hold?0:axis[0]*9./1024.,vy=hold?0:axis[1]*4./1024.;
        if((m->raw_x || m->raw_y) && !m->logged_raw_gyro) {
            m->logged_raw_gyro=1;
            printf("Raw gyro input X=%d Y=%d\n",m->raw_x,m->raw_y);
            fflush(stdout);
        }
        vx*=p->gx/100.;
        vy*=p->gy/100.;
        // Bridge the measured longer radio gaps only when consecutive
        // angular velocities agree in both direction and magnitude.
        double change=hypot(vx-m->vx,vy-m->vy);
        if(m->lease_end && change<=.25*hypot(m->vx,m->vy))hold_ms=65;
        m->vx=vx;
        m->vy=vy;
    } else if(m->dx || m->dy) {
        double gain=1.;
        // A raw idle tag can carry firmware XY before the initial zero is ready.

        if(!gyro) {
            double speed=measured_speed(m,us);
            double tilt=(14.+sqrt(1024.+8.*speed*1.024)-32.)/128.;
            gain=p->cursor_curve.count?spline_gain(&p->cursor_curve,tilt):ratio(p->pointer,tilt,p->pointer_dead);
        }
        int x=quantize(m->dx*gain*(gyro?p->gx/100.:1.),&m->rx);
        int y=quantize(m->dy*gain*(gyro?p->gy/100.:1.),&m->ry);
        if(!pointer_delta(out,m,x,y))return 0;
        if(m->samples++==0) {
            printf("First movement in=(%d,%d) out=(%d,%d) X=%d Y=%d\n",m->dx,m->dy,x,y,p->gx,p->gy);
            fflush(stdout);
        }
    }
    if ((raw_frame && m->source==1) || (m->source==2 && p->cursor_curve.count)) {
        m->integrated_until=now;
        m->lease_end=(m->vx || m->vy)?now+hold_ms:0;
    }
    m->source=m->raw_x=m->raw_y=0;
    if(raw_frame && p->scroll_curve.count) {
        double response=spline_output(&p->scroll_curve,abs(wheel_offset)/127.);
        double velocity=copysign(response*3.6,wheel_offset);
        if(!advance_wheel(out,m,now,!velocity || velocity*m->wheel_velocity<0))return 0;
        if(velocity) {
            if(!m->wheel_until)m->wheel_due=now+8;
            m->wheel_updated=now;
            m->wheel_until=now+45;
            m->wheel_velocity=velocity;
        }
    } else if(m->hires || m->notches) {
        int units=m->hires?m->hires:m->notches*120;
        uint64_t elapsed=us-m->previous_wheel;
        double rate=elapsed && elapsed<=350000?fabs((double)units)/120.*1000000./elapsed:4.;
        double tilt=(22.+4.*fmax(0,rate-4.))/128.;
        double gain=p->scroll_curve.count?spline_gain(&p->scroll_curve,tilt):ratio(p->wheel,tilt,p->wheel_dead);
        int scaled=quantize(units*gain,&m->rwheel);
        m->wheel_phase+=scaled;
        int notches=m->wheel_phase/120;
        m->wheel_phase-=120*notches;
        if((scaled && !emit(out,EV_REL,REL_WHEEL_HI_RES,scaled)) ||
           (notches && !emit(out,EV_REL,REL_WHEEL,notches)))return 0;
        m->previous_wheel=us;
    }
    m->dx=m->dy=m->notches=m->hires=0;
    return emit(out,EV_SYN,SYN_REPORT,0);
}
// Nonzero halves also encode all-released, which relative HID axes would
// otherwise omit. Ignore incomplete frames instead of inventing a release.
static int button_snapshot(int low,int high) {
    if(low<1 || low>512 || high<1 || high>512)return -1;
    return (low-1)|((high-1)<<9);
}
static int monitor_axis(int bucket) {
    // The center spans buckets 15 and 16 on a nominal 0..255 ADC.
    return bucket<16?(bucket-15)*127/15:(bucket-16)*127/15;
}
struct gamepad_state {
    struct input_absinfo axes[ABS_CNT];
    unsigned long keys[KEY_MAX/(8*sizeof(unsigned long))+1];
    int last[7],sent;
};
static void read_gamepad(int fd,struct gamepad_state *d) {
    for(int i=0;i<ABS_CNT;i++)ioctl(fd,EVIOCGABS(i),&d->axes[i]);
    ioctl(fd,EVIOCGKEY(sizeof(d->keys)),d->keys);
}
static int axis_level(struct gamepad_state *d,int code,int centered) {
    struct input_absinfo *a=&d->axes[code];
    if(a->maximum<=a->minimum)return 0;
    double v=(double)(a->value-a->minimum)/(a->maximum-a->minimum);
    return (int)lround(127.*(centered?2.*v-1.:v));
}
static int send_gamepad(int client,struct gamepad_state *d) {
    // Same scanner order as the S-mode report so saved button assignments are shared.
    const int codes[]={BTN_START,BTN_THUMBL,BTN_THUMBR,BTN_SELECT,BTN_NORTH,BTN_WEST,
        BTN_DPAD_RIGHT,BTN_DPAD_LEFT,BTN_DPAD_DOWN,BTN_DPAD_UP,BTN_TL,BTN_TR,
        BTN_EAST,BTN_SOUTH,BTN_TL2,BTN_TR2,KEY_RECORD,BTN_MODE};
    int mask=0;
    for(int i=0;i<18;i++) {
        int bit=codes[i];
        if(d->keys[bit/(8*sizeof(unsigned long))]&(1UL<<(bit%(8*sizeof(unsigned long)))))mask|=1<<i;
    }
    int hx=d->axes[ABS_HAT0X].value,hy=d->axes[ABS_HAT0Y].value;
    if(hx)mask|=1<<(hx>0?6:7);
    if(hy)mask|=1<<(hy>0?8:9);
    int lt=axis_level(d,ABS_BRAKE,0),rt=axis_level(d,ABS_GAS,0);
    if(lt>8)mask|=1<<14;
    if(rt>8)mask|=1<<15;
    int rx=d->axes[ABS_Z].maximum>d->axes[ABS_Z].minimum?ABS_Z:ABS_RX;
    int ry=d->axes[ABS_RZ].maximum>d->axes[ABS_RZ].minimum?ABS_RZ:ABS_RY;
    int v[]={mask,axis_level(d,ABS_X,1),axis_level(d,ABS_Y,1),
        axis_level(d,rx,1),axis_level(d,ry,1),lt,rt};
    if(d->sent && !memcmp(d->last,v,sizeof(v)))return 1;
    char line[90];
    int n=snprintf(line,sizeof(line),"D %d %d %d %d %d %d %d\n",v[0],v[1],v[2],v[3],v[4],v[5],v[6]);
    memcpy(d->last,v,sizeof(v));d->sent=1;
    return write(client,line,n)==n;
}
static void serve(int client,int watch) {
    int in=find_node("Lite2 Air Mouse Plus Mouse"),out=-1,keys=-1;
    int gamepad=in<0;
    struct gamepad_state d={0};
    if(gamepad)in=find_node(NULL);
    if(in<0)goto done;
    if(gamepad) {
        read_gamepad(in,&d);
        if(watch==2 && ioctl(in,EVIOCGRAB,1)<0)goto done;
    } else if(watch==2) {
        keys=find_node("Lite2 Air Mouse Plus Keyboard");
        if(keys<0 || ioctl(keys,EVIOCGRAB,1)<0 || ioctl(in,EVIOCGRAB,1)<0)goto done;
    } else if(!watch) {
        out=virtual_mouse();
        if(out<0 || ioctl(in,EVIOCGRAB,1)<0)goto done;
    }
    if(write(client,"OK\nG CAPS3\n",11)!=11)goto done;
    if(gamepad && !send_gamepad(client,&d))goto done;
    puts(gamepad?(watch==2?"Monitor captured Lite 2 gamepad":"Watching native Lite 2 gamepad"):watch==2?"Monitor captured Lite 2 mouse and keyboard":watch?"Watching native Lite 2 reports":"Lite 2 mouse redirected");fflush(stdout);
    struct profile p={.gx=100,.gy=100,.click_hold=1};
    for(int i=0;i<CURVE_SIZE;i++)p.pointer[i]=p.wheel[i]=1000;
    struct motion motion={0};
    int low=0,high=0,previous_buttons=-1;
    int preview_enabled=0,preview_profile=0;
    uint64_t wheel_notice_at=0;
    int last_wheel_direction=0;
    int last_axes[4]={999,999,999,999};
    uint64_t activity=now_ms();
    char lines[4096];
    size_t used=0;
    while(!stopped && now_ms()-activity<5000) {
        struct pollfd sources[2]={{.fd=client,.events=POLLIN},{.fd=in,.events=POLLIN}};
        int timeout=motion.lease_end?4:250;
        if(motion.wheel_until) {
            uint64_t now=now_ms();
            int wait=motion.wheel_due>now?(int)(motion.wheel_due-now):0;
            if(wait<timeout)timeout=wait;
        }
        int ready=poll(sources,2,timeout);
        if(!advance_wheel(out,&motion,now_ms(),0))goto done;
        if(!advance_pointer(out,&motion,now_ms()))goto done;
        if(ready<0){if(errno==EINTR)continue;break;}
        if((sources[0].revents|sources[1].revents)&(POLLHUP|POLLERR|POLLNVAL))break;
        if(sources[0].revents&POLLIN) {
            ssize_t got=read(client,lines+used,sizeof(lines)-used-1);
            if(got<=0)break;
            used+=got;lines[used]=0;
            char *start=lines,*end;
            while((end=strchr(start,'\n'))) {
                *end=0;
                if(!strcmp(start,"M 1") || !strcmp(start,"M 0")) {
                    preview_enabled=start[2]=='1';
                    if(watch!=2)goto done;
                    motion=(struct motion){0};
                } else if(watch==2 && !strncmp(start,"P ",2)) {
                    preview_profile=strcmp(start+2,"RAW")!=0;
                    if(preview_profile && !parse_profile(start+2,&p))goto done;
                    motion=(struct motion){0};
                } else if(strcmp(start,"H")!=0) {
                    if(watch || !parse_profile(start,&p))goto done;
                    motion.rx=motion.ry=motion.rwheel=0;
                    motion.wheel_phase=0;motion.wheel_until=0;motion.samples=0;motion.lease_end=0;
                    printf("Applied X=%d Y=%d pointer knots=%d wheel knots=%d\n",p.gx,p.gy,p.cursor_curve.count,p.scroll_curve.count);fflush(stdout);
                }
                if(write(client,"OK\n",3)!=3)goto done;
                activity=now_ms();start=end+1;
            }
            used-=start-lines;
            memmove(lines,start,used);
            if(used>=sizeof(lines)-1)break;
        }
        if(sources[1].revents&POLLIN) {
            struct input_event events[32];
            ssize_t got=read(in,events,sizeof(events));
            if(got<0 && errno==EAGAIN)continue;
            if(got<=0 || (size_t)got%sizeof(*events))break;
            for(size_t i=0;i<(size_t)got/sizeof(*events);i++) {
                struct input_event e=events[i];
                if(gamepad) {
                    if(e.type==EV_ABS && e.code<ABS_CNT)d.axes[e.code].value=e.value;
                    if(e.type==EV_KEY && e.code<=KEY_MAX) {
                        unsigned long bit=1UL<<(e.code%(8*sizeof(unsigned long)));
                        unsigned long *word=&d.keys[e.code/(8*sizeof(unsigned long))];
                        if(e.value)*word|=bit;else *word&=~bit;
                    }
                    if(e.type==EV_SYN && e.code==SYN_DROPPED)read_gamepad(in,&d);
                    if(e.type==EV_SYN && e.code==SYN_REPORT && !send_gamepad(client,&d))goto done;
                    continue;
                }
                if(!allowed(e))continue;
                if(e.type==EV_KEY && (!watch || (preview_enabled && preview_profile))) {
                    unsigned bit=1u<<(e.code-BTN_LEFT);
                    if(e.value==1 && !(motion.clicks&bit))motion.pressed_clicks|=bit;
                    if(!e.value && (motion.clicks&bit))motion.released_clicks|=bit;
                    if(e.value)motion.clicks|=bit;else motion.clicks&=~bit;
                }
                if(e.type==EV_SYN) {
                    int tag=(uint16_t)motion.source;
                    int extended=low>512 && high>512;
                    int x=0,y=0,rx=0,ry=0;
                    if(low<0 && high<0) {
                        // Timing frames preserve the full button mask and RX/RY.
                        // The replaced coarse axes were centered or are present in RX/RY.
                        low=1+(low&511);high=1+(high&511);
                        tag=(tag&0x700)|128;
                        motion.source=tag;
                    } else if(extended) {
                        x=monitor_axis(((low-1)>>9)-1);
                        y=monitor_axis(((high-1)>>9)-1);
                        rx=monitor_axis(tag>>11);
                        low=1+((low-1)&511);high=1+((high-1)&511);
                        tag &= 2047;
                        motion.source=tag;
                    }
                    if(tag>=1024 && tag<1792) {
                        if(preview_enabled && !preview_profile && (tag>>8)!=6 &&
                           !preview_delta(client,motion.dx,motion.dy))goto done;
                        if((tag>>8)==6) {x=motion.raw_x;y=motion.raw_y;}
                        ry=(tag&255)-128;
                        int axes[]={x,y,rx,ry};
                        if(memcmp(axes,last_axes,sizeof(axes))) {
                            char line[64];
                            int len=snprintf(line,sizeof(line),"T %d %d %d %d\n",x,y,rx,ry);
                            if(write(client,line,len)!=len)goto done;
                            memcpy(last_axes,axes,sizeof(axes));
                        }
                    }
                    int buttons=button_snapshot(low,high);
                    low=high=0;
                    if(buttons>=0 && buttons!=previous_buttons) {
                        char line[24];
                        int len=snprintf(line,sizeof(line),"B %d\n",buttons);
                        if(write(client,line,len)!=len)goto done;
                        if(previous_buttons<0){puts("18 physical buttons connected");fflush(stdout);}
                        previous_buttons=buttons;
                    }
                    uint64_t us=(uint64_t)e.time.tv_sec*1000000+e.time.tv_usec;
                    if(watch==2 && (motion.hires || motion.notches)) {
                        int direction=(motion.hires?motion.hires:motion.notches)>0?1:-1;
                        if(direction!=last_wheel_direction || us-wheel_notice_at>=120000) {
                            const char *notice=direction>0?"W 1\n":"W -1\n";
                            if(write(client,notice,strlen(notice))!=(ssize_t)strlen(notice))goto done;
                            wheel_notice_at=us;last_wheel_direction=direction;
                        }
                    }
                    if(preview_enabled && preview_profile) {
                        motion.preview_client=client;
                        if(!flush_motion(-1,&motion,&p,us))goto done;
                    } else if(watch)memset(&motion,0,sizeof(motion));
                    else if(!flush_motion(out,&motion,&p,us))goto done;
                } else if(e.type==EV_REL && e.code==REL_RX)motion.raw_x=e.value;
                else if(e.type==EV_REL && e.code==REL_RY)motion.raw_y=e.value;
                else if(e.type==EV_REL && e.code==REL_RZ)motion.source=e.value;
                else if(e.type==EV_REL && e.code==REL_Z)low=e.value;
                else if(e.type==EV_REL && e.code==REL_DIAL)high=e.value;
                else if(e.type==EV_REL && e.code==REL_X)motion.dx+=e.value;
                else if(e.type==EV_REL && e.code==REL_Y)motion.dy+=e.value;
                else if(e.type==EV_REL && e.code==REL_WHEEL)motion.notches+=e.value;
                else if(e.type==EV_REL && e.code==REL_WHEEL_HI_RES)motion.hires+=e.value;
                else if(!watch && !emit(out,e.type,e.code,e.value))goto done;
            }
        }
    }
done:
    if(keys>=0)close(keys);
    if(in>=0){if(watch!=1)ioctl(in,EVIOCGRAB,0);close(in);}
    if(out>=0){ioctl(out,UI_DEV_DESTROY);close(out);}
    close(client);
}
int main(int argc,char **argv) {
    if(argc!=2 || !is_hex_token(argv[1]))return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
    int server=listener();
    if(server<0){perror("loopback listener");return 1;}
    puts("Waiting for Lite 2 Control");fflush(stdout);
    while(!stopped) {
        int watch=0;
        int client=accept_app(server,argv[1],&watch);
        if(client>=0)serve(client,watch);
    }
    close(server);
    return 0;
}

