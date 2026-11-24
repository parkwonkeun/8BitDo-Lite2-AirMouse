/* Only the native BLE HID link-up event calls this wrapper. Keep its log and
   use the same LED timer stop routine called by the stock link handlers. */
#include "mouse_core.h"
#define BYTE(a) (*(volatile const unsigned char *)(a))

__attribute__((noinline,section(".led_apply")))
static void show_pointer_mode(void) {
    if (BYTE(0x20184)) return;
    ((void (*)(void))0x01022ae1)();
    const MouseState *mouse=(const MouseState *)0x2fe00;
    ((void (*)(unsigned))0x0102c289)(mouse->pointer_mode);
}

__attribute__((section(".led_event")))
void ble_led_connected(const char *message) {
    ((void (*)(const char *))0x0102141d)(message);
    if (BYTE(0xc448e)==0x83 && BYTE(0xc448f)==0x46)
        show_pointer_mode();
}
