#include "mouse_core.h"
#include <stddef.h>

void *memset(void *destination, int value, size_t count) {
    uint8_t *p = destination;
    while (count--) *p++ = (uint8_t)value;
    return destination;
}

/* Addresses are valid ONLY for the SHA-256 pinned v1.06 image.
   The patch builder reserves 256 bytes below the original initial SP.
   This placement still requires hardware/ROM ownership verification. */
#define READ8(a) (*(volatile uint8_t *)(a))
#define READ32(a) (*(volatile uint32_t *)(a))
#define CALL0(a) ((void (*)(void))((a) | 1u))()
enum { KEY_REFRESH_FRAMES = 50, MOTION_HISTORY = 8 };
typedef struct { int16_t x, y; } Motion;
enum VisibilityTrial { TRIAL_IDLE, TRIAL_HOLD, TRIAL_ARMED, TRIAL_WAIT, TRIAL_READY,
                       TRIAL_ACKED, TRIAL_FAILED };
typedef struct {
    uint32_t scans, imu_reads, low_scans, rejected;
    uint8_t defer, queue_quanta, trial, windows_left;
    uint32_t trial_tick;
} RadioStats;
typedef struct {
    MouseState mouse;
    uint32_t last_tick, active, initialized;
    Motion pending[MOTION_HISTORY];
    uint8_t head, count;
    int32_t pending_wheel;
    int32_t pending_fine_wheel;
    uint8_t last_keys, last_consumer, sent_keyboard, transports;
    uint8_t key_refresh, consumer_refresh, diagnostic_phase;
    uint16_t power_hold_ms;
    uint8_t power_armed, power_was_down, home_pending;
    uint16_t trace_scans;
    uint8_t mouse_serial, pressure_flags;
    RadioStats radio;
    struct {
        uint16_t next;
        uint32_t units[6];
        uint8_t loaded;
    } gain;
    volatile uint16_t slot_lease_ms;
    uint8_t mode_triggers;
    uint32_t usb_frames;

} Adapter;
#define STATE ((Adapter *)0x0002fe00)
_Static_assert(sizeof(Adapter) <= 256, "reserved RAM overflow");

/* The native configuration writer erases 0x107d000..0x107dfff and
   repopulates its 132-byte record. Its other known reader is at +0x100.
   Use an append-only extension from +0x200. Leave bond sectors untouched. */
enum { CONFIG = 0x0107d000, FIRST_GAIN = 0x200, SECTOR_END = 0x1000 };
#define SETTINGS_CODE __attribute__((section(".settings"),noinline))
#define FLASH_READ ((int (*)(unsigned,void *,unsigned))0x010276a1)
#define FLASH_WRITE ((int (*)(unsigned,const void *,unsigned))0x01027705)

static unsigned gain_word(unsigned channel,unsigned value) {
    /* Bits 0..2 of the tag retain the r39 channel and bit 16.
       Bit 3 extends gains beyond 1310.71% without changing old records. */
    unsigned bank=channel/3, axis=channel%3;
    unsigned tag=axis==2 ? 0xe0+bank :
        0xd0+2*(bank*2+axis)+((value>>16)&1)+((value>>17)<<3);
    unsigned lo=value&255,hi=(value>>8)&255;
    return tag | (lo<<8) | (hi<<16) | ((255^tag^lo^hi)<<24);
}

static __attribute__((noinline)) void load_gain(void) {
    if (STATE->gain.loaded) return;
    /* The erased suffix is contiguous. Find the next free record in ten
       reads even after hundreds of changes. Skip a torn last record. */
    unsigned low=FIRST_GAIN/4, high=SECTOR_END/4, word;
    while (low<high) {
        unsigned mid=(low+high)/2;
        if (!FLASH_READ(CONFIG+mid*4,&word,4)) return;
        if (word==UINT32_MAX) high=mid; else low=mid+1;
    }
    STATE->gain.next=low*4;
    unsigned found=0;
    unsigned legacy[3]={GAIN_DEFAULT,GAIN_DEFAULT,GAIN_DEFAULT};
    while (low>FIRST_GAIN/4 && found!=63) {
        if (!FLASH_READ(CONFIG+4*--low,&word,4)) return;
        unsigned tag=word&255, stick=(word>>8)&255;
        if (((word^(word>>16))&0xffff)==0xffff && tag-1<200 && stick-1<200) {
            legacy[0]=tag*500; legacy[1]=stick*500;
            break;
        }
        if (tag-0xd0u>=18) continue;
        unsigned old_channel=(tag>>1)&3;
        unsigned channel=tag>=0xe0 ? 2+3*(tag-0xe0) : old_channel+(old_channel>>1);
        unsigned mask=1u<<channel;
        unsigned value=(word>>8)&65535;
        if(tag<0xe0)value|=((tag&8)<<14) | ((tag&1)<<16);
        if (!(found&mask) && value>=GAIN_MIN && value<=(tag>=0xe0 ? WHEEL_GAIN_MAX : GYRO_GAIN_MAX) && gain_word(channel,value)==word) {
            STATE->gain.units[channel]=value;
            found|=mask;
        }
    }
    for (unsigned i=0;i<6;i++) if (!(found&(1u<<i))) STATE->gain.units[i]=legacy[i%3];
    STATE->gain.loaded=1;
}

