#include <stdint.h>

#define BYTE(address) (*(volatile uint8_t *)(address))
#define WORD(address) (*(volatile uint32_t *)(address))
#define TIMEOUT_MS 900000u
#define PHYSICAL_BUTTONS 0x7ffffu

/* Tail of the BLE private RAM, below the LED latch at 0x2fdfc. */
typedef struct {
    uint32_t tick, remaining;
    uint8_t period, linked_mode, held;
} ButtonIdle;
#define IDLE ((volatile ButtonIdle *)0x2fdec)
extern uint32_t read_buttons(void);
extern unsigned usb_active(void);
extern void power_down(unsigned), native_idle(void), previous_start(void);
extern void previous_link(const char *), previous_disconnect(const char *);

static uint8_t tick_period(void) {
    uint8_t p = BYTE(0x2022b);
    return p >= 5 && p <= 100 ? p : 10;
}
static void restart_clock(void) {
    IDLE->remaining = TIMEOUT_MS;
    IDLE->tick = WORD(0x20034);
    IDLE->period = tick_period();
    IDLE->held = 0;
}
void idle_start(void) {
    IDLE->linked_mode = 0;
    previous_start();
}
void idle_link(const char *message) {
    unsigned mode = BYTE(0x20184);
    if (mode <= 1 && IDLE->linked_mode != mode + 1) {
        restart_clock();
        IDLE->linked_mode = mode + 1;
    }
    previous_link(message);
}
void idle_disconnect(const char *message) {
    IDLE->linked_mode = 0;
    previous_disconnect(message);
}
static int managed(void) {
    return IDLE->linked_mode && IDLE->linked_mode == BYTE(0x20184) + 1;
}
void idle_native_check(void) {
    if (!managed() || usb_active()) native_idle();
}
uint32_t idle_scan(void) {
    uint32_t buttons = read_buttons();
    if (!managed()) return buttons;
    if ((buttons & PHYSICAL_BUTTONS) || IDLE->held || usb_active()) {
        restart_clock();
        IDLE->held = (buttons & PHYSICAL_BUTTONS) != 0;
        return buttons;
    }
    uint32_t now = WORD(0x20034);
    uint32_t delta = now - IDLE->tick;
    uint32_t period = IDLE->period;
    IDLE->tick = now;
    IDLE->period = tick_period();
    /* Bound multiplication even across a long scheduling pause or timer wrap. */
    if (IDLE->remaining && (delta >= TIMEOUT_MS || delta * period >= IDLE->remaining)) {
        IDLE->remaining = 0;
        power_down(0);
    } else if (IDLE->remaining) {
        IDLE->remaining -= delta * period;
    }
    return buttons;
}
