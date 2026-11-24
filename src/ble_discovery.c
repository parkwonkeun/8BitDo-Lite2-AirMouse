/* r50 S-mode discovery experiment. The generated header contains only LE
   records for the v1.06 radio's bounded, byte-addressed configuration areas. */
#include <stdint.h>
#include "ble_discovery_profile.h"

static void copy_radio(uintptr_t dest, const uint8_t *data, unsigned size) {
    volatile uint8_t *out = (volatile uint8_t *)dest;
    while (size--) *out++ = *data++;
}

__attribute__((section(".entry")))
int ble_pair_visibility(unsigned inquiry, unsigned page, unsigned le) {
    typedef int (*SetVisibility)(unsigned, unsigned, unsigned);
    SetVisibility set = (SetVisibility)0x0101ab8d;
    if (inquiry && page && *(volatile uint8_t *)0x20184 == 0 &&
        *(volatile uint32_t *)0x2fff8 == 0x20221031) {
        /* Only install once per radio initialization. In particular, leave
           the host's CCCD intact on later visibility notifications. */
        if (*(volatile uint8_t *)0xc4382 != gap_name[1]) {
            copy_radio(0xc4975, hid_attributes, sizeof hid_attributes);
            copy_radio(0xc4341, advertisement, sizeof advertisement);
            copy_radio(0xc4361, scan_response, sizeof scan_response);
            copy_radio(0xc4381, gap_name, sizeof gap_name);
        }
        return set(0, 0, 1);
    }
    return set(inquiry, page, le);
}