static SETTINGS_CODE void append_gain(unsigned channel) {
    unsigned value=gain_word(channel,STATE->gain.units[channel]), check=0, at=STATE->gain.next;
    if (at>=SECTOR_END) {
        /* The normal configuration commit also runs on orderly power-off.
           Defer a full journal to that path instead of erasing during motion. */
        READ8(0x201ba)=1;
        return;
    }
    if (FLASH_WRITE(CONFIG+at,&value,4) &&
        FLASH_READ(CONFIG+at,&check,4) && check==value) {
        STATE->gain.next=at+4;
        if (at>=SECTOR_END-128) READ8(0x201ba)=1;
    } else {
        /* A partially programmed word cannot be reused. The native save
           writes the current RAM settings into a freshly erased sector. */
        STATE->gain.next=SECTOR_END;
        READ8(0x201ba)=1;
    }
}

SETTINGS_CODE int lite2_config_erase(unsigned address,unsigned size) {
    load_gain();  /* Also preserve the extension on a cold start in D mode. */
    return ((int (*)(unsigned,unsigned))0x0101c6af)(address,size);
}
SETTINGS_CODE int lite2_config_write(unsigned address,const void *data,unsigned words) {
    int result=((int (*)(unsigned,const void *,unsigned))0x0101c6d3)(address,data,words);
    STATE->gain.next=FIRST_GAIN;
    if (STATE->gain.loaded)
        for (unsigned i=0;i<6;i++) if (STATE->gain.units[i]!=GAIN_DEFAULT) append_gain(i);
    return result;
}

SETTINGS_CODE void lite2_slot_output(const uint8_t *packet,unsigned length,unsigned transport) {
    unsigned offset=transport==2;
    if ((transport==1 || transport==2) && length>=offset+2 &&
        (!offset || packet[0]==0xa2) && packet[offset]==4) {
        STATE->mode_triggers=packet[offset+1]&3;
        STATE->slot_lease_ms=(packet[offset+1]&16) ? 2000 : 0;
        return;
    }
    ((void (*)(const uint8_t *,unsigned,unsigned))0x01022d91)(packet,length,transport);
}

static SETTINGS_CODE int pointer_step(const int16_t *imu,unsigned buttons,unsigned dt,
                                       uint8_t *out,unsigned axes) {
    load_gain();
    MouseState *s=&STATE->mouse;
    unsigned bank=(STATE->transports & 1u)*3;
    uint32_t *units=STATE->gain.units+bank;
    __builtin_memcpy(s->gain_units,units,sizeof(s->gain_units));
    if(STATE->slot_lease_ms) {
        unsigned remaining=STATE->slot_lease_ms;
        STATE->slot_lease_ms=dt>=remaining?0:remaining-dt;
        s->last_gain_button=s->repeat_ms=s->tap_ms=0;
        s->previous_triggers=(buttons>>14)&3;
        if(s->previous_triggers & STATE->mode_triggers)buttons|=2;
    }
    int hold=mouse_step(s,imu,buttons,dt,out,axes);
    if (STATE->gain.loaded) {
        for (unsigned i=0;i<3;i++) if (s->gain_units[i]!=units[i]) {
            units[i]=s->gain_units[i];
            append_gain(bank+i);
        }
    }
    return hold;
}

extern const uint8_t lite2_sdp_recipe[];
extern const unsigned lite2_mouse_sdp_size;
const char lite2_mouse_name[] = "Lite2 Air Mouse Plus";

