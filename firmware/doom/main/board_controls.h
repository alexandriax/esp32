/* Waveshare ESP32-C6-Touch-AMOLED-2.16 board services for the Doom app. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the shared I2C bus, powers and resets the AMOLED, resets touch, and
 * enables the PMIC's latched short-press event. Call before display_init(). */
void board_controls_init(void);

/* Returns true for a valid CST9220 sample. On release, x/y are the last valid
 * contact coordinates. On I2C failure, returns false and leaves outputs alone. */
bool board_touch_read(int *x, int *y, bool *down);

/* One true result per PMIC-classified PWR short tap; the initial boot tap is
 * discarded. Hardware long-hold power behavior is left to the PMIC. */
bool board_power_tap(void);

/* -1 means no measured battery percentage; 0 mV means no valid battery
 * voltage. Values are cached for five seconds to spare the shared I2C bus. */
int board_battery_percent(void);
int board_battery_mv(void);

/* Power the ES8311 analog supply before configuring the codec. Keep the
 * amplifier separate so it cannot click during the register sequence. */
bool board_audio_power(bool enabled);
bool board_audio_amplifier(bool enabled);

/* Confirm a pending OTA boot after Doom has completed its first frame.
 * Also succeeds when this image was confirmed previously. */
bool board_confirm_doom_boot(void);

/* Select the factory Moss image, then restart. Never returns. */
void board_return_to_moss(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
