#include "mouse_core.h"
#include <string.h>

enum { Y = 0x20, X = 0x10, R3 = 4, L3 = 2, STICK_DEAD_ZONE = 14 };

static int32_t magnitude(int32_t value) { return value < 0 ? -value : value; }

void mouse_reset(MouseState *state) {
    memset(state, 0, sizeof(*state));
}

void put_signed16(uint8_t *p, int32_t value) {
    if (value > 32767) value = 32767;
    if (value < -32767) value = -32767;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)((uint32_t)value >> 8);
}

/* Pack the keyboard bits and the two Consumer Control bits. */
uint8_t keyboard_keys(uint32_t buttons) {
    return ((buttons >> 12) & 1) | ((buttons >> 8) & 32) |
           ((buttons >> 5) & 30) | ((buttons >> 10) & 64);
}

/* Five-knot slow Hermite preset. Output units are 0.2 percent. */
static unsigned slow_response(unsigned radius) {
    static const uint8_t samples[] = {0,0,0,0,0,0,1,2,4,7,10,14,18,22,27,32,39,47,57,67,77,88,100,112,124,139,154,170,187,204,221,238,250};
    if (radius <= 19) return 0;
    if (radius >= 127) return 250;
    unsigned i = radius >> 2;
    unsigned response = samples[i] + ((samples[i+1] - samples[i]) * (radius & 3) + 2) / 4;
    return response ? response : 1;
}

static unsigned stick_radius(unsigned squared) {
    unsigned radius = 0;
    for (unsigned bit = 128; bit; bit >>= 1) {
        unsigned next = radius | bit;
        if (next * next <= squared) radius = next;
    }
    return radius;
}

static int32_t displacement(int32_t *fraction, uint8_t *moving, int32_t velocity,
                            uint32_t dt, int clear_on_stop) {
    *moving = velocity != 0;
    if (!velocity && clear_on_stop) *fraction = 0;
    *fraction += velocity * (int32_t)dt;
    int32_t counts = clear_on_stop ? *fraction / 1024 : (*fraction + 512) >> 10;
    *fraction -= counts * 1024;
    return counts;
}

static void move(MouseState *s, int32_t x, int32_t y, uint32_t dt, uint8_t *output,
                 int clear_on_stop) {
    put_signed16(output + 1, displacement(&s->fraction_x, &s->moving_x, x, dt, clear_on_stop));
    put_signed16(output + 3, displacement(&s->fraction_y, &s->moving_y, y, dt, clear_on_stop));
}

/* Accumulate direction evidence, in capped milliseconds. Individual noise
   spikes and delayed samples cannot by themselves open the fine-motion gate.
   Keep the calibrated output linear once the direction is established. */
static int32_t fine_gyro(int32_t value, int16_t *trend, int32_t *trim, uint32_t dt) {
    int32_t residual = value * 256 - *trim / 64;
    int32_t level = magnitude(residual);
    int32_t elapsed = dt > 10 ? 10 : (int32_t)dt;
    int32_t score = residual < 0 ? -*trend : *trend;
    if (score < 0) score /= 2;
    if (level >= 1792) elapsed *= 2;
    if (level >= 4096) elapsed *= 2;
    if (level < 448) elapsed /= 4;
    if (level < 192) score /= 2;
    else score += elapsed;
    if (score > 80) score = 80;
    *trend = residual < 0 ? -score : score;
    if (level >= 8192 || score == 80)
        return (residual + (residual >= 0 ? 128 : -128)) / 256;
    if (magnitude(score) <= 4) *trim += residual * (int32_t)dt / 192;
    return 0;
}