__attribute__((noreturn)) void lite2_start(void) {
    memset(STATE, 0, 256);
    CALL0(0x010271e4);
    for (;;) {}
}

#ifndef BLE_BASE_R50
static unsigned tx_queue_quanta(unsigned free_bytes) {
    /* v1.06 initializes this entire TX shared-memory bank at 0x010265f8.
       The eight-byte descriptor and the FIFO can move within it. */
    const unsigned bank=0xc4000, bank_size=0x1000;
    unsigned address=READ32(0x2002c);
    if (address-bank > bank_size-8) return 63;
    /* These are byte-wide radio registers, even on aligned addresses. */
    volatile const uint8_t *ring=(volatile const uint8_t *)address;
    unsigned start_lo=ring[0], start_hi=ring[1];
    unsigned end_lo=ring[2], end_hi=ring[3];
    unsigned start=start_lo | (start_hi<<8), end=end_lo | (end_hi<<8);
    unsigned descriptor=address-0xc0000;
    if (start<0x4000 || end>=0x5000 || end<start ||
        (start<descriptor+8 && end>=descriptor)) return 63;
    unsigned capacity=end-start+1;
    if (!free_bytes || free_bytes>capacity) return 63;
    unsigned pending=(capacity-free_bytes)/4;
    return pending<63 ? pending : 63;
}

#endif

/* Keep stock radio headroom while continuing to sample S wireless inputs. */
unsigned lite2_scan_pressure(void) {
    int free_bytes = ((int (*)(void))0x010205cd)();
#ifdef BLE_BASE_R50
    unsigned pending=63;
    if(STATE->trace_scans && READ32(0x2002c)==0xc4ff8 && READ32(0xc4ff8)==0x4fe04c80 &&
       free_bytes>0 && free_bytes<=865) {
        pending=(865u-(unsigned)free_bytes)/4;
        if(pending>63)pending=63;
    }
    STATE->radio.queue_quanta=pending;
#else
    STATE->radio.queue_quanta=STATE->trace_scans ? tx_queue_quanta(free_bytes) : 63;
#endif
    unsigned route = 0;
    if (free_bytes < 700 && READ8(0x20152) == 2)
        route = READ8(0x20184) == 0 ? 2 : 1;
    STATE->radio.defer = route == 2;
    if (route == 2) ++STATE->radio.low_scans;
    return route;
}

int lite2_set_device_class(const uint8_t *stock_class) {
    /* Native opcode 0x15 copies three bytes from the caller's address. */
    static const uint8_t mouse_keyboard_class[3] = {0xc0,0x25,0x00};
    const uint8_t *device_class = READ8(0x20184) == 0 ? mouse_keyboard_class : stock_class;
    return ((int (*)(const uint8_t *))0x0101a9c1)(device_class);
}

void lite2_hid_connected(void) {
    CALL0(0x010269d4);
    if (!READ8(0x20184)) {
        STATE->radio.trial=TRIAL_IDLE;
        STATE->radio.queue_quanta=63;
        STATE->trace_scans=0;
    }
}

int lite2_set_sdp(uint8_t *buffer, unsigned capacity, unsigned service) {
    const uint8_t *op=lite2_sdp_recipe;
    unsigned size=lite2_mouse_sdp_size;
    if (size>capacity) return 0;
    uint8_t *out=buffer;
    while (out<buffer+size) {
        unsigned n=*op++;
        const uint8_t *source=op;
        if (n&128) {
            n-=128;
            op+=n;
        } else {
            source=(const uint8_t *)(0x01020000u+op[0]+(op[1]<<8));
            op+=2;
        }
        while(n--) *out++=*source++;
    }
    return ((int (*)(const void *,unsigned,unsigned))0x0101a6fd)(buffer,size,service);
}

int lite2_switch_commands(void) {
    /* S no longer waits for Nintendo report-mode/IMU-enable handshakes. */
    return 0;
}

void lite2_scan(void) {
    CALL0(0x01023958);
    uint32_t active = READ8(0x20184) == 0;
    if (!active || !STATE->active) {
        mouse_reset(&STATE->mouse);
        STATE->initialized = 0;
    }
    STATE->active = active;
    /* Stock input scanner emits even without button changes when below 100. */
    if (active) *(volatile uint16_t *)0x201ea = 99;
}

/* Keep S wireless scans on the shortest native timer interval, even at idle. */
void lite2_scan_delay(void *timer, unsigned delay) {
    if (READ8(0x20184)==0 && READ8(0x20152)==2) delay = 4;
    ((void (*)(void *,unsigned))0x0102644d)(timer,delay);
}

