#include "mouse_core.h"
#include "ble_controls_profile.h"

#define B(a) (*(volatile uint8_t *)(a))
#define W(a) (*(volatile uint32_t *)(a))
#define RADIO 0xc0000u
enum { SDP=0x4683, GATT=0x4975, GATT_PTR=0x448e, SDP_PTR=0x4547 };
#ifndef BLE_MONITOR
#define DB_SIZE sizeof(db)
#endif
#ifdef BLE_DUAL
enum { AD_LEN=29, SCAN_LEN=18, NAME_LEN=16 };
#else
enum { AD_LEN=28, SCAN_LEN=17, NAME_LEN=10 };
#endif
typedef struct {
    uint8_t backup[DB_SIZE], prior_ad[AD_LEN], prior_scan[SCAN_LEN], prior_name[NAME_LEN];
    uint32_t tick, blocked_at;
    int16_t x, y;
    int8_t wheel;
    uint8_t installed, started, keys, media, clicks, blocked, home, power_armed;
    uint16_t power_ms;
#ifdef BLE_MONITOR
    uint32_t last_stamp, stamp_tick;
#endif
#ifdef BLE_GAMEPAD
    uint32_t pad_buttons,pad_axes,pad_time;
#ifdef BLE_DUAL
    uint8_t advertise_pending,switching;
#endif
#endif
} Controls;
#define S ((Controls *)0x2fb80)
_Static_assert(sizeof(Controls)<=0x27c,"reserved BLE state");
#ifdef BLE_MONITOR
#define COPY_SECTION ".monitor_led"
#else
#define COPY_SECTION ".earlytext"
#endif
static __attribute__((noinline,section(COPY_SECTION))) void copy_bytes(volatile uint8_t *to, const volatile uint8_t *from,unsigned n) {
    while(n--) *to++=*from++;
}
static void pointer(unsigned address,unsigned value) {
    B(RADIO+address)=value;
    B(RADIO+address+1)=value>>8;
}
static unsigned read_pointer(unsigned address) {
    return B(RADIO+address) | ((unsigned)B(RADIO+address+1)<<8);
}
#ifdef BLE_GAMEPAD
#define LAST_BLE_MODE 1
#else
#define LAST_BLE_MODE 0
#endif
#ifdef BLE_RECONNECT
/* At power-on a zero return selects connectable advertising. The native
   retry path also calls this lookup, which is only for saved Classic hosts. */
#ifdef BLE_GAMEPAD
__attribute__((noinline,section(".pad_code")))
#endif
int ble_saved_host(void) {
    if (B(0x20184)<=LAST_BLE_MODE) return 0;
    return ((int (*)(void))0x01021cb5)();
}
#define BLE_BOOT_ALLOWED 1
#else
#define BLE_BOOT_ALLOWED (W(0x2fff8)==0x20221031)
#endif

