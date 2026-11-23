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
typedef struct {
    uint32_t scans, imu_reads, low_scans, rejected;
    uint8_t defer;
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
    uint8_t power_armed, power_was_down, home_pending, fast_motion;
    uint16_t trace_scans;
    uint8_t mouse_serial, pressure_flags;
    RadioStats radio;
} Adapter;
#define STATE ((Adapter *)0x0002fe00)
_Static_assert(sizeof(Adapter) <= 256, "reserved RAM overflow");

extern const uint8_t lite2_sdp_recipe[];
extern const unsigned lite2_mouse_sdp_size;
const char lite2_mouse_name[] = "Lite2 Air Mouse Plus";

__attribute__((noreturn)) void lite2_start(void) {
    memset(STATE, 0, 256);
    CALL0(0x010271e4);
    for (;;) {}
}

/* Keep stock radio headroom while continuing to sample S wireless inputs. */
unsigned lite2_scan_pressure(void) {
    int free_bytes = ((int (*)(void))0x010205cd)();
    unsigned route = 0;
    if (free_bytes < 700 && READ8(0x20152) == 2)
        route = READ8(0x20184) == 0 ? 2 : 1;
    STATE->radio.defer = route == 2;
    if (route == 2) ++STATE->radio.low_scans;
    return route;
}

int lite2_set_device_class(unsigned stock_class) {
    /* Classic peripheral major class, both keyboard and pointing bits. */
    unsigned device_class = READ8(0x20184) == 0 ? 0x05c0 : stock_class;
    return ((int (*)(unsigned))0x0101a9c1)(device_class);
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

/* The stock scanner rearms this timer after each dispatch. Shorten its
   next interval while either wireless pointing source is moving. */
void lite2_scan_delay(void *timer, unsigned delay) {
    if (STATE->active && STATE->fast_motion) delay = 4;
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
        STATE->mouse.sum_x = STATE->mouse.sum_z = 0;
        STATE->mouse.fine_x = STATE->mouse.fine_y = 0;
        memset(&STATE->initialized, 0, offsetof(Adapter, radio) - offsetof(Adapter, initialized));
        STATE->last_keys = STATE->last_consumer = 0xff;
        STATE->transports = transports;
    }
    if (!transports) return;
    if (axes == UINT32_MAX) axes = READ32(0x201a0);
    if (!axes) axes = 0x80808080;  /* Before the first valid stock ADC sample. */
    /* The Bluetooth run loop timestamps timers with 0x204f0, in units
       of the period programmed by the stock timer driver. */
    uint32_t tick = bluetooth && !usb ? READ32(0x204f0) : READ32(0x20034);
    uint32_t dt = 0, period = READ8(0x2022b);
    if (period < 5 || period > 100) period = 10;
    if (STATE->initialized) {
        uint32_t delta = (tick - STATE->last_tick) & 0x7fffffff;
        if (delta <= 100 && delta * period <= 100) dt = delta * period;
        else dt = 101;
        /* The stock input timer remains the fallback when wireless low-power
           operation stops or resets SysTick. No second transport invocation. */
        if (!usb && (dt == 0 || dt > 100)) dt = period;
    }
    STATE->last_tick = tick; STATE->initialized = 1;
    int16_t imu[6] = {0};
    unsigned prior_mode = STATE->mouse.pointer_mode;
    unsigned prior_auto_stick = STATE->mouse.auto_stick;
    int ok = 0;
    if (prior_mode != MODE_STICK)
        ok = ((int (*)(int16_t *))0x0101d60d)(imu);
    if (bluetooth) {
        ++STATE->radio.scans;
        if (ok) ++STATE->radio.imu_reads;
    }
    if (!ok) imu[0] = imu[1] = imu[2] = 0;
    uint8_t packet[19] = {0xa1,1};
    int click_hold = mouse_step(&STATE->mouse,imu,buttons,dt,packet+2,axes);
    int fine_wheel = (int8_t)packet[8];
    if (click_hold || prior_mode != STATE->mouse.pointer_mode ||
        prior_auto_stick != STATE->mouse.auto_stick)
        STATE->count = 0;
    set_mode_led(STATE->mouse.pointer_mode);
    /* The stock S scanner can mask Capture for its button combinations. Read
       its physical bit before filtering so the star remains a Home shortcut. */
    uint32_t physical_buttons = ((uint32_t (*)(void))0x01021b65)();
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
        else if (usb || !(STATE->mouse.moving_x || STATE->mouse.moving_y)) {
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
    int wheel = STATE->pending_wheel + (int8_t)packet[7];
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
        put_signed16(packet+8, STATE->mouse.bias_z - (int32_t)imu[5]);
        put_signed16(packet+10, (int32_t)imu[4] - STATE->mouse.bias_x);
        source = 1;
    }
    STATE->fast_motion = !usb && (STATE->mouse.wheel_direction || packet[2] || (source && (STATE->mouse.moving_x || STATE->mouse.moving_y)));
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
        if ((physical_buttons & 0x3ffff) == 9) STATE->trace_scans = 12000;
        if (STATE->radio.defer) STATE->pressure_flags |= 1;
        if (STATE->trace_scans) {
            --STATE->trace_scans;
            unsigned lx = (uint8_t)axes >> 3, ly = (uint8_t)(axes >> 8) >> 3;
            unsigned rx = (uint8_t)(axes >> 16) >> 3;
            if ((axes >> 24)==128 && rx-15 <= 1 && (source==2 || (lx-15 <= 1 && ly-15 <= 1)) && period < 32) {
                tag = (tag & 0x700) | (period << 11) | READ8(0xc8040);
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
    if (accumulated>127) accumulated=127;
    if (accumulated< -127) accumulated=-127;
    packet[18]=(uint8_t)accumulated;
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
    STATE->pending_fine_wheel=bluetooth && (report || !accepted)?accumulated:0;
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
        if (diagnostic_only && !report) {
            /* Hold + only when collecting a diagnostic trace over USB. */
            uint8_t diagnostic[64] = {3,2};
            diagnostic[2] = ok; diagnostic[3] = STATE->mouse.calibrated;
            diagnostic[4] = bluetooth; diagnostic[5] = period;
            diagnostic[6] = dt; diagnostic[7] = READ8(0x20184);
            for (unsigned i=0;i<sizeof(imu);++i) diagnostic[8+i]=((const uint8_t *)imu)[i];
            __builtin_memcpy(diagnostic+20,&buttons,sizeof(buttons));
            diagnostic[34] = 'R'; diagnostic[35] = 31;
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