static const uint16_t player_pins[] = {0x100, 0x80, 0x8000, 0x4000};

static void write_player_lights(unsigned mask, unsigned on) {
    typedef void (*GpioWrite)(unsigned, unsigned);
    GpioWrite write = (GpioWrite)(on ? 0x0101cf19 : 0x0101ce4d);
    for (unsigned i = 0; i < 4; ++i)
        if (mask & player_pins[i]) write(0, player_pins[i]);
}

static void set_mode_led(unsigned mode) {
    unsigned mask = player_pins[mode];
    write_player_lights(~mask, 0);
    write_player_lights(mask, 1);
}

void lite2_player_lights(unsigned mask, unsigned on) {
    if (READ8(0x20184) == 0 && STATE->active &&
        (READ8(0x20152) == 2 || ((int (*)(void))0x01020a81)()))
        set_mode_led(STATE->mouse.pointer_mode);
    else
        write_player_lights(mask, on);
}

/* The long pairing-button handler writes this retained boot reason before
   restarting the radio. Stock reconnect skips the saved host for this reason,
   and the link-up handler clears it. Use the stock LED timer in both modes. */
void lite2_pairing_lights(void *callback_arg) {
    if (READ32(0x2fff8) != 0x20221031 || READ8(0x20152) == 2 ||
        !((int (*)(void))0x01020a35)() || ((int (*)(void))0x01020a81)()) {
        ((void (*)(void *))0x010210e1)(callback_arg);
        return;
    }

    void *timer = (void *)0x21870;
    ((void (*)(unsigned))0x010224d1)(9);  /* Toggle all four player LEDs. */
    ((void (*)(void *,unsigned))0x0102644d)(timer,250);
    ((void (*)(void *))0x01026105)(timer);
}

/* Keep ordinary control requests on the native handler, including the
   SET_REPORT data/status phases. A fixed multiplier needs no saved setting. */
__attribute__((naked,noinline)) static int stock_usb_setup(void *device __attribute__((unused)),
                                                           const uint8_t *setup __attribute__((unused))) {
    __asm__("push {r1,r2,r3,r4,r5,r6,r7,lr}\n"
            "mov r6,r0\n"
            "mov r4,r1\n"
            "movs r5,#0\n"
            "ldr r3,1f\n"
            "bx r3\n"
            ".balign 4\n"
            "1: .word 0x01023f69");
}

int lite2_usb_setup(void *device,const uint8_t *setup) {
    /* HID GET_REPORT, feature ID 6, interface 0. Return a fixed 120x field. */
    if (READ8(0x20184)==0 && setup[0]==0xa1 && setup[1]==1 &&
        setup[2]==6 && setup[3]==3 && !setup[4] && !setup[5]) {
        static const uint8_t feature[]={6,1};
        unsigned length=setup[6] | (setup[7]<<8);
        ((void (*)(void *,const uint8_t *,unsigned))0x01023a87)(
            device,feature,length<sizeof(feature)?length:sizeof(feature));
        return 0;
    }
    return stock_usb_setup(device,setup);
}

static int send_bluetooth(const uint8_t *data, unsigned length) {
    /* Match the stock B0 packet enqueue ABI, but expose its acceptance result. */
    struct { uint8_t ready, command; uint16_t length; const uint8_t *data; }
        request = {0, 0xb0, (uint16_t)length, data};
    ((void (*)(unsigned, unsigned))0x01018ee9)(0x2000c,5);
    return ((int (*)(void *))0x010223ff)(&request);
}

static int32_t signed_axis(const uint8_t *axis) {
    return (int16_t)(axis[0] | (axis[1] << 8));
}