#ifdef BLE_GAMEPAD
__attribute__((noinline,section(".pad_code")))
#endif
void ble_start(void) {
    S->installed=S->started=0;
#ifdef BLE_DUAL
    S->advertise_pending=S->switching=0;
#endif
#ifdef BLE_MONITOR
    B(0x2fdfc)=0;
#endif
    ((void (*)(void))0x0102c84d)();
}
#ifdef BLE_MONITOR
/* The four saved byte arrays use the same order as the radio profile. */
static void exchange_profile(int restore) {
    static const uint16_t ranges[][2]={{SDP,DB_SIZE},{0x4341,AD_LEN},{0x4361,SCAN_LEN},{0x4381,NAME_LEN}};
    uint8_t *saved=(uint8_t *)S;
#ifdef BLE_STICKS
    unsigned position=0;
#else
    const uint8_t *profile=radio_profile;
#endif
    for(unsigned i=0;i<4;i++) {
        volatile uint8_t *radio=(volatile uint8_t *)(RADIO+ranges[i][0]);
        for(unsigned n=ranges[i][1];n;n--,radio++,saved++) {
#ifdef BLE_STICKS
            uint8_t value=position<sizeof(profile_prefix)?profile_prefix[position]:radio_profile[position-sizeof(profile_prefix)];
            position++;
#else
            uint8_t value=*profile++;
#endif
            if(restore) *radio=*saved;
            else { *saved=*radio; *radio=value; }
        }
    }
}
#endif
#ifdef BLE_GAMEPAD
#ifdef BLE_DUAL
#define PAD_PROFILE_SECTION ".pad_code"
#else
#define PAD_PROFILE_SECTION ".pad_extra"
#endif
static __attribute__((noinline, section(PAD_PROFILE_SECTION))) void pad_profile(void) {
    copy_bytes((void *)(RADIO+SDP+PAD_TAIL),pad_tail,sizeof(pad_tail));
#ifdef BLE_DUAL
    static const uint8_t label[17] __attribute__((section(".pad_label")))="\x0e\x09" "Lite2 GamePAD";
    copy_bytes((void *)0xc434d,label,17);
    copy_bytes((void *)0xc4362,label,17);
    B(0xc4381)=13;
    copy_bytes((void *)0xc4382,label+2,15);
#else
    static const uint32_t names[]={RADIO+SDP+PAD_NAME,0xc4355,0xc436a,0xc4388};
    for(unsigned i=0;i<4;i++) copy_bytes((void *)names[i],(const uint8_t *)"Pad",3);
#endif
}
#endif
int ble_visibility(unsigned inquiry,unsigned page,unsigned le) {
    int (*visibility)(unsigned,unsigned,unsigned)=(void *)0x0101ab8d;
#ifdef BLE_GAMEPAD
    unsigned mode=B(0x20184);
    if(S->installed && S->installed!=mode+1) {
        visibility(0,0,0);
        pointer(GATT_PTR,GATT);
        exchange_profile(1);
        pointer(SDP_PTR,SDP);
        S->installed=S->started=0;
    }
    if(mode<=LAST_BLE_MODE && BLE_BOOT_ALLOWED && inquiry && page &&
#else
    if(!B(0x20184) && BLE_BOOT_ALLOWED && inquiry && page &&
#endif
       B(0x20152)!=2 && (S->installed || (read_pointer(GATT_PTR)==GATT && read_pointer(SDP_PTR)==SDP))) {
        if(!S->installed) {
#ifdef BLE_MONITOR
            visibility(0,0,0);
            pointer(SDP_PTR,GATT+200);
            exchange_profile(0);
            pointer(GATT_PTR,SDP);
#else
            static const uint8_t advertisement[28] __attribute__((section(".earlydata")))={3,2,1,5,3,25,0xc2,3,3,3,0x12,0x18,10,9,'L','i','t','e','2',' ','B','L','E'};
            static const uint8_t label[] __attribute__((section(".earlydata")))={31,10,9,'L','i','t','e','2',' ','B','L','E',0,0,0,0,0};
            visibility(0,0,0);
            copy_bytes(S->prior_ad,(void *)0xc4341,sizeof(S->prior_ad));
            copy_bytes(S->prior_scan,(void *)0xc4361,sizeof(S->prior_scan));
            copy_bytes(S->prior_name,(void *)0xc4381,sizeof(S->prior_name));
            copy_bytes(S->backup,(void *)(RADIO+SDP),sizeof(db));
            pointer(SDP_PTR,GATT+200); /* Original ATT table ends with two zeros. */
            copy_bytes((void *)(RADIO+SDP),db,sizeof(db));
            pointer(GATT_PTR,SDP);
            copy_bytes((void *)0xc4341,advertisement,sizeof(advertisement));
            copy_bytes((void *)0xc4361,label,sizeof(label));
            B(0xc4381)=9;
            copy_bytes((void *)0xc4382,label+3,9);
#endif
#ifdef BLE_GAMEPAD
            if(mode==1) {
                pad_profile();
                B(RADIO+SDP+PAD_APPEARANCE)=B(0xc4347)=0xc4;
            }
            S->installed=mode+1;
#ifdef BLE_DUAL
            S->switching=0;
#endif
#else
            S->installed=1;
#endif
        }
        return visibility(0,0,1);
    }
#ifdef BLE_GAMEPAD
    if(S->installed && (mode>LAST_BLE_MODE || !BLE_BOOT_ALLOWED)) {
#else
    if(S->installed && (B(0x20184) || !BLE_BOOT_ALLOWED)) {
#endif
        visibility(0,0,0);
        pointer(GATT_PTR,GATT);
#ifdef BLE_MONITOR
        exchange_profile(1);
        pointer(SDP_PTR,SDP);
#else
        copy_bytes((void *)(RADIO+SDP),S->backup,sizeof(db));
        pointer(SDP_PTR,SDP);
        copy_bytes((void *)0xc4341,S->prior_ad,sizeof(S->prior_ad));
        copy_bytes((void *)0xc4361,S->prior_scan,sizeof(S->prior_scan));
        copy_bytes((void *)0xc4381,S->prior_name,sizeof(S->prior_name));
#endif
        S->installed=S->started=0;
    }
    if(S->installed && (page || inquiry)) return visibility(0,0,le);
    return visibility(inquiry,page,le);
}
#ifdef BLE_DUAL
__attribute__((noinline,section(".dual_more")))
#endif
static int notify(unsigned handle,const uint8_t *bytes,unsigned n) {
#ifdef BLE_MONITOR
    enum { capacity=12 };
#else
    enum { capacity=8 };
#endif
#ifdef BLE_MONITOR
    uint8_t payload[capacity], req[7];
    payload[0]=handle;payload[1]=0;req[0]=0;req[1]=9;req[2]=n+2;
#else
    uint8_t payload[capacity]={handle,0},req[7]={0,9,n+2};
#endif
    copy_bytes(payload+2,bytes,n);
    ((void (*)(const void *,void *))0x01018303)(payload,req+3);
    /* The native writer checks free FIFO bytes and copies the entire payload
       before returning. The separate command-result flag is not a link credit. */
    return ((int (*)(void *))0x010223b9)(req);
}
#ifdef BLE_GAMEPAD
static __attribute__((noinline,section(".pad_code"))) void pad_poll(unsigned buttons,unsigned axes,unsigned now) {
    if(S->pad_buttons==buttons && S->pad_axes==axes && now-S->pad_time<250) return;
    uint8_t report[9]={0};
    ((void (*)(unsigned,unsigned,unsigned,uint8_t *))0x0101dbaf)(buttons,axes,0,report);
    unsigned face=report[0];
    report[0]=(face&0xe4)|((face&9)<<1)|((face&18)>>1);
    if(buttons&(1u<<16)) report[1]|=0x80;
    if(notify(MOUSE_HANDLE,report,sizeof(report))) {
        S->pad_buttons=buttons; S->pad_axes=axes; S->pad_time=now;
    }
}
#endif
static unsigned centered(unsigned raw) {
    unsigned out=0;
    for(unsigned i=0;i<4;i++) {
        int sample=(raw>>(8*i))&255, lo=B(0x0102d714+i), hi=B(0x0102d718+i), v=128;
        if(sample<lo) v-=(lo-sample)*128/lo;
        if(sample>hi) v+=(sample-hi)*127/(255-hi);
        out|=(unsigned)v<<(8*i);
    }
    return out;
}
static int bounded(int v,int max) { return v>max?max:v < -max?-max:v; }
static int axis(const uint8_t *p) { return (int16_t)(p[0]|p[1]<<8); }
void ble_poll(void) {
    ((void (*)(void))0x0102d4cd)();
    #ifdef BLE_GAMEPAD
    if(!S->installed || S->installed!=1u+B(0x20184) ||
#else
    if(!S->installed || B(0x20184) ||
#endif
       ((unsigned (*)(void))0x01020a81)()) return;
#ifdef BLE_DUAL
    if(S->switching)return;
    if(S->advertise_pending) {
        S->advertise_pending=0;
        ble_visibility(1,1,0);
    }
#endif
    unsigned period=B(0x2022b);
    if(period<5 || period>100) period=10;
    uint32_t now=((unsigned (*)(unsigned))0x01027bad)(period);
    uint32_t dt=now-S->tick;
    if(S->started && dt<8) return;
    if(!S->started) {
#ifdef BLE_GAMEPAD
        S->pad_time=now-250;
#endif
        S->keys=S->media=S->clicks=S->home=S->power_armed=0;
        S->power_ms=0; S->blocked=0;S->started=1;dt=8;
        S->x=S->y=S->wheel=0;
#ifdef BLE_MONITOR
        S->last_stamp=0; S->stamp_tick=now;
#endif
    }
    S->tick=now;
    unsigned raw=((unsigned (*)(void))0x01020ac5)();
    if(raw==UINT32_MAX) raw=W(0x201a0);
    unsigned axes=raw && raw!=UINT32_MAX ? centered(raw):0x80808080;
    unsigned buttons=((unsigned (*)(void))0x01021b65)();
#ifdef BLE_GAMEPAD
#ifdef BLE_DUAL
    if(S->installed != ((buttons&(1u<<29)) ? 1 : 2)) return;
#endif
    if(S->installed==2) {
        pad_poll(buttons,axes,now);
        return;
    }
#endif
    int16_t imu[6]={0};
    if(!((unsigned (*)(int16_t *))0x0101d60d)(imu)) imu[0]=imu[1]=imu[2]=0;
#ifdef BLE_STICKS
    // Read-only ATT value, little endian: sample time, four sticks, six IMU axes.
    // Only the monitor reads it. Normal mouse notifications stay unchanged.
    copy_bytes((void *)SAMPLE_ADDRESS,(const uint8_t *)&now,4);
    copy_bytes((void *)(SAMPLE_ADDRESS+4),(const uint8_t *)&axes,4);
    copy_bytes((void *)(SAMPLE_ADDRESS+8),(const uint8_t *)imu,12);
#endif
#ifdef BLE_MONITOR
    uint8_t motion[10];
#else
    uint8_t motion[9]={0};
#endif
    MouseState *mouse=(MouseState *)0x2fe00;
    unsigned mode=mouse->pointer_mode;
    ((int (*)(const int16_t *,unsigned,unsigned,uint8_t *,unsigned))0x01027e3d)(
        imu,buttons,dt>101?101:dt,motion,axes);
    if(mode!=mouse->pointer_mode) ((void (*)(unsigned))0x0102c289)(mouse->pointer_mode);
    if(buttons & POWER_BUTTON) {
        if(S->power_armed) S->power_ms=dt>=750u-S->power_ms?750:S->power_ms+dt;
    } else {
        if(S->power_ms && S->power_ms<750) S->home=1;
        S->power_ms=0; S->power_armed=1;
    }
    uint8_t keys=((buttons>>12)&1)|((buttons>>5)&30);
    unsigned shoulders=(buttons>>10)&3;
    uint8_t media=((buttons>>13)&1)|((buttons>>15)&2)|(S->home?2:0)|
                  (shoulders==3?0:shoulders<<2);
    if(keys!=S->keys && notify(KEY_HANDLE,&keys,1)) S->keys=keys;
    if(media!=S->media && notify(MEDIA_HANDLE,&media,1)) S->media=media;
    if(S->media&2) S->home=0;
    if(S->blocked && (now-S->blocked_at>32 || dt>32)) {
        S->x=S->y=S->wheel=0;S->blocked=0;
    }
    S->x=bounded(S->x+axis(motion+1),32767);
    S->y=bounded(S->y+axis(motion+3),32767);
    S->wheel=bounded(S->wheel+(int8_t)motion[5],127);
#ifdef BLE_MONITOR
    int changed=motion[0]!=S->clicks;
    unsigned source=(mouse->pointer_mode==MODE_STICK || (mouse->pointer_mode==MODE_AUTO && mouse->auto_stick))?2:1;
    unsigned right=axes>>24;
    if(right<110 || right>145)source=3;
    uint32_t stamp=1+(buttons&0x3ffff)+(source<<18);
    changed|=stamp!=S->last_stamp || ((buttons&0x3ffff) && now-S->stamp_tick>=64);
    if(S->x || S->y || S->wheel || changed) {
        copy_bytes(motion+1,(const uint8_t *)S+__builtin_offsetof(Controls,x),4);
        motion[5]=(uint8_t)S->wheel;
        copy_bytes(motion+6,(const uint8_t *)&stamp,4);
        if(notify(MOUSE_HANDLE,motion,sizeof(motion))) {
#else
    if(S->x || S->y || S->wheel || motion[0]!=S->clicks) {
        uint8_t report[]={motion[0],S->x,S->x>>8,S->y,S->y>>8,S->wheel};
        if(notify(MOUSE_HANDLE,report,sizeof(report))) {
#endif
#ifdef BLE_MONITOR
            S->last_stamp=stamp; S->stamp_tick=now;
#endif
            S->clicks=motion[0];S->x=S->y=S->wheel=0;S->blocked=0;
        } else if(!S->blocked) {S->blocked=1;S->blocked_at=now;}
    }
}

#ifdef BLE_DUAL
__attribute__((section(".dual_code"))) void ble_switch_off(void) {
    if(S->installed) {
        S->switching=1;
        S->advertise_pending=0;
        ((int (*)(unsigned,unsigned,unsigned))0x0101ab8d)(0,0,0);
        ((int (*)(void))0x01018fb5)();
    }
    ((void (*)(void))0x0101f665)();
}
__attribute__((section(".pad_extra"))) void ble_link_down(const char *message) {
    if(S->installed && !S->switching) S->advertise_pending=1;
    ((void (*)(const char *))0x0102141d)(message);
}
#endif
