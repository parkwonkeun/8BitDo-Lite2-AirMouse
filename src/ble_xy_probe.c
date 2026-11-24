/* On-demand transport test for the existing four-byte BLE report map.
   Gate each batch on an explicit Y press; a missing controller ACK stops
   the batch. No generated test movements, key injection or NVM writes. */
#include <stdint.h>

#define BYTE(a) (*(volatile uint8_t *)(a))
typedef struct { uint32_t tick; uint8_t held, awaiting, stopped, armed; } Probe;
#define STATE ((volatile Probe *)0x2fdf8)
_Static_assert(sizeof(Probe)==8, "probe RAM reservation");

static void progress(unsigned stage) {
#if CURSOR_PROBE
    const unsigned pins[]={0x100,0x80,0x8000,0x4000};
    for (unsigned i=0;i<4;i++) {
        uintptr_t fn=(stage==i+1)?0x0101cf19:0x0101ce4d;
        ((void (*)(unsigned,unsigned))fn)(0,pins[i]);
    }
#else
    (void)stage;
#endif
}

__attribute__((section(".start")))
void ble_xy_start(void) {
    *(volatile uint32_t *)0x2fdf8=0;
    *(volatile uint32_t *)0x2fdfc=0;
    ((void (*)(void))0x0102c84d)();
}

__attribute__((section(".entry")))
void ble_xy_poll(void) {
    ((void (*)(void))0x0102d4cd)(); /* r50 foreground USB handler */
    if (BYTE(0x20184) || BYTE(0xc4382)!='L' ||
        ((unsigned (*)(void))0x01020a81)()) return;
    unsigned buttons=((unsigned (*)(void))0x01021b65)();
    if (!(buttons & 0x20)) {
        if (STATE->held) progress(1);
        STATE->armed=0xba;
        STATE->held=STATE->awaiting=STATE->stopped=0;
        return;
    }
    if (STATE->armed!=0xba || STATE->stopped) return;
    unsigned (*elapsed)(unsigned,unsigned)=(void *)0x01023481;
    if (!STATE->held) progress(1);
    if (STATE->held && !elapsed(STATE->tick,15)) return;
    if (STATE->awaiting && !((unsigned (*)(unsigned,unsigned))0x01018f0b)(0x2000c,6)) {
        if (elapsed(STATE->tick,200)) { STATE->stopped=1; progress(4); }
        return;
    }
    if (STATE->awaiting) progress(3);
    STATE->awaiting=0;
    if (!STATE->held) STATE->held=1;
    STATE->tick=*(volatile uint32_t *)0x20034;
    unsigned axes=((unsigned (*)(void))0x01020ac5)();
    if (!axes || axes==UINT32_MAX) return;
    int x=(int)(uint8_t)axes-128, y=(int)(uint8_t)(axes>>8)-128;
    /* Large deliberate deflections only for this transport test. */
    x=(x>48 || x < -48) ? x/8 : 0;
    y=(y>48 || y < -48) ? y/8 : 0;
    if (!(x || y)) return;
#if CURSOR_PROBE
    uint8_t value[]={18,0,0,(uint8_t)x,(uint8_t)(x<0?255:0),(uint8_t)y,(uint8_t)(y<0?255:0)};
#else
    uint8_t value[]={18,0,(uint8_t)x,(uint8_t)(x<0?255:0),(uint8_t)y,(uint8_t)(y<0?255:0)};
#endif
    uint8_t req[7];
    req[0]=0;
    req[1]=9;
    req[2]=sizeof(value);
    ((void (*)(const void *,void *))0x01018303)(value,req+3);
    ((void (*)(unsigned,unsigned))0x01018ee9)(0x2000c,6);
    STATE->awaiting=((unsigned (*)(void *))0x010223b9)(req)!=0;
    if (STATE->awaiting && STATE->held==1) {progress(2); STATE->held=2;}
    else if (!STATE->awaiting) {progress(4); STATE->stopped=1;}
}