/* The native timeout helper uses elapsed SysTick time and never changes it. */
static void update_visibility_trial(unsigned keys, unsigned axes) {
    RadioStats *r=&STATE->radio;
    int chord=keys==0x409 && ((axes>>19)&31)-15<=1 && (axes>>27)-15<=1;
    int (*expired)(unsigned,unsigned)=(int (*)(unsigned,unsigned))0x01023481;
    if (r->trial>=TRIAL_ACKED) return;
    if (STATE->slot_lease_ms) r->trial=TRIAL_IDLE;
    else if (r->trial==TRIAL_IDLE && chord) {
        r->trial_tick=READ32(0x20034);
        r->trial=TRIAL_HOLD;
    } else if (r->trial==TRIAL_HOLD) {
        if (!chord) r->trial=TRIAL_IDLE;
        else if (expired(r->trial_tick,800)) {
            r->trial=TRIAL_ARMED;
            STATE->trace_scans=12000;
        }
    } else if (r->trial==TRIAL_ARMED && !keys) {
        r->trial_tick=READ32(0x20034);
        r->trial=TRIAL_WAIT;
        r->windows_left=12;
        STATE->trace_scans=12000;
    } else if (r->trial==TRIAL_WAIT && expired(r->trial_tick,30000)) {
        /* Six minutes, in windows short enough for the stock tick-to-ms
           conversion. Refresh the stamp stream through the transition. */
        r->trial_tick=READ32(0x20034);
        STATE->trace_scans=18001;
        if (!--r->windows_left) r->trial=TRIAL_READY;
    }
}

/* The scanner can report nominal center and a second stable resting position.
   Map the measured neutral interval to zero before forming either report. */
const volatile uint32_t lite2_stick_centers[2] = {0x80808080,0x80808080};

uint32_t lite2_center_axes(uint32_t raw) {
    uint32_t result = 0;
    uint32_t lows = lite2_stick_centers[0], highs = lite2_stick_centers[1];
    for (int i = 0; i < 4; ++i) {
        int low = lows >> 24, high = highs >> 24;
        int sample = raw >> 24;
        int delta = 0;
        if (sample < low) delta = (sample-low)*128/low;
        if (sample > high) delta = (sample-high)*127/(255-high);
        result = (result << 8) | (unsigned)(128 + delta);
        lows <<= 8;
        highs <<= 8;
        raw <<= 8;
    }
    return result;
}

static void update_power_shortcut(uint32_t physical, unsigned dt) {
    /* Arm after release so powering up or reconnecting cannot send Home. */
    if (!(physical & POWER_BUTTON)) {
        if (STATE->power_armed && STATE->power_was_down && STATE->power_hold_ms < 750)
            STATE->home_pending = 1;
        STATE->power_armed = 1;
        STATE->power_hold_ms = STATE->power_was_down = 0;
    } else {
        STATE->power_was_down = 1;
        if (STATE->power_hold_ms < 750) STATE->power_hold_ms += dt;
    }
}

/* Count milliseconds within the stock SysTick without changing its reload
   or the timing of Bluetooth, power management and other native timers. */
static __attribute__((section(".gravity"),noinline)) uint32_t usb_millis(unsigned period) {
    uint32_t ticks, value, reload=READ32(0xe000e014)+1;
    do {
        ticks=READ32(0x20034);
        value=READ32(0xe000e018);
    } while (ticks!=READ32(0x20034));
    unsigned phase=(reload-1-value)*period;
    return ticks*period+(int)phase/(int)reload;
}

