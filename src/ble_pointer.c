/* Android BLE input path for the SHA-pinned r50 image. Report 1 contains
   one button byte followed by two signed 16-bit relative displacements. */
#include "mouse_core.h"

typedef struct {
    MouseState mouse;
    uint32_t sample_tick, send_tick;
    uint8_t active, button, awaiting, flags, scans, sent, ack;
} BlePointer;

#define P ((BlePointer *)0x2fd80)
#define B(addr) (*(volatile uint8_t *)(addr))
#define W(addr) (*(volatile uint32_t *)(addr))
_Static_assert(sizeof(BlePointer)<=128,"pointer state must fit below r50 state");

static void reset(void) {
    for (unsigned i=0;i<sizeof(*P);++i) ((volatile uint8_t *)P)[i]=0;
}

__attribute__((section(".start")))
void ble_xy_start(void) {
    reset();
    ((void (*)(void))0x0102c84d)();
}

static void status(void) {
    static const char hex[]="0123456789ABCDEF";
    volatile uint8_t *out=(volatile uint8_t *)STATUS_ADDR;
    out[0]=hex[P->scans>>4]; out[1]=hex[P->scans&15];
    out[2]=hex[P->flags>>4]; out[3]=hex[P->flags&15];
    out[4]=hex[P->sent&15]; out[5]=hex[P->ack&15];
}

static unsigned center(unsigned raw) {
    unsigned adjusted=0;
    for (unsigned i=0;i<4;i++) {
        int v=(raw>>(8*i))&255, lo=B(0x0102d714+i), hi=B(0x0102d718+i);
        int n=128;
        if (v<lo) n-=((lo-v)*128)/lo;
        if (v>hi) n+=((v-hi)*127)/(255-hi);
        adjusted|=(unsigned)n<<(8*i);
    }
    return adjusted;
}

__attribute__((section(".entry")))
void ble_xy_poll(void) {
    ((void (*)(void))0x0102d4cd)();
    /* Check the installed report's type, not the first letter of a name
       that the radio can rewrite. Other S paths and D keep their own loop. */
    if (B(0x20184) || B(STATUS_ADDR-3)!=1 || B(STATUS_ADDR-2)!=0x29 ||
        B(STATUS_ADDR-1)!=6 || ((unsigned (*)(void))0x01020a81)()) {
        if (P->active) reset();
        return;
    }
    unsigned (*elapsed)(unsigned,unsigned)=(void *)0x01023481;
    if (P->active && !elapsed(P->sample_tick,10)) return;
    unsigned dt=P->active && elapsed(P->sample_tick,100)?101:10;
    P->active=1; P->sample_tick=W(0x20034); P->scans++;
    unsigned axes=((unsigned (*)(void))0x01020ac5)();
    if (axes==UINT32_MAX) { axes=W(0x201a0); P->flags|=1; }
    if (axes==UINT32_MAX) axes=0x80808080;
    axes=center(axes);
    unsigned x=axes&255,y=(axes>>8)&255;
    if(x<80 || x>176 || y<80 || y>176) P->flags|=2;
    int16_t imu[6]={0};
    if (((int (*)(int16_t *))0x0101d60d)(imu)) P->flags|=4;
    unsigned buttons=((unsigned (*)(void))0x01021b65)();
    uint8_t motion[9]={0};
    ((int (*)(MouseState *,const int16_t *,uint32_t,uint32_t,uint8_t *,uint32_t))0x0102c389)(
        &P->mouse,imu,buttons,dt,motion,axes);
    motion[0]&=1;
    if(P->mouse.calibrated) P->flags|=8;
    if((motion[1]|motion[2]|motion[3]|motion[4])!=0)
        P->flags|=(P->mouse.pointer_mode==MODE_STICK ||
                  (P->mouse.pointer_mode==MODE_AUTO && P->mouse.auto_stick))?16:32;
    for(unsigned i=0;i<5;i++) B(REPORT_ADDR+i)=motion[i];
    if(P->awaiting) {
        if(((unsigned (*)(unsigned,unsigned))0x01018f0b)(0x2000c,6)) {
            P->ack++; P->awaiting=0;
        } else if(elapsed(P->send_tick,200)) {
            P->flags|=64; P->awaiting=0;
        }
    }
    if(!P->awaiting && (motion[0]!=P->button || motion[1] || motion[2] || motion[3] || motion[4])) {
        uint8_t payload[7]={17,0};
        for(unsigned i=0;i<5;i++) payload[i+2]=motion[i];
        uint8_t req[7];
        req[0]=0;req[1]=9;req[2]=sizeof(payload);
        ((void (*)(const void *,void *))0x01018303)(payload,req+3);
        ((void (*)(unsigned,unsigned))0x01018ee9)(0x2000c,6);
        if(((unsigned (*)(void *))0x010223b9)(req)) {
            P->sent++; P->awaiting=1; P->button=motion[0]; P->send_tick=P->sample_tick;
        } else P->flags|=128;
    }
    status();
}
