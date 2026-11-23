#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include <time.h>
static unsigned long fake_ms;
static int test_clock(clockid_t id,struct timespec *t) {
    (void)id;t->tv_sec=fake_ms/1000;t->tv_nsec=(fake_ms%1000)*1000000;
    return 0;
}
#define clock_gettime test_clock
#define main bridge_main
#include "../bridge/lite2-pointer-bridge.c"
#undef main

// Fixed physical tilt with irregular Bluetooth reports, on the tablet clock.
static int replay_stick(unsigned period) {
    int pipes[2];assert(pipe2(pipes,O_NONBLOCK)==0);
    struct profile p={.gx=150,.gy=100,
        .cursor_curve={.count=3,.x={0,10,100},.y={0,0,100},.slope={0,0,1.111111}}};
    struct motion m={0};
    int total=0,jumps=0;
    unsigned prev=0,max_gap=0;
    for(unsigned i=0;i<=1100;i+=4) {
        fake_ms=2000+i;
        assert(advance_pointer(pipes[1],&m,fake_ms));
        if((i<1000 && i%period==0) || i==1000) {
            m.source=2;m.raw_x=i==1000?0:32;
            assert(flush_motion(pipes[1],&m,&p,fake_ms*1000));
        }
        struct input_event event;
        while(read(pipes[0],&event,sizeof(event))==sizeof(event)) {
            if(event.type==EV_REL && event.code==REL_X){
                assert(i<=1000 && event.value>0);
                total+=event.value;jumps++;
                if(prev && i-prev>max_gap)max_gap=i-prev;
                prev=i;
            }
        }
    }
    close(pipes[0]);close(pipes[1]);
    printf("stick reports=%ums distance=%d events=%d max gap=%ums\n",period,total,jumps,max_gap);
    assert(total>0 && max_gap<=20);
    return total;
}
// Replay a stationary two-axis capture, optionally adding a known slow turn.
static void replay_gyro_zero(void) {
    struct gyro_zero zero={0};
    double dt,x,y,elapsed=0,still=0,positive=0,negative=0;
    while(scanf("%lf %lf %lf",&dt,&x,&y)==3) {
        elapsed+=dt;
        double turn=elapsed>=2500 && elapsed<4000?4:elapsed>=5000 && elapsed<6500?-4:0;
        double axis[]={x+turn,y+turn};
        correct_gyro(&zero,axis,dt);
        if(turn>0)positive+=axis[0]*dt;
        else if(turn<0)negative+=axis[0]*dt;
        else if(elapsed<2500 || elapsed>7000)still+=fabs(axis[0])+fabs(axis[1]);
    }
    printf("elapsed=%.0f still=%.2f positive=%.2f negative=%.2f\n",elapsed,still,positive,negative);fflush(stdout);
    assert(zero.ready && elapsed>7200 && still==0 && positive>3000 && negative< -3000);
    puts("PASS: rest capture and sustained +4/-4 turns");
}
static int replay_wheel(unsigned report_period,int offset) {
    struct profile p={.scroll_curve={.count=2,.x={0,100},.y={0,50},.slope={.5,.5}}};
    struct motion m={0};
    int fd[2];assert(pipe2(fd,O_NONBLOCK)==0);
    int total=0,legacy=0,events=0;
    for(unsigned t=0;t<=1100;t++) {
        fake_ms=4000+t;
        assert(advance_wheel(fd[1],&m,fake_ms,0));
        if((t<1000 && t%report_period==0) || t==1000){
            m.source=1024+128-(t==1000?0:offset);
            assert(flush_motion(fd[1],&m,&p,fake_ms*1000));
        }
        struct input_event e;
        while(read(fd[0],&e,sizeof(e))==sizeof(e)) {
            if(e.type==EV_REL && e.code==REL_WHEEL_HI_RES) {
                assert(t<=1000 && abs(e.value)<=15);total+=e.value;events++;
            }
            if(e.type==EV_REL && e.code==REL_WHEEL)legacy+=e.value;
        }
    }
    assert(events==125 && legacy==total/120);
    close(fd[0]);close(fd[1]);return total;
}
// Hold a calibrated rate over repeated radio gaps, then reverse and stop.
static void test_gyro_cadence(void) {
    struct profile p={.gx=100,.gy=100};
    struct motion m={0};
    int fds[2];assert(pipe2(fds,O_NONBLOCK)==0);
    int total=0,last=0,events=0,max_gap=0;
    for(int t=0;t<=1080;t+=4) {
        fake_ms=10000+t;
        assert(advance_pointer(fds[1],&m,fake_ms));
        // 60 ms gaps after speed has settled, including after reversal.
        int in_gap=(t>=200 && t<240) || (t>=700 && t<740);
        if(t<=1000 && !in_gap && t%20==0) {
            m.source=1408;
            m.raw_x=t==1000?0:t<500?1024:-1024;
            assert(flush_motion(fds[1],&m,&p,fake_ms*1000));
        }
        struct input_event e;
        while(read(fds[0],&e,sizeof(e))==sizeof(e)) {
            if(e.type!=EV_REL || e.code!=REL_X)continue;
            assert(t>0 && t<=1000 && e.value==(t<=500?36:-36));
            if(t-last>max_gap)max_gap=t-last;
            last=t;total+=e.value;events++;
        }
    }
    assert(events==250 && total==0 && max_gap==4);
    m.source=1408;m.raw_x=1024;m.raw_y=0;
    assert(flush_motion(fds[1],&m,&p,fake_ms*1000));
    assert(m.lease_end==fake_ms+45);
    fake_ms+=10;m.source=1408;m.raw_x=1024;
    assert(flush_motion(fds[1],&m,&p,fake_ms*1000));
    assert(m.lease_end==fake_ms+65);
    fake_ms+=10;m.source=1408;m.raw_x=-1024;
    assert(flush_motion(fds[1],&m,&p,fake_ms*1000));
    assert(m.lease_end==fake_ms+45);
    fake_ms+=100;
    assert(advance_pointer(fds[1],&m,fake_ms) && !m.lease_end);
    close(fds[0]);close(fds[1]);
}
static void test_click_hold(void) {
    const char *knots="3 0 0 0 15 0 0 100 40 0";
    for(int enabled=0;enabled<=1;enabled++) {
        struct profile profile={0};
        char line[160];
        snprintf(line,sizeof(line),"F 100 100 %s %s %d",knots,knots,enabled);
        assert(parse_profile(line,&profile) && profile.click_hold==enabled);
        int fd[2];assert(pipe2(fd,O_NONBLOCK)==0);
        struct motion m={0};
        for(int sample=0;sample<=25;sample++) {
            // Tap, release, then hold for a full drag.
            int press=sample==0 || sample==3;
            m.pressed_clicks=press;m.clicks=sample==0 || sample>=3;
            m.source=1408;m.raw_x=1024;m.raw_y=-512;
            fake_ms=1000+sample*10;
            assert(flush_motion(fd[1],&m,&profile,fake_ms*1000));
            fake_ms+=10;
            assert(advance_pointer(fd[1],&m,fake_ms));
            int x=0,y=0;
            struct input_event e;
            while(read(fd[0],&e,sizeof(e))==sizeof(e)){
                if(e.type==EV_REL && e.code==REL_X)x+=e.value;
                if(e.type==EV_REL && e.code==REL_Y)y+=e.value;
            }
            int held=enabled && (sample==0 || (sample>=3 && sample<23));
            assert(x==(held?0:90) && y==(held?0:-20));
        }
        close(fd[0]);close(fd[1]);
    }
}
static void test_monitor_pointer(void) {
    int pair[2];assert(socketpair(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK,0,pair)==0);
    char line[32]={0};
    struct profile p={.gx=150,.gy=50};
    struct motion m={.preview_client=pair[0],.source=1,.dx=20,.dy=-16};
    assert(flush_motion(-1,&m,&p,1000000));
    assert(read(pair[1],line,sizeof(line))==8 && !strcmp(line,"G 30 -8\n"));
    assert(preview_delta(pair[0],-13,7));
    assert(read(pair[1],line,sizeof(line))==8 && !strcmp(line,"G -13 7\n"));
    m.last_source=2;
    assert(pointer_delta(-1,&m,140,-30));
    assert(preview_delta(pair[0],0,0));
    assert(read(pair[1],line,sizeof(line))<0 && errno==EAGAIN);
    close(pair[0]);close(pair[1]);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--rest-replay")) {replay_gyro_zero();return 0;}
    test_monitor_pointer();
    test_gyro_cadence();
    test_click_hold();
    assert(replay_wheel(13,127)==1800 && replay_wheel(32,-127)==-1800);
    const char *curves=" 3 0 0 0 15 0 0 100 40 0 3 0 0 0 15 0 0 100 40 0 0";
    char line[160];
    struct profile maximum={0};
    snprintf(line,sizeof(line),"C 500 500%s",curves);
    assert(parse_profile(line,&maximum) && maximum.gx==500 && maximum.gy==500);
    snprintf(line,sizeof(line),"C 501 500%s",curves);
    assert(!parse_profile(line,&maximum));
    int fds[2]; assert(pipe2(fds,O_NONBLOCK)==0);
    struct motion gyro={.source=1,.dx=15,.dy=-10};
    assert(flush_motion(fds[1],&gyro,&maximum,1000000));
    struct input_event e;
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_X && e.value==75);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_Y && e.value==-50);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    snprintf(line,sizeof(line),"C 100 100%s",curves);
    assert(parse_profile(line,&maximum));
    gyro.source=1;gyro.dx=15;gyro.dy=-10;
    assert(flush_motion(fds[1],&gyro,&maximum,1010000));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_X && e.value==15);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_Y && e.value==-10);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    snprintf(line,sizeof(line),"C 100 100%s 3 0 0 0.5 50 25 0.5 100 50 0.5",curves);
    assert(parse_profile(line,&maximum));
    struct motion raw={.source=1408,.dx=900,.dy=900,.raw_x=1024,.raw_y=-512};
    assert(flush_motion(fds[1],&raw,&maximum,1100000));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    fake_ms+=10;
    assert(advance_pointer(fds[1],&raw,fake_ms));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_X && e.value==90);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_Y && e.value==-20);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    snprintf(line,sizeof(line),"C 0 0%s",curves);
    assert(parse_profile(line,&maximum));
    raw.source=1408; raw.raw_x=2048; raw.raw_y=2048;
    assert(flush_motion(fds[1],&raw,&maximum,1105000));
    while(read(fds[0],&e,sizeof(e))==sizeof(e)) assert(e.type!=EV_REL);
    raw.source=1152; raw.dx=15; raw.dy=-10;
    assert(flush_motion(fds[1],&raw,&maximum,1110000));
    while(read(fds[0],&e,sizeof(e))==sizeof(e)) assert(e.type!=EV_REL);
    snprintf(line,sizeof(line),"C 500 500%s",curves);
    assert(parse_profile(line,&maximum));
    raw.source=1408; raw.raw_x=1024; raw.raw_y=-512;
    assert(flush_motion(fds[1],&raw,&maximum,1120000));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    fake_ms+=10;
    assert(advance_pointer(fds[1],&raw,fake_ms));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_X && e.value==450);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_Y && e.value==-100);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    // Full positive wheel tilt at 50% output yields 18 high-resolution
    // units in 10 ms. The firmware wheel notch must not be applied again.
    snprintf(line,sizeof(line),"C 100 100 3 0 0 0 15 0 0 100 50 0 3 0 0 0 15 0 0 100 50 0 0 3 0 0 1 50 50 1 100 100 1");
    assert(parse_profile(line,&maximum));
    raw.source=1536; raw.raw_x=127; raw.raw_y=0; raw.notches=1;
    assert(flush_motion(fds[1],&raw,&maximum,1130000));
    assert(fabs(raw.vx-1.22)<.0001 && raw.vy==0);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    assert(advance_wheel(fds[1],&raw,now_ms()+10,0));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_WHEEL_HI_RES && e.value==18);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    close(fds[0]); close(fds[1]);
    for(int mask=0;mask<262144;mask++)
        assert(button_snapshot((mask&511)+1,(mask>>9)+1)==mask);
    assert(button_snapshot(0,1)==-1 && button_snapshot(1,513)==-1);
    int uniform=replay_stick(12);
    assert(uniform==replay_stick(28));
    struct motion m={.dx=1,.previous_motion=10000,.speed=12.5};
    assert(fabs(measured_speed(&m,90000)-12.5)<.0001);
    puts("PASS");
}

