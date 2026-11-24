/* S-mode BLE owns its three pointer-mode indicators. Only an explicit pairing
   boot can delegate player LEDs to the stock blinker before link-up.
   The byte is in r52's reserved private RAM beyond the Controls structure. */
#include "mouse_core.h"
#define B(a) (*(volatile uint8_t *)(a))
#define LINK_SEEN B(0x2fdfc)

extern void r52_start(void);
extern void r52_link_log(const char *);
extern void r52_lights(unsigned,unsigned);
extern void r52_timer(void *);
extern void mode_led(unsigned);
extern int usb_active(void);

#ifndef BLE_MONITOR
void led_start(void) {
    LINK_SEEN=0;
    r52_start();
}
#endif
#ifdef BLE_MONITOR
#define LED_CODE __attribute__((section(".monitor_led"),noinline))
#else
#define LED_CODE
#endif
static LED_CODE int ble_profile(void) {
    return !B(0x20184) && B(0xc448e)==0x83 && B(0xc448f)==0x46;
}
__attribute__((section(".event"))) void led_link(const char *message) {
    if (ble_profile()) LINK_SEEN=1;
    r52_link_log(message);
}
static LED_CODE int pointer_light(void) {
    if (!ble_profile() || usb_active()) return 0;
    if (!LINK_SEEN && *(volatile uint32_t *)0x2fff8==0x20221031) return 0;
    mode_led(((const MouseState *)0x2fe00)->pointer_mode);
    return 1;
}
LED_CODE void led_timer(void *arg) {
    if (!pointer_light()) r52_timer(arg);
}
LED_CODE void led_write(unsigned mask,unsigned on) {
    if (!pointer_light()) r52_lights(mask,on);
}