static void calibrate_gyro(MouseState *s, const int16_t imu[6],
                           uint32_t dt, uint32_t buttons) {
    /* R3 resets gyro calibration without resetting the input mode or wheel. */
    if (buttons & R3) {
        s->calibrated = s->have_anchor = 0;
        s->calibration_ms = 0;
        s->sum_x = s->sum_z = s->fraction_x = s->fraction_y = 0;
        s->moving_x = s->moving_y = 0;
        s->fine_x = s->fine_y = 0;
        return;
    }
    /* Historic *_x state fields now store gyro Y (normal forward grip). */
    int32_t gx = imu[4], gy = imu[3], gz = imu[5];
    /* Reject missing data and obvious movement. Sensor scale is inherited
       from the stock driver; these are raw-count stability thresholds. */
    int stable = (imu[0] || imu[1] || imu[2]) && magnitude(gx) < 32
               && magnitude(gy) < 32 && magnitude(gz) < 32;
    if (s->have_anchor && (magnitude(gx - s->anchor_x) > 9
                       || magnitude(gz - s->anchor_z) > 9)) stable = 0;
    if (!stable) {
        /* One missing sample or noise spike must not erase a nearly complete
           resting window. Consecutive rejected samples restart acquisition. */
        if (s->have_anchor == 1) {
            s->have_anchor = 2;
            return;
        }
        s->sum_x = s->sum_z = 0;
        s->calibration_ms = 0;
        s->have_anchor = 0;
        return;
    }
    if (!s->have_anchor) {
        s->anchor_x = gx; s->anchor_z = gz;
    }
    s->have_anchor = 1;
    s->sum_x += gx * (int32_t)dt;
    s->sum_z += gz * (int32_t)dt;
    s->calibration_ms += dt;
    if (s->calibration_ms >= 600) {
        s->bias_x = s->sum_x / (int32_t)s->calibration_ms;
        s->bias_z = s->sum_z / (int32_t)s->calibration_ms;
        /* X pointer is negative sensor Z; Y follows sensor Y. */
        s->sum_z = -(s->sum_z % s->calibration_ms) * 16384 / s->calibration_ms;
        s->sum_x = (s->sum_x % s->calibration_ms) * 16384 / s->calibration_ms;
        s->calibrated = 1;
    }
}

