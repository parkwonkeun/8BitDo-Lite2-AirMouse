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
static int replay_stick(unsigned period, int speed) {
    int pipes[2];assert(pipe2(pipes,O_NONBLOCK)==0);
    struct profile p={.gx=150,.gy=100,.stick_percent=speed,
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
static int replay_wheel(unsigned report_period,int offset,int percent) {
    struct profile p={.wheel_percent=percent,.scroll_curve={.count=2,.x={0,100},.y={0,50},.slope={.5,.5}}};
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
                assert(t<=1000 && abs(e.value)==60);total+=e.value;events++;
            }
            if(e.type==EV_REL && e.code==REL_WHEEL)legacy+=e.value;
        }
    }
    assert(events==15*percent/100 && legacy==total/120);
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
            assert(x==(held?0:90) && y==(held?0:-45));
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
static void check_raw_remap(void) {
    struct raw_bindings raw={0};
    raw.map[0]=16;raw.map[3]=38;
    assert(!needs_raw_output(raw.map));
    raw.map[4]=2;raw.map[5]=2;
    assert(needs_raw_output(raw.map));
    int fds[2];assert(pipe2(fds,O_NONBLOCK)==0);
    struct input_event e;
    assert(raw_keys(fds[1],&raw,1<<4));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==BTN_LEFT && e.value==1);
    assert(raw_keys(fds[1],&raw,3<<4) && raw_keys(fds[1],&raw,1<<5));
    assert(read(fds[0],&e,sizeof(e))<0 && errno==EAGAIN);
    assert(raw_keys(fds[1],&raw,0));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==BTN_LEFT && e.value==0);
    raw.map[5]=5;
    assert(raw_keys(fds[1],&raw,1<<5) && raw_keys(fds[1],&raw,1<<5));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_WHEEL && e.value==1);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_WHEEL_HI_RES && e.value==120);
    assert(read(fds[0],&e,sizeof(e))<0 && errno==EAGAIN);
    struct gamepad_state d={0};
    d.axes[ABS_X]=(struct input_absinfo){.minimum=-32768,.maximum=32767,.value=-12345};
    raw.map[13]=21;d.last[0]=1<<13;
    assert(mapped_gamepad(fds[1],&d,&raw));
    int button=0,axis=0;
    while(read(fds[0],&e,sizeof(e))==sizeof(e)) {
        if(e.type==EV_ABS && e.code==ABS_X)axis=e.value;
        if(e.type==EV_KEY && e.value){assert(e.code==BTN_EAST);button++;}
    }
    assert(axis==-12345 && button==1);
    // On the BLE gamepad the printed B is in the Xbox A (south) position.
    d.ble_pad=1;
    d.last[0]=(1<<12)|(1<<16);
    assert(mapped_gamepad(fds[1],&d,&raw));
    int south=0,star=0;
    while(read(fds[0],&e,sizeof(e))==sizeof(e))if(e.type==EV_KEY && e.value) {
        if(e.code==BTN_SOUTH)south++;
        else if(e.code==BTN_GAMEPAD+15)star++;
        else assert(0);
    }
    assert(south==1 && star==1);
    const int physical_face[]={13,12,4,5}; // Printed A, B, X, Y
    const int android_face[]={BTN_SOUTH,BTN_EAST,BTN_NORTH,BTN_WEST};
    for(int ble=0;ble<=1;ble++)for(int xbox=0;xbox<=1;xbox++){
        d.ble_pad=ble;
        for(int i=0;i<4;i++)raw.map[physical_face[i]]=20+(i^xbox);
        raw.map[16]=41;
        for(int i=0;i<4;i++){
            d.last[0]=(1<<physical_face[i])|(1<<16);
            assert(mapped_gamepad(fds[1],&d,&raw));
            int pressed=0;
            while(read(fds[0],&e,sizeof(e))==sizeof(e))if(e.type==EV_KEY && e.value){
                assert(e.code==android_face[i^xbox]);
                pressed++;
            }
            assert(pressed==1);
        }
    }
    close(fds[0]);close(fds[1]);
}
int main(int argc,char **argv) {
    assert(other_mode_address("02:00:00:00:01:5b","02:00:00:00:01:9B"));
    assert(!other_mode_address("02:00:00:00:01:5b","02:00:00:00:02:9B"));
    assert(!other_mode_address("02:00:00:00:01:5b","02:00:00:00:01:DB"));
    check_raw_remap();
    struct profile measured={0};
    const char *calibrated="F 100 100 3 0 0 0 50 50 1 100 100 1 3 0 0 0 50 50 1 100 100 1 1 -80 10 12 92 -90 -9 -7 70 -100 3 4 96 -64 5 7 88";
    assert(parse_profile(calibrated,&measured));
    assert(slot_axis(&measured,0,11)==0 && slot_axis(&measured,1,-8)==0);
    assert(slot_axis(&measured,0,92)==127 && slot_axis(&measured,0,-80)==-127);
    assert(slot_axis(&measured,0,52)==63.5 && slot_axis(&measured,0,120)==127);
    assert(slot_axis(&measured,3,6)==0 && slot_axis(&measured,3,-64)==-127);
    assert(slot_axis(&measured,2,96)==127);
    char full_profile[256];
    snprintf(full_profile,sizeof(full_profile),"%s 2000 42 43",calibrated);
    assert(parse_profile(full_profile,&measured) && measured.stick_percent==2000);
    snprintf(full_profile,sizeof(full_profile),"%s 2001 42 43",calibrated);
    assert(!parse_profile(full_profile,&measured));
    if(argc==2 && !strcmp(argv[1],"--rest-replay")) {replay_gyro_zero();return 0;}
    test_monitor_pointer();
    test_gyro_cadence();
    test_click_hold();
    assert(replay_wheel(13,127,100)==900 && replay_wheel(32,-127,100)==-900);
    assert(replay_wheel(13,127,300)==2700 && replay_wheel(32,-127,300)==-2700);
    const char *curves=" 3 0 0 0 15 0 0 100 40 0 3 0 0 0 15 0 0 100 40 0 0";
    char line[160];
    struct profile maximum={0};
    snprintf(line,sizeof(line),"C 500 500%s",curves);
    assert(parse_profile(line,&maximum) && maximum.gx==500 && maximum.gy==500);
    snprintf(line,sizeof(line),"F 4000 2000%s",curves);
    assert(parse_profile(line,&maximum) && maximum.gx==4000 && maximum.gy==2000);
    snprintf(line,sizeof(line),"F 2000 4000%s",curves);
    assert(parse_profile(line,&maximum) && maximum.gx==2000 && maximum.gy==4000);
    snprintf(line,sizeof(line),"F 4001 2000%s",curves);
    assert(!parse_profile(line,&maximum));
    snprintf(line,sizeof(line),"C 2000 2000%s",curves);
    assert(parse_profile(line,&maximum) && maximum.gx==2000 && maximum.gy==2000);
    snprintf(line,sizeof(line),"C 2001 2000%s",curves);
    assert(!parse_profile(line,&maximum));
    int fds[2]; assert(pipe2(fds,O_NONBLOCK)==0);
    struct motion gyro={.source=1,.dx=15,.dy=-10};
    assert(flush_motion(fds[1],&gyro,&maximum,1000000));
    struct input_event e;
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_X && e.value==300);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_Y && e.value==-200);
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
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_Y && e.value==-45);
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
    raw.source=1408; raw.raw_x=1024; raw.raw_y=-1024;
    assert(flush_motion(fds[1],&raw,&maximum,1120000));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    fake_ms+=10;
    assert(advance_pointer(fds[1],&raw,fake_ms));
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_X && e.value==450);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_Y && e.value==-450);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    // A curve capped at 50% emits 60 units immediately.
    snprintf(line,sizeof(line),"C 100 100 3 0 0 0 15 0 0 100 50 0 3 0 0 0 15 0 0 100 50 0 0 3 0 0 1 50 50 1 100 100 1");
    assert(parse_profile(line,&maximum));
    raw.source=1536; raw.raw_x=127; raw.raw_y=0; raw.notches=1;
    assert(flush_motion(fds[1],&raw,&maximum,1130000));
    assert(fabs(raw.vx-1.22)<.0001 && raw.vy==0);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.code==REL_WHEEL_HI_RES && e.value==60);
    assert(read(fds[0],&e,sizeof(e))==sizeof(e) && e.type==EV_SYN);
    close(fds[0]); close(fds[1]);
    for(int mask=0;mask<262144;mask++)
        assert(button_snapshot((mask&511)+1,(mask>>9)+1)==mask);
    assert(button_snapshot(0,1)==-1 && button_snapshot(1,513)==-1);
    int uniform=replay_stick(12,100);
    assert(uniform==replay_stick(28,100));
    assert(abs(replay_stick(12,1000)-2*replay_stick(12,500))<=1);
    assert(abs(replay_stick(12,2000)-2*replay_stick(12,1000))<=1);
    struct motion m={.dx=1,.previous_motion=10000,.speed=12.5};
    assert(fabs(measured_speed(&m,90000)-12.5)<.0001);
    // D-mode RAW passes through both sticks. The two profile curves transform them independently.
    struct gamepad_state pad={0};
    struct raw_bindings mapping={0};
    int axis_codes[]={ABS_X,ABS_Y,ABS_Z,ABS_RZ};
    int physical[]={255,128,0,128};
    for(int i=0;i<4;i++){
        pad.axes[axis_codes[i]]=(struct input_absinfo){.minimum=0,.maximum=255,.value=physical[i]};
        pad.last[i+1]=i==0?127:i==2?-127:0;
    }
    mapping.stick_profile.cursor_curve=(struct spline){.count=2,.x={0,100},.y={0,50},.slope={.5,.5}};
    mapping.stick_profile.scroll_curve=(struct spline){.count=2,.x={0,100},.y={0,100},.slope={1,1}};
    for(int apply=0;apply<=1;apply++){
        mapping.shape_sticks=apply;
        int events[2];assert(pipe(events)==0);
        assert(mapped_gamepad(events[1],&pad,&mapping));
        close(events[1]);
        int axes_read=0;
        while(read(events[0],&e,sizeof(e))==sizeof(e))if(e.type==EV_ABS){
            for(int i=0;i<4;i++)if(e.code==axis_codes[i]){
                assert(e.value==(apply && i==0?191:physical[i]));
                axes_read++;
            }
        }
        close(events[0]);assert(axes_read==4);
    }
    puts("PASS");
}

