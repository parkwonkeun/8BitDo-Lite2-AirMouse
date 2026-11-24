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
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/wait.h>
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
static int ble_pad_name(const char *name) {
    return !strcmp(name,"Lite2 GamePAD") || !strcmp(name,"Lite2 GamePAD Gamepad") ||
           !strcmp(name,"Lite2 Pad") || !strcmp(name,"Lite2 Pad Gamepad");
}
static int physical_node(const char *node,const char *expected,int flags) {
    if(!numeric_suffix(node))return -1;
    int fd=open(node,flags|O_NONBLOCK|O_CLOEXEC);
    char name[128]={0};
    struct input_id id;
    if(fd<0)return -1;
    if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0 || ioctl(fd,EVIOCGID,&id)<0) { close(fd);return -1; }
    int ble_pad=!expected && ble_pad_name(name) && id.bustype==BUS_BLUETOOTH;
    int ble_mouse=expected && (!strcmp(expected,"Lite2 Air Mouse Mouse") || !strcmp(expected,"Lite2 BLE Mouse"));
    int ble_keyboard=expected && (!strcmp(expected,"Lite2 Air Mouse Keyboard") || !strcmp(expected,"Lite2 BLE Keyboard"));
    int matches=expected && !strcmp(name,expected);
    if(ble_mouse) {
        // Android may use the product name alone for the pointer collection.
        matches|=(strlen(expected)==strlen(name)+6 && !strncmp(expected,name,strlen(name)));
        unsigned long rel=0;
        matches=matches && ioctl(fd,EVIOCGBIT(EV_REL,sizeof(rel)),&rel)>=0 &&
                (rel&(1UL<<REL_X)) && (rel&(1UL<<REL_Y));
    }
    if((ble_mouse || ble_keyboard ? id.bustype!=BUS_BLUETOOTH : (!ble_pad && id.vendor!=0x2dc8)) ||
       (id.bustype!=BUS_BLUETOOTH && id.bustype!=BUS_USB) ||
       (expected?(!matches || (!ble_mouse && !ble_keyboard && id.product!=0x5112)):
        ((!ble_pad && !strcasestr(name,"lite 2")) || strcasestr(name,"air mouse")))) {
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
    const int keys[]={BTN_LEFT,BTN_RIGHT,BTN_MIDDLE,KEY_ENTER,KEY_BACK,KEY_HOMEPAGE,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_VOLUMEUP,KEY_VOLUMEDOWN};
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
    int axes[4][4];
    double stick_percent,wheel_percent,gx,gy;
    int source_buttons,trigger_action[2];
    int pointer_dead,wheel_dead;
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
    int wheel_delta;
    double wheel_interval,wheel_due;
    uint64_t wheel_until;
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
static int same_ble_peer(int mouse,int keyboard);
static int find_peer_node(const char *name,int flags,int peer) {
    DIR *dir=opendir("/dev/input");
    if(!dir)return -1;
    struct dirent *entry;
    int fd=-1;
    while((entry=readdir(dir))) {
        if(strncmp(entry->d_name,"event",5)!=0)continue;
        char node[300];
        snprintf(node,sizeof(node),"/dev/input/%s",entry->d_name);
        fd=physical_node(node,name,flags);
        if(fd>=0) {
            if(peer<0 || same_ble_peer(peer,fd))break;
            close(fd);fd=-1;
        }
    }
    closedir(dir);
    return fd;
}
static int find_node(const char *name,int flags) {return find_peer_node(name,flags,-1);}

static int device_added(int watch,int client,uint64_t deadline) {
    uint64_t now=now_ms();
    if(now>=deadline || stopped)return 0;
    int remaining=(int)(deadline-now);
    struct pollfd fds[]={{.fd=watch,.events=POLLIN},{.fd=client,.events=POLLIN}};
    int ready=poll(fds,2,watch<0 && remaining>40?40:remaining);
    if(ready<0 || fds[1].revents)return 0;
    if(fds[0].revents&POLLIN) {
        char events[4096];
        return read(watch,events,sizeof(events))>0;
    }
    return watch<0 && now_ms()<deadline;
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
enum { MAX_SPEED_PERCENT = 2000, MAX_STICK_PERCENT = 2000 };
static double slot_axis(const struct profile *p,int index,int value) {
    const int *a=p->axes[index];
    if(a[3]==a[0])return value; // Profiles from older clients.
    if(value<a[1])return 127.*fmax(-1.,(double)(value-a[1])/(a[1]-a[0]));
    if(value>a[2])return 127.*fmin(1.,(double)(value-a[2])/(a[3]-a[2]));
    return 0;
}
static int parse_profile(const char *line,struct profile *settings) {
    if(line[0]=='C' || line[0]=='F') {
        struct profile update={.click_hold=1,.wheel_percent=100,.stick_percent=100};
        const char *pos=line+1;
        double gx,gy;
        int axis_limit=line[0]=='F'?2*MAX_SPEED_PERCENT:MAX_SPEED_PERCENT;
        if(!number(&pos,0,axis_limit,&gx) || !number(&pos,0,axis_limit,&gy) ||
           !read_spline(&pos,&update.cursor_curve) || !read_spline(&pos,&update.scroll_curve))return 0;
        if(line[0]=='F') {
            double hold;
            if(!number(&pos,0,1,&hold) || hold!=floor(hold))return 0;
            update.click_hold=(int)hold;
            if(*pos) {
                for(int i=0;i<4;i++) {
                    int *a=update.axes[i];
                    for(int j=0;j<4;j++) {
                        double v;
                        if(!number(&pos,-128,127,&v) || floor(v)!=v)return 0;
                        a[j]=(int)v;
                    }
                    if(a[0]>=a[1] || a[1]>a[2] || a[2]>=a[3])return 0;
                }
                if(*pos) {
                    double speed;
                    if(!number(&pos,5,MAX_STICK_PERCENT,&speed))return 0;
                    update.stick_percent=speed;
                    for(int i=0;i<2;i++) {
                        double action;
                        if(!number(&pos,0,43,&action) || action!=floor(action))return 0;
                        update.trigger_action[i]=(int)action;
                    }
                    int has_wheel_gain=*pos!=0;
                    if(has_wheel_gain && !number(&pos,5,300,&update.wheel_percent))return 0;
                    if(*pos)return 0;
                    update.source_buttons=has_wheel_gain?2:1;
                }
            }
        } else if(*pos) {
            double flag;
            if(!number(&pos,0,1,&flag) || flag!=floor(flag))return 0;
            struct spline legacy_gyro;
            if(*pos && (!read_spline(&pos,&legacy_gyro) || *pos))return 0;
        }
        update.gx=gx;update.gy=gy;
        *settings=update;
        return 1;
    }
    if(strncmp(line,"P ",2))return 0;
    const char *pos=line+2;
    struct profile update={.wheel_percent=100,.stick_percent=100};
    for(int i=0;i<2+2*CURVE_SIZE;i++) {
        char *end;
        long n=strtol(pos,&end,10);
        if(end==pos || n<0 || n>(i<2?MAX_SPEED_PERCENT:8000))return 0;
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
// Send one signed wheel delta per tick while the right-stick sample is fresh.
static int advance_wheel(int out,struct motion *m,uint64_t now,int finish) {
    if(!m->wheel_until)return 1;
    if(finish || now>=m->wheel_until) {
        m->wheel_until=0;
        m->wheel_delta=0;
        return 1;
    }
    if(now<m->wheel_due)return 1;
    int units=m->wheel_delta;
    m->wheel_due+=m->wheel_interval;
    if(m->wheel_due<=now)m->wheel_due=now+m->wheel_interval;
    m->wheel_phase+=units;
    int steps=m->wheel_phase/120;
    m->wheel_phase-=steps*120;
    return emit(out,EV_REL,REL_WHEEL_HI_RES,units) &&
           (!steps || emit(out,EV_REL,REL_WHEEL,steps)) &&
           emit(out,EV_SYN,SYN_REPORT,0);
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
    double wheel_offset=0;
    double raw_ms=0;
    if(raw_frame) {
        if(!m->previous_raw){puts("r19 raw input connected");fflush(stdout);}
        int tag=m->source-1024;
        wheel_offset=-slot_axis(p,3,(tag & 255)-128);
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
        double x=slot_axis(p,0,m->raw_x), y=slot_axis(p,1,m->raw_y);
        double radius=hypot(x,y);
        double response=spline_output(&p->cursor_curve,radius/127.);
        double scale=radius>0 ? 2.44*response*(p->stick_percent/100.)/radius : 0;
        m->vx=scale*x;m->vy=scale*y;

    } else if(raw_frame && m->source==1) {
        double axis[]={m->raw_x,m->raw_y};
        correct_gyro(&m->zero,axis,raw_ms);
        double vx=hold?0:axis[0]*9./1024.,vy=hold?0:axis[1]*9./1024.;
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
        if(!gyro)gain*=p->stick_percent/100.;
        int x=quantize(m->dx*gain*(gyro?p->gx/100.:1.),&m->rx);
        int y=quantize(m->dy*gain*(gyro?p->gy/100.:1.),&m->ry);
        if(!pointer_delta(out,m,x,y))return 0;
        if(m->samples++==0) {
            printf("First movement in=(%d,%d) out=(%d,%d) X=%.2f Y=%.2f\n",m->dx,m->dy,x,y,p->gx,p->gy);
            fflush(stdout);
        }
    }
    if ((raw_frame && m->source==1) || (m->source==2 && p->cursor_curve.count)) {
        m->integrated_until=now;
        m->lease_end=(m->vx || m->vy)?now+hold_ms:0;
    }
    m->source=m->raw_x=m->raw_y=0;
    if(raw_frame && p->scroll_curve.count) {
        double response=spline_output(&p->scroll_curve,fabs(wheel_offset)/127.);
        int delta=response>0 ? (int)fmax(1.,round(response*120.)) : 0;
        if(wheel_offset<0)delta=-delta;
        if(!delta) {
            if(!advance_wheel(out,m,now,1))return 0;
        } else {
            if(!m->wheel_until || now>=m->wheel_until || delta*m->wheel_delta<0)
                m->wheel_due=now;
            m->wheel_until=now+45;
            m->wheel_delta=delta;
            m->wheel_interval=100000./(15.*p->wheel_percent);
            if(!advance_wheel(out,m,now,0))return 0;
        }
    } else if(m->hires || m->notches) {
        int units=m->hires?m->hires:m->notches*120;
        uint64_t elapsed=us-m->previous_wheel;
        double rate=elapsed && elapsed<=350000?fabs((double)units)/120.*1000000./elapsed:4.;
        double tilt=(22.+4.*fmax(0,rate-4.))/128.;
        double gain=p->scroll_curve.count?spline_gain(&p->scroll_curve,tilt):ratio(p->wheel,tilt,p->wheel_dead);
        int scaled=quantize(units*gain*p->wheel_percent/100.,&m->rwheel);
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
    int last[7],sent,ble_pad;
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
static const int gamepad_codes[]={BTN_START,BTN_THUMBL,BTN_THUMBR,BTN_SELECT,BTN_NORTH,BTN_WEST,
        BTN_DPAD_RIGHT,BTN_DPAD_LEFT,BTN_DPAD_DOWN,BTN_DPAD_UP,BTN_TL,BTN_TR,
        BTN_EAST,BTN_SOUTH,BTN_TL2,BTN_TR2,KEY_RECORD,BTN_MODE};
static int output_index(const struct gamepad_state *d,int i) {
    if(d->ble_pad && (i==4 || i==5 || i==12 || i==13))return i^1;
    return i;
}
static int gamepad_code(const struct gamepad_state *d,int i) {
    return d->ble_pad && i==16 ? BTN_GAMEPAD+15 : gamepad_codes[i];
}
static int send_gamepad(int client,struct gamepad_state *d) {
    // Same scanner order as the S-mode report so saved button assignments are shared.
    int mask=0;
    for(int i=0;i<18;i++) {
        int bit=gamepad_code(d,output_index(d,i));
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
/* Emit the held union so a trigger releasing a duplicated click cannot
   release a click still held by Y/X or the other trigger. */
static int trigger_output(int out,const struct profile *p,int physical,int held,
                          unsigned *sent,int *last_triggers) {
    static const int keys[]={BTN_LEFT,BTN_RIGHT,BTN_MIDDLE,0,0,KEY_ENTER,KEY_BACK,
                             KEY_HOMEPAGE,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT};
    unsigned wanted=physical&7;
    int triggers=(held>>14)&3;
    int down=*last_triggers<0?0:triggers&~*last_triggers;
    *last_triggers=triggers;
    int scroll=0;
    for(int i=0;i<2;i++) {
        int action=p->trigger_action[i];
        if((triggers&(1<<i)) && action>=2 && action<=13 && keys[action-2])wanted|=1u<<(action-2);
        if(down&(1<<i))scroll+=(action==5)-(action==6);
    }
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++)
        if(((wanted^*sent)&(1u<<i)) && !emit(out,EV_KEY,keys[i],(wanted>>i)&1))return 0;
    *sent=wanted;
    return !scroll || (emit(out,EV_REL,REL_WHEEL,scroll) && emit(out,EV_REL,REL_WHEEL_HI_RES,scroll*120));
}
/* On Android builds which restrict writing evdev, the same authorized shell
   session can use the Bluetooth HID host service. Only the installed helper
   class is launched; there is no command input from the client. */
static int hid_output_worker(pid_t *pid) {
    int fds[2];
    if(pipe2(fds,O_CLOEXEC)<0)return -1;
    *pid=fork();
    if(*pid==0) {
        if(dup2(fds[0],STDIN_FILENO)<0)_exit(1);
        close(fds[0]);close(fds[1]);
        execl("/system/bin/sh","sh","-c",
              "pkg=$(pm path local.lite2.control | head -n 1); "
              "export CLASSPATH=\"${pkg#package:}\"; "
              "exec app_process / local.lite2.control.SlotHidOutput",(char *)NULL);
        _exit(1);
    }
    close(fds[0]);
    if(*pid<0){close(fds[1]);return -1;}
    return fds[1];
}
// The LE HID profile has separate mouse and keyboard event nodes. Match the
// physical peer when Android exposes its identity (two pads may share a name).
static int same_ble_peer(int mouse,int keyboard) {
    char a[128]={0},b[128]={0};
    if(ioctl(mouse,EVIOCGUNIQ(sizeof(a)),a)>0 && a[0] &&
       ioctl(keyboard,EVIOCGUNIQ(sizeof(b)),b)>0 && b[0])return strcmp(a,b)==0;
    if(ioctl(mouse,EVIOCGPHYS(sizeof(a)),a)>0 && a[0] &&
       ioctl(keyboard,EVIOCGPHYS(sizeof(b)),b)>0 && b[0])return strcmp(a,b)==0;
    return 0;
}
static int ble_key_mask(unsigned code) {
    switch(code) {
        case BTN_LEFT:return 1<<5;
        case BTN_RIGHT:return 1<<4;
        case KEY_VOLUMEDOWN:return 1<<10;
        case KEY_VOLUMEUP:return 1<<11;
        case KEY_RIGHT:return 1<<6;
        case KEY_LEFT:return 1<<7;
        case KEY_DOWN:return 1<<8;
        case KEY_UP:return 1<<9;
        case KEY_ENTER:return 1<<12;
        case KEY_BACK:return 1<<13;
        case KEY_HOMEPAGE:return 1<<16;
        default:return 0;
    }
}
static int ble_buttons(int client,int mask,int source) {
    char line[32];
    int n=snprintf(line,sizeof(line),"B %d %d\n",mask,source);
    return write(client,line,n)==n;
}
/* Keep the last real snapshot across slot/monitor handovers. The initial B
   message establishes a baseline for the new client, never a press edge. */
static char button_peer[256];
static int peer_held;
static void select_button_peer(int fd) {
    char id[128]={0},name[96]={0},key[256];
    if(ioctl(fd,EVIOCGUNIQ(sizeof(id)),id)<=0 || !id[0])ioctl(fd,EVIOCGPHYS(sizeof(id)),id);
    ioctl(fd,EVIOCGNAME(sizeof(name)),name);
    snprintf(key,sizeof(key),"%s/%s",name,id);
    if(strcmp(key,button_peer)){strcpy(button_peer,key);peer_held=0;}
}
enum { RAW_BUTTONS=18 };
struct raw_bindings {
    int map[RAW_BUTTONS], captured,held,gamepad,shape_sticks;
    struct profile stick_profile;
    unsigned sent;
    uint64_t power_down;
};
static const int native_action[RAW_BUTTONS]={0,0,0,0,3,2,13,12,11,10,40,39,7,8,0,0,9,9};
static const int raw_output_codes[]={BTN_LEFT,BTN_RIGHT,BTN_MIDDLE,KEY_ENTER,KEY_BACK,KEY_HOMEPAGE,
                                     KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_VOLUMEUP,KEY_VOLUMEDOWN};
static int raw_output_bit(int action) {
    if(action>=2 && action<=4)return 1<<(action-2);
    if(action>=7 && action<=13)return 1<<(action-4);
    if(action==39 || action==40)return 1<<(action-29);
    return 0;
}
static int parse_bindings(const char *line,int map[RAW_BUTTONS]) {
    for(int i=0;i<RAW_BUTTONS;i++){
        double value;
        if(!number(&line,0,43,&value) || value!=(int)value)return 0;
        map[i]=(int)value;
    }
    return !*line;
}
static int needs_raw_output(const int map[RAW_BUTTONS]) {
    for(int i=0;i<RAW_BUTTONS;i++) {
        if(!map[i] || map[i]==native_action[i])continue;
        if(native_action[i] || raw_output_bit(map[i]) || map[i]==5 || map[i]==6)return 1;
    }
    return 0;
}
static int raw_keys(int out,struct raw_bindings *raw,int held) {
    int down=held&~raw->held,up=raw->held&~held,scroll=0,home_pulse=0;
    unsigned wanted=0;
    for(int i=0;i<RAW_BUTTONS;i++) {
        int bit=1<<i,action=raw->map[i]?raw->map[i]:(raw->gamepad?0:native_action[i]);
        if(!raw->gamepad && i==17 && !raw->map[i]) {
            if(down&bit)raw->power_down=now_ms();
            if((up&bit) && raw->power_down && now_ms()-raw->power_down<750)home_pulse=1;
            continue;
        }
        if(held&bit)wanted|=raw_output_bit(action);
        if(down&bit)scroll+=(action==5)-(action==6);
    }
    raw->held=held;
    for(unsigned i=0;i<sizeof(raw_output_codes)/sizeof(*raw_output_codes);i++)
        if(((wanted^raw->sent)&(1u<<i)) && !emit(out,EV_KEY,raw_output_codes[i],(wanted>>i)&1))return 0;
    raw->sent=wanted;
    if(home_pulse && !(wanted&raw_output_bit(9)) &&
       (!emit(out,EV_KEY,KEY_HOMEPAGE,1) || !emit(out,EV_SYN,SYN_REPORT,0) || !emit(out,EV_KEY,KEY_HOMEPAGE,0)))return 0;
    return !scroll || (emit(out,EV_REL,REL_WHEEL,scroll) && emit(out,EV_REL,REL_WHEEL_HI_RES,120*scroll));
}
static int raw_gamepad_device(int in,const struct gamepad_state *d) {
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0)return -1;
    struct uinput_setup setup={.id={BUS_VIRTUAL,0x2dc8,0x5112,1}};
    strcpy(setup.name,"Lite 2 Control mapped gamepad");
    unsigned long supported[KEY_MAX/(8*sizeof(unsigned long))+1]={0};
    if(ioctl(in,EVIOCGBIT(EV_KEY,sizeof(supported)),supported)<0 ||
       ioctl(fd,UI_SET_EVBIT,EV_KEY)<0 || ioctl(fd,UI_SET_EVBIT,EV_ABS)<0)goto fail;
    for(int i=0;i<RAW_BUTTONS;i++) {
        unsigned code=gamepad_code(d,i);
        supported[code/(8*sizeof(long))]|=1UL<<(code%(8*sizeof(long)));
    }
    for(int i=0;i<=KEY_MAX;i++)if(supported[i/(8*sizeof(long))]&(1UL<<(i%(8*sizeof(long)))))
        if(ioctl(fd,UI_SET_KEYBIT,i)<0)goto fail;
    for(int i=0;i<ABS_CNT;i++)if(d->axes[i].maximum>d->axes[i].minimum) {
        struct uinput_abs_setup axis={.code=i,.absinfo=d->axes[i]};
        if(ioctl(fd,UI_SET_ABSBIT,i)<0 || ioctl(fd,UI_ABS_SETUP,&axis)<0)goto fail;
    }
    if(ioctl(fd,UI_DEV_SETUP,&setup)<0 || ioctl(fd,UI_DEV_CREATE)<0)goto fail;
    return fd;
fail:
    close(fd);return -1;
}
static int mapped_gamepad(int fd,const struct gamepad_state *d,const struct raw_bindings *raw) {
    static const unsigned targets[]={13,12,4,5,9,8,7,6,10,11,14,15,1,2,0,3,16,17};
    int held=d->last[0],buttons=0;
    int axes[]={ABS_X,ABS_Y,d->axes[ABS_Z].maximum>d->axes[ABS_Z].minimum?ABS_Z:ABS_RX,
               d->axes[ABS_RZ].maximum>d->axes[ABS_RZ].minimum?ABS_RZ:ABS_RY};
    int shaped[4]={0};
    if(raw->shape_sticks)for(int pair=0;pair<4;pair+=2){
        const struct profile *p=&raw->stick_profile;
        double x=slot_axis(p,pair,d->last[pair+1]),y=slot_axis(p,pair+1,d->last[pair+2]);
        double radius=hypot(x,y);
        double gain=radius?spline_output(pair?&p->scroll_curve:&p->cursor_curve,radius/127.)/radius:0;
        for(int k=0;k<2;k++){
            const struct input_absinfo *axis=&d->axes[axes[pair+k]];
            double normalized=fmax(-1.,fmin(1.,(k?y:x)*gain));
            shaped[pair+k]=axis->minimum+(int)lround((1.+normalized)*.5*(axis->maximum-axis->minimum));
        }
    }
    for(int i=0;i<RAW_BUTTONS;i++)if(held&(1<<i)) {
        int a=raw->map[i];
        if(!a)buttons|=1<<output_index(d,i);
        else if(a>=20 && a<=37)buttons|=1<<targets[a-20];
    }
    for(int i=0;i<RAW_BUTTONS;i++) {
        int axis=i==6 || i==7?ABS_HAT0X:i==8 || i==9?ABS_HAT0Y:i==14?ABS_BRAKE:i==15?ABS_GAS:-1;
        if(axis>=0 && d->axes[axis].maximum>d->axes[axis].minimum)continue;
        if(!emit(fd,EV_KEY,gamepad_code(d,i),(buttons>>i)&1))return 0;
    }
    for(int i=0;i<ABS_CNT;i++) {
        const struct input_absinfo *axis=&d->axes[i];
        if(axis->maximum<=axis->minimum)continue;
        int value=axis->value;
        if(raw->shape_sticks)for(int j=0;j<4;j++)if(i==axes[j])value=shaped[j];
        if(i==ABS_HAT0X)value=((buttons>>6)&1)-((buttons>>7)&1);
        if(i==ABS_HAT0Y)value=((buttons>>8)&1)-((buttons>>9)&1);
        if(i==ABS_BRAKE || i==ABS_GAS) {
            int index=i==ABS_BRAKE?14:15;
            if(raw->map[index])value=axis->minimum;
            if((buttons&(1<<index)) && (raw->map[index] || !(held&(1<<index))))value=axis->maximum;
        }
        if(!emit(fd,EV_ABS,i,value))return 0;
    }
    return emit(fd,EV_SYN,SYN_REPORT,0);
}
// r56 assigns the same controller two addresses with the last byte XORed by C0.
static int other_mode_address(const char *a,const char *b) {
    unsigned x,y;
    return strlen(a)==17 && strlen(b)==17 && !strncasecmp(a,b,15) &&
           sscanf(a+15,"%2x",&x)==1 && sscanf(b+15,"%2x",&y)==1 && (x^y)==0xc0;
}
static int other_mode_peer(int old,int next) {
    char a[64]={0},b[64]={0};
    return ioctl(old,EVIOCGUNIQ(sizeof(a)),a)>0 && ioctl(next,EVIOCGUNIQ(sizeof(b)),b)>0 &&
           other_mode_address(a,b);
}
struct monitor_handoff { int mouse,keys; };
static void discard_handoff(struct monitor_handoff *h) {
    if(h->mouse>=0)close(h->mouse);
    if(h->keys>=0)close(h->keys);
    h->mouse=h->keys=-1;
}
// Acquire newly created collections before the old gamepad disappears. Keep the
// grabbed file descriptions across the monitor client's protocol renegotiation.
static int capture_new_mouse(int watch,int gamepad,struct monitor_handoff *h) {
    union {struct inotify_event align; char bytes[4096];} events;
    ssize_t n=read(watch,events.bytes,sizeof(events.bytes));
    for(ssize_t pos=0;n>0 && pos<n;) {
        const struct inotify_event *event=(const void *)(events.bytes+pos);
        pos+=sizeof(*event)+event->len;
        if(!event->len || strncmp(event->name,"event",5))continue;
        char path[300];
        snprintf(path,sizeof(path),"/dev/input/%s",event->name);
        int *target=&h->mouse,fd=physical_node(path,"Lite2 Air Mouse Mouse",O_RDONLY);
        if(fd<0){target=&h->keys;fd=physical_node(path,"Lite2 Air Mouse Keyboard",O_RDONLY);}
        if(fd<0)continue;
        if(*target<0 && other_mode_peer(gamepad,fd) && ioctl(fd,EVIOCGRAB,1)==0)*target=fd;
        else close(fd);
    }
    return h->mouse>=0;
}
static void serve(int client,int watch,struct monitor_handoff *handoff) {
    int in=-1,out=-1,keys=-1,pad_out=-1,hotplug=-1;
    int ble=0,gamepad=0,feedback=-1,phase=0;
    struct raw_bindings raw={0};
    uint64_t refresh_feedback=0,deadline=now_ms()+1900;
    pid_t worker=-1;int worker_pipe=-1;
    struct gamepad_state d={0};
    const char *ble_keyboard=NULL;
    int inherited=0;
    if(watch!=2)discard_handoff(handoff);
    if(handoff->mouse>=0) {
        in=handoff->mouse; keys=handoff->keys; handoff->mouse=handoff->keys=-1;
        inherited=ble=1;ble_keyboard="Lite2 Air Mouse Keyboard";
    }
    if(watch==2) {
        hotplug=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
        if(hotplug>=0 && inotify_add_watch(hotplug,"/dev/input",IN_CREATE|IN_MOVED_TO|IN_ATTRIB)<0) {
            close(hotplug);hotplug=-1;
        }
    }
    if(!inherited)do {
        in=find_node("Lite2 Air Mouse Plus Mouse",O_RDONLY);
        if(in<0) {
            ble_keyboard="Lite2 Air Mouse Keyboard";
            in=find_node("Lite2 Air Mouse Mouse",O_RDONLY);
            if(in<0) {
                ble_keyboard="Lite2 BLE Keyboard";
                in=find_node("Lite2 BLE Mouse",O_RDONLY);
            }
            ble=in>=0;
        }
        if(in<0){in=find_node(NULL,O_RDONLY);gamepad=in>=0;}
    } while(in<0 && watch==2 && device_added(hotplug,client,deadline));
    if(in<0)goto done;
    unsigned long rels=0;
    int ble_snapshot=ble && ioctl(in,EVIOCGBIT(EV_REL,sizeof(rels)),&rels)>=0 && (rels & (1UL<<REL_DIAL));
    if(ble) {
        if(watch==2 && !inherited && ioctl(in,EVIOCGRAB,1)<0)goto done;
        if(keys<0 && handoff->keys>=0) {
            if(same_ble_peer(in,handoff->keys)){keys=handoff->keys;handoff->keys=-1;}
            else discard_handoff(handoff);
        }
        if(keys<0) {
            do {
                keys=find_peer_node(ble_keyboard,O_RDONLY,in);
            } while(keys<0 && watch==2 && device_added(hotplug,client,deadline));
            if(keys<0 || (watch==2 && ioctl(keys,EVIOCGRAB,1)<0))goto done;
        }
        if(!watch) {
            out=virtual_mouse();
            if(out<0 || ioctl(in,EVIOCGRAB,1)<0)goto done;
        }
    } else if(gamepad) {
        char name[128]={0};
        d.ble_pad=ioctl(in,EVIOCGNAME(sizeof(name)),name)>=0 && ble_pad_name(name);
        read_gamepad(in,&d);
        if(watch==2 && ioctl(in,EVIOCGRAB,1)<0)goto done;
    } else if(watch==2) {
        keys=find_node("Lite2 Air Mouse Plus Keyboard",O_RDONLY);
        if(keys<0 || ioctl(keys,EVIOCGRAB,1)<0 || ioctl(in,EVIOCGRAB,1)<0)goto done;
    } else if(!watch) {
        out=virtual_mouse();
        if(out<0 || ioctl(in,EVIOCGRAB,1)<0)goto done;
    }
    if(hotplug>=0 && !(gamepad && d.ble_pad)){close(hotplug);hotplug=-1;}
    if(!gamepad && !ble && watch!=1) {
        feedback=find_node("Lite2 Air Mouse Plus Keyboard",O_RDWR);
        unsigned long leds=0;
        if(feedback>=0 && (ioctl(feedback,EVIOCGBIT(EV_LED,sizeof(leds)),&leds)<0 ||
           (leds&((1UL<<LED_KANA)|(1UL<<LED_COMPOSE)))!=((1UL<<LED_KANA)|(1UL<<LED_COMPOSE)))) {
            close(feedback);feedback=-1;
        }
    }
    if(!gamepad && !ble && watch!=1 && feedback<0)worker_pipe=hid_output_worker(&worker);
    if(!gamepad)select_button_peer(in);
    const char hello[]="OK\nG DCURVE1\nG RAW1\nG CAPS5\nG CAPS4\nG CAPS3\n";
    if(write(client,hello,sizeof(hello)-1)!=(ssize_t)sizeof(hello)-1)goto done;
    if(ble && watch==2) {
        char address[32]={0},notice[40];
        if(ioctl(in,EVIOCGUNIQ(sizeof(address)),address)>0 && strlen(address)==17) {
            int n=snprintf(notice,sizeof(notice),"U %s\n",address);
            if(write(client,notice,n)!=n)goto done;
        }
    }
    if(gamepad && write(client,"G GAMEPAD\n",10)!=10)goto done;
    if(!gamepad && !ble_buttons(client,peer_held,0))goto done;
    if(gamepad && !send_gamepad(client,&d))goto done;
    puts(gamepad?(watch==2?"Monitor captured Lite 2 gamepad":"Watching native Lite 2 gamepad"):watch==2?"Monitor captured Lite 2 mouse and keyboard":watch?"Watching native Lite 2 reports":"Lite 2 mouse redirected");fflush(stdout);
    struct profile p={.wheel_percent=100,.stick_percent=100,.gx=100,.gy=100,.click_hold=1};
    for(int i=0;i<CURVE_SIZE;i++)p.pointer[i]=p.wheel[i]=1000;
    struct motion motion={0};
    int low=0,high=0,previous_buttons=-1,last_triggers=-1;
    int ble_held=ble_snapshot?0:peer_held,ble_previous_source=-1;
    unsigned sent_keys=0;
    int preview_enabled=0,preview_profile=0;
    uint64_t wheel_notice_at=0;
    int last_wheel_direction=0;
    int last_axes[4]={999,999,999,999};
    int sampling=0; // 1 for left, 2 for right.
    uint64_t sample_at=0,button_notice_at=0;
    uint64_t activity=now_ms();
    char lines[4096];
    size_t used=0;
    while(!stopped && now_ms()-activity<5000) {
        if((feedback>=0 || worker_pipe>=0) && now_ms()>=refresh_feedback) {
            phase=!phase;
            if(worker_pipe>=0) {
                int bits=16|(phase<<3)|(!watch && p.trigger_action[0]==14)|
                         ((!watch && p.trigger_action[1]==14)<<1);
                char state[8];
                int n=snprintf(state,sizeof(state),"%d\n",bits);
                if(write(worker_pipe,state,n)!=n){close(worker_pipe);worker_pipe=-1;}
            } else if(!emit(feedback,EV_LED,LED_NUML,!watch && p.trigger_action[0]==14) ||
               !emit(feedback,EV_LED,LED_CAPSL,!watch && p.trigger_action[1]==14) ||
               !emit(feedback,EV_LED,LED_KANA,1) || !emit(feedback,EV_LED,LED_COMPOSE,phase) ||
               !emit(feedback,EV_SYN,SYN_REPORT,0))goto done;
            refresh_feedback=now_ms()+650;
        }
        if(sampling && gamepad && now_ms()-sample_at>=50) {
            // Unchanged ABS axes retain their last kernel state.
            d.sent=0;
            if(!send_gamepad(client,&d))goto done;
            sample_at=now_ms();
        }
        struct pollfd sources[]={{.fd=client,.events=POLLIN},{.fd=in,.events=POLLIN},
                                 {.fd=(ble || raw.captured)?keys:-1,.events=POLLIN},{.fd=hotplug,.events=POLLIN}};
        int timeout=motion.lease_end?4:sampling?50:250;
        if(motion.wheel_until) {
            uint64_t now=now_ms();
            int wait=motion.wheel_due>now?(int)ceil(motion.wheel_due-now):0;
            if(wait<timeout)timeout=wait;
        }
        int ready=poll(sources,4,timeout);
        if(!advance_wheel(out,&motion,now_ms(),0))goto done;
        if(!advance_pointer(out,&motion,now_ms()))goto done;
        if(ready<0){if(errno==EINTR)continue;break;}
        if(sources[3].revents&POLLIN && capture_new_mouse(hotplug,in,handoff)) {
            puts("Monitor captured incoming S mouse during gamepad handoff");fflush(stdout);
            break;
        }
        if((sources[0].revents|sources[1].revents|sources[2].revents)&(POLLHUP|POLLERR|POLLNVAL))break;
        if(sources[0].revents&POLLIN) {
            ssize_t got=read(client,lines+used,sizeof(lines)-used-1);
            if(got<=0)break;
            used+=got;lines[used]=0;
            char *start=lines,*end;
            while((end=strchr(start,'\n'))) {
                *end=0;
                if(watch==2 && (!strcmp(start,"K 1") || !strcmp(start,"K 2"))) {
                    sampling=start[2]-'0';
                } else if(!strcmp(start,"M 1") || !strcmp(start,"M 0")) {
                    preview_enabled=start[2]=='1';
                    if(watch!=2)goto done;
                    motion=(struct motion){0};
                } else if(watch==2 && !strncmp(start,"P ",2)) {
                    preview_profile=strcmp(start+2,"RAW")!=0;
                    if(preview_profile && !parse_profile(start+2,&p))goto done;
                    motion=(struct motion){0};
                } else if(gamepad && watch!=2 && start[0]=='J') {
                    int map[RAW_BUTTONS]={0};
                    char *shape=strstr(start," |");
                    if(shape){
                        *shape=0;
                        if(!parse_profile(shape+2,&raw.stick_profile))goto done;
                    }
                    raw.shape_sticks=shape!=NULL;
                    if(start[1] && !parse_bindings(start+1,map))goto done;
                    int capture=raw.shape_sticks;
                    for(int i=0;i<RAW_BUTTONS;i++)capture|=map[i]!=0;
                    if(capture && !raw.captured) {
                        pad_out=raw_gamepad_device(in,&d);out=virtual_mouse();
                        if(pad_out<0 || out<0 || ioctl(in,EVIOCGRAB,1)<0)goto done;
                        read_gamepad(in,&d);
                        if(!send_gamepad(client,&d))goto done;
                        raw.held=d.last[0];
                    } else if(!capture && raw.captured) {
                        ioctl(in,EVIOCGRAB,0);
                        close(pad_out);pad_out=-1;
                        close(out);out=-1;
                        raw=(struct raw_bindings){0};
                    }
                    raw.gamepad=1;raw.captured=capture;
                    memcpy(raw.map,map,sizeof(map));
                    if(capture && (!mapped_gamepad(pad_out,&d,&raw) || !raw_keys(out,&raw,d.last[0]) || !emit(out,EV_SYN,SYN_REPORT,0)))goto done;
                } else if(watch==1 && !strncmp(start,"R ",2)) {
                    int map[RAW_BUTTONS];
                    if(!parse_bindings(start+1,map))goto done;
                    int capture=!gamepad && needs_raw_output(map);
                    if(capture && !raw.captured) {
                        if(keys<0)keys=find_node("Lite2 Air Mouse Plus Keyboard",O_RDONLY);
                        if(keys<0 || (out=virtual_mouse())<0 || ioctl(keys,EVIOCGRAB,1)<0 || ioctl(in,EVIOCGRAB,1)<0)goto done;
                        raw.held=peer_held;
                    } else if(!capture && raw.captured) {
                        ioctl(out,UI_DEV_DESTROY);close(out);out=-1;
                        ioctl(keys,EVIOCGRAB,0);ioctl(in,EVIOCGRAB,0);
                        raw=(struct raw_bindings){0};
                    }
                    raw.captured=capture;
                    memcpy(raw.map,map,sizeof(map));
                    if(capture && (!raw_keys(out,&raw,raw.held) || !emit(out,EV_SYN,SYN_REPORT,0)))goto done;
                } else if(strcmp(start,"H")!=0) {
                    if(watch || !parse_profile(start,&p))goto done;
                    refresh_feedback=0;
                    motion.rx=motion.ry=motion.rwheel=0;
                    motion.samples=0;motion.lease_end=0;
                    printf("Applied X=%.2f Y=%.2f pointer knots=%d wheel knots=%d\n",p.gx,p.gy,p.cursor_curve.count,p.scroll_curve.count);fflush(stdout);
                }
                if(write(client,"OK\n",3)!=3)goto done;
                activity=now_ms();start=end+1;
            }
            used-=start-lines;
            memmove(lines,start,used);
            if(used>=sizeof(lines)-1)break;
        }
        for(int node=1;node<=2;node++) if(sources[node].revents&POLLIN) {
            struct input_event events[32];
            ssize_t got=read(sources[node].fd,events,sizeof(events));
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
                    if(e.type==EV_SYN && e.code==SYN_REPORT) {
                        if(!send_gamepad(client,&d))goto done;
                        if(raw.captured && (!mapped_gamepad(pad_out,&d,&raw) || !raw_keys(out,&raw,d.last[0]) ||
                           !emit(out,EV_SYN,SYN_REPORT,0)))goto done;
                    } else if(raw.captured && e.type==EV_KEY) {
                        int assigned=0;
                        for(int k=0;k<RAW_BUTTONS;k++)assigned|=e.code==gamepad_code(&d,k);
                        if(!assigned && !emit(pad_out,EV_KEY,e.code,e.value))goto done;
                    }
                    continue;
                }
                if(ble && !ble_snapshot && e.type==EV_KEY) {
                    int bit=ble_key_mask(e.code);
                    if(e.value)ble_held|=bit;else ble_held&=~bit;
                    if(ble_held!=previous_buttons && !ble_buttons(client,ble_held,0))goto done;
                    peer_held=previous_buttons=ble_held;
                }
                if(raw.captured && !ble_snapshot && ble && e.type==EV_KEY &&
                   (!raw_keys(out,&raw,ble_held) || !emit(out,EV_SYN,SYN_REPORT,0)))goto done;
                if(node==2 || !allowed(e))continue;
                if(raw.captured && e.type==EV_REL && (e.code==REL_X || e.code==REL_Y || e.code==REL_WHEEL ||
                   e.code==REL_HWHEEL || e.code==REL_WHEEL_HI_RES || e.code==REL_HWHEEL_HI_RES)) {
                    if(!emit(out,e.type,e.code,e.value))goto done;
                    continue;
                }
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
                        int usable=!sampling || (extended && (sampling==2 || (tag>>8)==6));
                        int refresh=sampling && now_ms()-sample_at>=50;
                        if(usable && (refresh || memcmp(axes,last_axes,sizeof(axes)))) {
                            sample_at=now_ms();
                            char line[64];
                            int len=snprintf(line,sizeof(line),"T %d %d %d %d\n",x,y,rx,ry);
                            if(write(client,line,len)!=len)goto done;
                            memcpy(last_axes,axes,sizeof(axes));
                        }
                    }
                    if(ble && preview_enabled && !preview_delta(client,motion.dx,motion.dy))goto done;
                    int buttons=button_snapshot(low,high);
                    int ble_source=0;
                    if(ble && ble_snapshot && high>0) {
                        unsigned stamp=(unsigned)high-1;
                        buttons=stamp&0x3ffff;
                        ble_source=(stamp>>18)&3;
                    }
                    if(raw.captured && ((buttons>=0 && !raw_keys(out,&raw,buttons)) || !emit(out,EV_SYN,SYN_REPORT,0)))goto done;
                    low=high=0;
                    uint64_t notice_ms=now_ms();
                    if(buttons>=0 && (buttons!=previous_buttons || (ble && ble_source!=ble_previous_source) ||
                       (!watch && buttons && notice_ms-button_notice_at>=20))) {
                        button_notice_at=notice_ms;
                        char line[24];
                        int source=tag>=1024 && tag<1792 ? ((tag>>8)==6 ? 2 : 1) : 0;
                        if(p.source_buttons==2 && source && (fabs(slot_axis(&p,3,ry))>19 ||
                           (extended && fabs(slot_axis(&p,2,rx))>19)))source=3;
                        if(ble){source=ble_source;ble_previous_source=source;}
                        int len=p.source_buttons ? snprintf(line,sizeof(line),"B %d %d\n",buttons,source)
                                                 : snprintf(line,sizeof(line),"B %d\n",buttons);
                        if(write(client,line,len)!=len)goto done;
                        if(previous_buttons<0){puts("18 physical buttons connected");fflush(stdout);}
                        if(watch==2 && previous_buttons>=0 && ((previous_buttons^buttons)&(1<<16))){
                            puts(buttons&(1<<16)?"Monitor star pressed":"Monitor star released");fflush(stdout);
                        }
                        peer_held=previous_buttons=buttons;
                    }
                    if(!gamepad && !watch && !trigger_output(out,&p,motion.clicks,
                       previous_buttons<0?0:previous_buttons,&sent_keys,&last_triggers))goto done;
                    uint64_t us=(uint64_t)e.time.tv_sec*1000000+e.time.tv_usec;
                    if(watch==2 && (motion.hires || motion.notches)) {
                        int direction=(motion.hires?motion.hires:motion.notches)>0?1:-1;
                        if(direction!=last_wheel_direction || us-wheel_notice_at>=120000) {
                            const char *notice=direction>0?"W 1\n":"W -1\n";
                            if(write(client,notice,strlen(notice))!=(ssize_t)strlen(notice))goto done;
                            wheel_notice_at=us;last_wheel_direction=direction;
                        }
                    }
                    if(!ble && preview_enabled && preview_profile) {
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
                else if(!watch && e.type!=EV_KEY && !emit(out,e.type,e.code,e.value))goto done;
            }
        }
    }
done:
    if(hotplug>=0)close(hotplug);
    if(worker_pipe>=0)close(worker_pipe);
    if(worker>0) {
        for(int i=0;i<20 && waitpid(worker,NULL,WNOHANG)==0;i++)usleep(20000);
        if(waitpid(worker,NULL,WNOHANG)==0){kill(worker,SIGTERM);waitpid(worker,NULL,0);}
    }
    if(feedback>=0) {
        emit(feedback,EV_LED,LED_NUML,0);
        emit(feedback,EV_LED,LED_CAPSL,0);
        emit(feedback,EV_LED,LED_KANA,0);
        emit(feedback,EV_LED,LED_COMPOSE,0);
        emit(feedback,EV_SYN,SYN_REPORT,0);
        close(feedback);
    }
    if(keys>=0)close(keys);
    if(in>=0){if(watch!=1)ioctl(in,EVIOCGRAB,0);close(in);}
    if(pad_out>=0)close(pad_out);
    if(out>=0){ioctl(out,UI_DEV_DESTROY);close(out);}
    close(client);
}
int main(int argc,char **argv) {
    if(argc!=2 || !is_hex_token(argv[1]))return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
    int server=listener();
    if(server<0){perror("loopback listener");return 1;}
    puts("Waiting for Lite 2 Control");fflush(stdout);
    struct monitor_handoff handoff={-1,-1};
    while(!stopped) {
        int watch=0;
        int client=accept_app(server,argv[1],&watch);
        if(client>=0)serve(client,watch,&handoff);
        else discard_handoff(&handoff);
    }
    discard_handoff(&handoff);
    close(server);
    return 0;
}