int mouse_step(MouseState *s, const int16_t imu[6], uint32_t buttons,
                uint32_t dt, uint8_t output[9], uint32_t axes) {
    for (uint32_t i = 0; i < 9; ++i) output[i] = 0;
    output[0] = ((buttons & Y) ? 1 : 0) | ((buttons & X) ? 2 : 0);

    int32_t stick_x = (int32_t)(axes & 255) - 128;
    int32_t stick_y = (int32_t)((axes >> 8) & 255) - 128;
    int32_t radius_squared = stick_x * stick_x + stick_y * stick_y;
    int stick_crossed_half = !s->stick_over_half && radius_squared >= 64 * 64;
    if (stick_crossed_half) s->stick_over_half = 1;
    if (radius_squared < 48 * 48) s->stick_over_half = 0;

    unsigned prior_mode = s->pointer_mode;
    if (buttons & L3) {
        if (!s->l3_latched) {
            s->pointer_mode = prior_mode == MODE_GYRO ? MODE_AUTO : prior_mode + 1;
            s->auto_stick = s->gyro_motion_ms = 0;
            s->auto_hold_ms = 0;
        }
        s->l3_latched = 1;
        s->l3_release_ms = 0;
    } else if (s->l3_latched && dt <= 100) {
        s->l3_release_ms += dt;
        if (s->l3_release_ms >= 30) s->l3_latched = 0;
    }

    int32_t wheel_offset = 128 - (int32_t)(axes >> 24);
    int32_t tilt = magnitude(wheel_offset) * 100 > 15 * 127;
    int32_t direction = tilt <= 0 ? 0 : (wheel_offset > 0 ? 1 : -1);
    if (direction != s->wheel_direction || dt > 100) {
        s->wheel_phase = 0;
        s->wheel_direction = direction;
        output[5] = output[6] = (uint8_t)direction;
    } else if (direction) {
        /* 150000 phase units per detent, 2500 per Bluetooth subdivision.
           Keep the original slow curve and the 15 detents/sec maximum. */
        int32_t phase = s->wheel_phase + (int32_t)slow_response(magnitude(wheel_offset)) * 9 * (int32_t)dt;
        output[6] = (uint8_t)((phase/2500 - s->wheel_phase/2500) * direction);
        while (phase >= 150000) {
            output[5] += direction;
            phase -= 150000;
        }
        s->wheel_phase = phase;
    }
    if (dt > 100) {
        s->fraction_x = s->fraction_y = 0;
        s->moving_x = s->moving_y = 0;
        s->fine_x = s->fine_y = 0;
        s->gyro_motion_ms = 0;
        s->click_ms = 0;
        s->previous_clicks = output[0];
        return 0;
    }
    /* Auto needs the gyro while steering with the stick to detect a takeover. */
    if (s->pointer_mode != MODE_STICK && dt && (!s->calibrated || (buttons & R3)))
        calibrate_gyro(s, imu, dt, buttons);
    int gyro_valid = !(buttons & R3) && (imu[0] || imu[1] || imu[2]);
    int32_t gyro_x = -((int32_t)imu[5] - s->bias_z);
    int32_t gyro_y = (int32_t)imu[4] - s->bias_x;
    int was_auto_stick = s->auto_stick;
    if (s->pointer_mode == MODE_AUTO && prior_mode == MODE_AUTO && !(buttons & L3)) {
        if (stick_crossed_half) {
            s->auto_stick = 1;
            s->gyro_motion_ms = 0;
            s->auto_hold_ms = 2000;
        } else if (s->auto_stick && (magnitude(stick_x) > STICK_DEAD_ZONE || magnitude(stick_y) > STICK_DEAD_ZONE)) {
            s->auto_hold_ms = 2000;
            s->gyro_motion_ms = 0;
        } else if (s->auto_stick && s->auto_hold_ms) {
            s->auto_hold_ms = dt >= s->auto_hold_ms ? 0 : s->auto_hold_ms - dt;
        } else if (s->auto_stick && gyro_valid &&
                   (magnitude(gyro_x) >= 700 || magnitude(gyro_y) >= 700)) {
            if (dt >= 20 - (uint32_t)s->gyro_motion_ms) {
                s->auto_stick = s->gyro_motion_ms = 0;
            } else {
                s->gyro_motion_ms += dt;
            }
        } else {
            s->gyro_motion_ms = 0;
        }
    }
    if (prior_mode != s->pointer_mode || was_auto_stick != s->auto_stick) {
        s->fraction_x = s->fraction_y = 0;
        s->moving_x = s->moving_y = 0;
        s->fine_x = s->fine_y = 0;
    }

    int stick = s->pointer_mode == MODE_STICK || (s->pointer_mode == MODE_AUTO && s->auto_stick);
    uint8_t clicks = output[0];
    if (stick) s->click_ms = 0;
    else if (clicks != s->previous_clicks) s->click_ms = (clicks & ~s->previous_clicks) ? 200 : 0;
    else s->click_ms = dt >= s->click_ms ? 0 : s->click_ms - dt;
    int hold = s->click_ms != 0;
    s->previous_clicks = clicks;
    if (stick) {
        unsigned radius = stick_radius(radius_squared);
        int32_t speed = slow_response(radius) * 5;
        int32_t divisor = radius ? (int32_t)radius : 1;
        move(s, speed * stick_x / divisor, speed * stick_y / divisor, dt, output, 1);
    } else if (gyro_valid) {
        if (s->calibrated) {
            gyro_x = fine_gyro(gyro_x, &s->fine_x, &s->sum_z, dt);
            gyro_y = fine_gyro(gyro_y, &s->fine_y, &s->sum_x, dt);
        } else {
            if (magnitude(gyro_x) < 32) gyro_x = 0;
            if (magnitude(gyro_y) < 32) gyro_y = 0;
        }
        if (!hold) move(s, gyro_x * 9, gyro_y * 4, dt, output, 0);
    } else {
        s->fine_x = s->fine_y = 0;
    }
    if (hold) s->fraction_x = s->fraction_y = 0;
    return hold;
}