void lite2_dispatch(uint32_t buttons, uint32_t axes, uint32_t triggers) {
    if (READ8(0x20184) != 0) {
        ((void (*)(uint32_t,uint32_t,uint32_t))0x01020d39)(buttons,axes,triggers);
        return;
    }
    if (buttons == UINT32_MAX) buttons = READ32(0x20130);
    int usb = ((int (*)(void))0x01020a81)();
    int bluetooth = READ8(0x20152) == 2;
    unsigned transports = (usb ? 1u : 0u) | (bluetooth ? 2u : 0u);
    if (transports != STATE->transports) {
        STATE->mouse.calibrated = STATE->mouse.have_anchor = 0;
        STATE->mouse.click_ms = STATE->mouse.previous_clicks = 0;
        STATE->mouse.calibration_ms = 0;
        STATE->mouse.tilt_variance=0;
        STATE->mouse.sum_roll=0;
        STATE->mouse.sum_x = STATE->mouse.sum_z = 0;
        STATE->mouse.fine_x = STATE->mouse.fine_y = 0;
        memset(&STATE->initialized, 0, offsetof(Adapter, radio) - offsetof(Adapter, initialized));
        STATE->mouse.repeat_ms=STATE->mouse.tap_ms=STATE->mouse.last_gain_button=STATE->mouse.previous_triggers=0;
        STATE->last_keys = STATE->last_consumer = 0xff;
        STATE->transports = transports;
    }
    if (!transports) return;
    uint32_t period = READ8(0x2022b);
    if (period < 5 || period > 100) period = 10;
    uint32_t tick = usb ? usb_millis(period) : READ32(0x204f0);
    uint32_t unit = usb ? 1 : period;
    uint32_t dt = 0;
    if (STATE->initialized) {
        uint32_t delta = (tick - STATE->last_tick) & 0x7fffffff;
        if (usb && !delta) return;
        dt = delta <= 100 && delta * unit <= 100 ? delta * unit : 101;
        if (!usb && (dt == 0 || dt > 100)) dt = period;
    }
    STATE->last_tick = tick; STATE->initialized = 1;
    if (usb && axes == UINT32_MAX)
        axes = ((uint32_t (*)(void))0x01020ac5)();
    if (axes == UINT32_MAX) axes = READ32(0x201a0);
    axes = axes ? lite2_center_axes(axes) : 0x80808080;
    int16_t imu[6] = {0};
    unsigned prior_mode = STATE->mouse.pointer_mode;
    unsigned prior_auto_stick = STATE->mouse.auto_stick;
    int ok = 0;
    if (prior_mode != MODE_STICK && !(usb && prior_mode==MODE_AUTO &&
        prior_auto_stick && STATE->mouse.auto_hold_ms>dt))
        ok = ((int (*)(int16_t *))0x0101d60d)(imu);
    if (bluetooth) {
        ++STATE->radio.scans;
        if (ok) ++STATE->radio.imu_reads;
    }
    if (!ok) imu[0] = imu[1] = imu[2] = 0;
    uint8_t packet[19] = {0xa1,1};
    uint32_t physical_buttons = ((uint32_t (*)(void))0x01021b65)();
    uint32_t mouse_buttons = (buttons & ~0xc000u) | (physical_buttons & 0xc000u);
    int prior_wheel_direction=STATE->mouse.wheel_direction;
    int click_hold = pointer_step(imu,mouse_buttons,dt,packet+2,axes);
    if (prior_wheel_direction!=STATE->mouse.wheel_direction) STATE->pending_fine_wheel=0;
    int fine_wheel = (int16_t)(packet[8] | (packet[9]<<8));
    if (click_hold || prior_mode != STATE->mouse.pointer_mode ||
        prior_auto_stick != STATE->mouse.auto_stick)
        STATE->count = 0;
    if (!usb || !dt || prior_mode!=STATE->mouse.pointer_mode) set_mode_led(STATE->mouse.pointer_mode);
    /* The stock S scanner can mask Capture for its button combinations. Read
       its physical bit before filtering so the star remains a Home shortcut. */
    update_power_shortcut(physical_buttons, dt ? dt : period);
    uint8_t controls = keyboard_keys((buttons & ~CAPTURE_BUTTON) |
                                     (physical_buttons & CAPTURE_BUTTON));
    if (STATE->home_pending) controls |= 64;
    uint8_t keys = controls & 31, consumer_keys = controls >> 5;
    int diagnostic_only = usb && !bluetooth && (buttons & 1) && (STATE->diagnostic_phase ^= 1);
    unsigned report = 0;
    if (STATE->key_refresh < KEY_REFRESH_FRAMES) ++STATE->key_refresh;
    if (STATE->consumer_refresh < KEY_REFRESH_FRAMES) ++STATE->consumer_refresh;
    if (!diagnostic_only && !STATE->sent_keyboard) {
        if (keys != STATE->last_keys) report = 4;
        else if (consumer_keys != STATE->last_consumer) report = 5;
        else if (!(STATE->mouse.moving_x || STATE->mouse.moving_y)) {
            /* Reaffirm key state when idle. Real key changes take priority
               immediately, including during continuous wireless movement. */
            if (STATE->key_refresh == KEY_REFRESH_FRAMES) report = 4;
            else if (STATE->consumer_refresh == KEY_REFRESH_FRAMES) report = 5;
        }
    }
    STATE->sent_keyboard = report != 0;
    uint8_t keyboard[10] = {0xa1,4};
    uint8_t consumer[3] = {0xa1,5,consumer_keys};
    const uint8_t *out = packet;
    unsigned length = sizeof(packet);
    Motion current = {signed_axis(packet+3), signed_axis(packet+5)};
    int wheel = STATE->pending_wheel + (usb ? fine_wheel : (int8_t)packet[7]);
    if (wheel > 127) wheel = 127;
    if (wheel < -127) wheel = -127;
    if (report) {
        if (report == 4) {
            static const uint8_t usages[] = {0x28,0x4f,0x50,0x51,0x52};
            unsigned pos = 4;
            for (unsigned i = 0; i < 5; ++i)
                if (keys & (1u << i)) keyboard[pos++] = usages[i];
            out = keyboard;
            length = sizeof(keyboard);
        } else {
            out = consumer;
            length = sizeof(consumer);
        }
    } else {
        if (STATE->count) {
            Motion oldest = STATE->pending[STATE->head];
            put_signed16(packet+3, current.x + oldest.x);
            put_signed16(packet+5, current.y + oldest.y);
        }
        packet[7] = (uint8_t)wheel;
    }
    /* Standard XY remains usable without the companion. RX/RY carry either
       the stick position or bias-corrected gyro counts before pointer shaping. */
    unsigned source = 0;
    if (STATE->mouse.pointer_mode == MODE_STICK ||
        (STATE->mouse.pointer_mode == MODE_AUTO && STATE->mouse.auto_stick)) {
        put_signed16(packet+8, (int)(uint8_t)axes - 128);
        put_signed16(packet+10, (int)(uint8_t)(axes >> 8) - 128);
        source = 2;
    } else if (ok && STATE->mouse.calibrated && !(buttons & 4)) {
        put_signed16(packet+8, STATE->mouse.yaw_rate);
        put_signed16(packet+10, STATE->mouse.pitch_rate);
        source = 1;
    }
    /* Consumer Volume Down / Up share the physical mouse input report.
       The slot and monitor grab this node, so only RAW delivers native volume. */
    unsigned shoulders=(physical_buttons >> 10) & 3;
    if (shoulders!=3) packet[2] |= shoulders << 6;
    /* Extra high bits on existing telemetry fields carry both sticks even
       when RX/RY carry gyro. Every physical button frame remains nonzero. */
    uint16_t tag = (uint16_t)(1024 + source*256 + (axes >> 24) + ((axes >> 19) << 11));

    /* Two nonzero relative fields convey a complete 18-bit physical snapshot,
       including release. Pairing and power-management flags are outside it. */
    unsigned low = (physical_buttons & 511) + 1;
    unsigned high = ((physical_buttons >> 9) & 511) + 1;
    low += (1 + ((uint8_t)axes >> 3)) << 9;
    high += (1 + ((uint8_t)(axes >> 8) >> 3)) << 9;
    /* + and - arm a bounded wireless timing trace. Use only redundant or
       centered stick fields. Android still receives the original mouse XY. */
    if (bluetooth && !usb) {
        unsigned keys_down=physical_buttons & 0x3ffff;
        if (keys_down==9) STATE->trace_scans = 12000;
        update_visibility_trial(keys_down,axes);
        if (STATE->radio.defer) STATE->pressure_flags |= 1;
        if (STATE->trace_scans) {
            --STATE->trace_scans;
            unsigned lx = (uint8_t)axes >> 3, ly = (uint8_t)(axes >> 8) >> 3;
            unsigned rx = (uint8_t)(axes >> 16) >> 3;
            if ((axes >> 24)==128 && rx-15 <= 1 && (source==2 || (lx-15 <= 1 && ly-15 <= 1)) && period < 32) {
                unsigned phase=0;
                if (STATE->radio.trial>=TRIAL_ACKED) phase=STATE->radio.trial-TRIAL_ACKED+1;
                tag = (tag & 0x700) | (period << 11) | (phase << 6) | STATE->radio.queue_quanta;
                low = 0x8000 | ((STATE->mouse_serial & 15) << 9) |
                      (STATE->pressure_flags << 13) | (physical_buttons & 511);
                high = 0x8000 | ((READ32(0x204f0) & 63) << 9) | ((physical_buttons >> 9) & 511);
            }
        }
    }
    packet[12]=tag; packet[13]=tag>>8;
    packet[14]=low;packet[15]=low>>8;
    packet[16]=high;packet[17]=high>>8;
    int accumulated=STATE->pending_fine_wheel+fine_wheel;
    int emitted_wheel=accumulated;
    if (emitted_wheel>120) emitted_wheel=120;
    if (emitted_wheel< -120) emitted_wheel=-120;
    packet[18]=(uint8_t)emitted_wheel;
    if (accumulated>240) accumulated=240;
    if (accumulated< -240) accumulated=-240;
    int accepted = 0;
    if (bluetooth && !STATE->radio.defer) {
        accepted = send_bluetooth(out,length);
        if (!accepted) {
            ++STATE->radio.rejected;
            STATE->pressure_flags |= 2;
        } else if (!report) {
            ++STATE->mouse_serial;
            STATE->pressure_flags = 0;
        }
    }
    STATE->pending_fine_wheel=bluetooth ? accumulated-((!report && accepted)?emitted_wheel:0) : 0;
    if (report) {
        if (usb || accepted) {
            if (report == 4) {
                STATE->last_keys = keys;
                STATE->key_refresh = 0;
            } else {
                STATE->last_consumer = consumer_keys;
                STATE->home_pending = 0;
                STATE->consumer_refresh = 0;
            }
        }
    }
    if (report || (!usb && !accepted)) {
        /* Keep recent samples individually. Age out the oldest after a
           prolonged outage. On recovery add at most one per mouse report. */
        if (STATE->count == MOTION_HISTORY) {
            STATE->head = (STATE->head + 1) % MOTION_HISTORY;
            --STATE->count;
        }
        STATE->pending[(STATE->head + STATE->count) % MOTION_HISTORY] = current;
        ++STATE->count;
        STATE->pending_wheel = wheel;
    } else {
        if (STATE->count) {
            Motion *oldest = &STATE->pending[STATE->head];
            /* If the combined delta hit the HID limit, retain the part
               that did not fit, independently for the two axes. */
            oldest->x -= signed_axis(packet+3) - current.x;
            oldest->y -= signed_axis(packet+5) - current.y;
            if (!(oldest->x || oldest->y)) {
                STATE->head = (STATE->head + 1) % MOTION_HISTORY;
                --STATE->count;
            }
        }
        STATE->pending_wheel = 0;
    }
    if (usb) {
        ++STATE->usb_frames;
        if (diagnostic_only && !report) {
            /* Hold + only when collecting a diagnostic trace over USB. */
            uint8_t diagnostic[64] = {3,2};
            diagnostic[2] = ok; diagnostic[3] = STATE->mouse.calibrated;
            diagnostic[4] = bluetooth; diagnostic[5] = period;
            diagnostic[6] = dt; diagnostic[7] = READ8(0x20184);
            for (unsigned i=0;i<sizeof(imu);++i) diagnostic[8+i]=((const uint8_t *)imu)[i];
            __builtin_memcpy(diagnostic+20,&buttons,sizeof(buttons));
            diagnostic[34] = 'R'; diagnostic[35] = 50;
            __builtin_memcpy(diagnostic+52,STATE->mouse.gain_units,8);
            __builtin_memcpy(diagnostic+60,&STATE->usb_frames,4);
            __builtin_memcpy(diagnostic+36, &STATE->radio, offsetof(RadioStats, defer));
            for (unsigned i=0;i<9;++i) diagnostic[24+i]=packet[2+i];
            ((void (*)(const void *,unsigned))0x0101c669)(diagnostic,64);
        } else {
            ((void (*)(const void *,unsigned))0x0101c669)(
                out+1,report?length-1:17);
        }
    }
    if (controls || packet[3] || packet[4] || packet[5] || packet[6] || packet[7])
        CALL0(0x010221a4);
}

/* Called from the foreground run loop. The native scanner still handles
   button debouncing and housekeeping on its own timer. */
void lite2_usb_poll(void) {
    unsigned trial=STATE->radio.trial;
    if (trial!=TRIAL_IDLE && trial<TRIAL_ACKED) {
        if (READ8(0x20152)!=2 || READ8(0x20184) || STATE->slot_lease_ms || STATE->transports!=2)
            STATE->radio.trial=TRIAL_IDLE;
        else if (trial==TRIAL_READY) {
            /* Outside input/timer callbacks. Like the stock pairing path,
               SET_VISIBILITY waits at most 500 ms for the controller ACK. */
            if (((int (*)(void))0x01020a81)()) STATE->radio.trial=TRIAL_IDLE;
            else {
                int ack=((int (*)(unsigned,unsigned,unsigned))0x0101ab8d)(0,0,0);
                STATE->radio.trial=ack==1 ? TRIAL_ACKED : TRIAL_FAILED;
            }
        }
    }
    if (STATE->transports==1 && !READ8(0x20184) &&
        ((int (*)(void))0x01020a81)())
        lite2_dispatch(UINT32_MAX,UINT32_MAX,0);
}
