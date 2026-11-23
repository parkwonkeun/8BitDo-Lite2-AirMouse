#ifndef LITE2_MOUSE_CORE_H
#define LITE2_MOUSE_CORE_H
#include <stdint.h>

enum { MODE_AUTO, MODE_STICK, MODE_GYRO };
enum { CAPTURE_BUTTON = 0x10000, POWER_BUTTON = 0x20000 };

/* State is owned by the adapter; this module has no hidden globals. */
typedef struct {
    int32_t sum_x, sum_z, fraction_x, fraction_y;
    int16_t bias_x, bias_z, anchor_x, anchor_z;
    int32_t wheel_direction;
    uint16_t calibration_ms;
    uint8_t click_ms, previous_clicks;
    uint8_t calibrated, have_anchor;
    uint8_t pointer_mode, auto_stick;
    uint16_t auto_hold_ms;
    uint8_t stick_over_half, gyro_motion_ms, l3_release_ms, l3_latched;
    uint8_t moving_x, moving_y;
    int16_t fine_x, fine_y;
    int32_t wheel_phase;
} MouseState;

void put_signed16(uint8_t *p, int32_t value);
void mouse_reset(MouseState *state);
int mouse_step(MouseState *state, const int16_t imu[6], uint32_t buttons,
                uint32_t elapsed_ms, uint8_t output[9], uint32_t axes);
uint8_t keyboard_keys(uint32_t buttons);
#endif
