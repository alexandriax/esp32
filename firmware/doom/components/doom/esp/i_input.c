// Touch targets, the Waveshare board's two side buttons, and serial diagnostics
// produce Doom key events. An action's keys cover its in-game and menu uses.
#include "pico.h"
#include <string.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "doomtype.h"
#include "d_event.h"
#include "doomkeys.h"
#include "i_input.h"
#include "m_controls.h"
#include "doom/doomstat.h"
#include "doom/m_menu.h"
#include "input_action.h"
#include "board_controls.h"
#include "driver/usb_serial_jtag.h"
#include "display.h"
#include "nvs.h"
#include "esp_vfs_usb_serial_jtag.h"

float mouse_acceleration = 2.0;
int mouse_threshold = 10;
int novert = 0;

// Logical actions shared by the physical buttons, touch targets, and serial input.
typedef struct { uint32_t pad_bit; uint8_t keys[3]; } vkey_t;
static const vkey_t vkeys[] = {
    { PAD_UP,     { KEY_UPARROW } },
    { PAD_DOWN,   { KEY_DOWNARROW } },
    { PAD_LEFT,   { KEY_LEFTARROW } },
    { PAD_RIGHT,  { KEY_RIGHTARROW } },
    { PAD_A,      { KEY_RCTRL, KEY_ENTER, 'y' } },     // fire / menu forward / confirm prompt
    { PAD_B,      { ' ', KEY_BACKSPACE, 'n' } },       // use / menu back / decline prompt
    { PAD_X,      { KEY_RALT } },                      // strafe modifier
    { PAD_Y,      { KEY_RSHIFT } },                    // run
    { PAD_L,      { '[' } },                           // previous weapon (bound below)
    { PAD_R,      { ']' } },                           // next weapon
    { PAD_START,  { KEY_ESCAPE } },                    // menu
    { PAD_SELECT, { KEY_TAB } },                       // automap
};

static uint32_t previous_keys[8]; // One bit per Doom key code (0..255).

static void post(int type, int key)
{
    event_t ev = { .type = type, .data1 = key, .data2 = key < 128 ? key : 0, .data3 = key < 128 ? key : 0 };
    D_PostEvent(&ev);
}

static void post_key_transitions(uint32_t vk)
{
    uint32_t active_keys[8] = {0};
    for (unsigned i = 0; i < count_of(vkeys); i++) {
        if (!(vk & vkeys[i].pad_bit)) continue;
        for (int k = 0; k < 3 && vkeys[i].keys[k]; k++) {
            unsigned key = vkeys[i].keys[k];
            active_keys[key >> 5] |= 1u << (key & 31);
        }
    }
    // Different sources can hold the same key, e.g. touch TURN and serial LEFT.
    // Releasing one source must not release the other's held key.
    for (unsigned word = 0; word < count_of(active_keys); word++) {
        uint32_t released = previous_keys[word] & ~active_keys[word];
        uint32_t pressed = active_keys[word] & ~previous_keys[word];
        while (released) {
            unsigned bit = __builtin_ctz(released);
            post(ev_keyup, word * 32 + bit);
            released &= released - 1;
        }
        while (pressed) {
            unsigned bit = __builtin_ctz(pressed);
            post(ev_keydown, word * 32 + bit);
            pressed &= pressed - 1;
        }
        previous_keys[word] = active_keys[word];
    }
}

// Side buttons remain held controls: KEY fires/accepts, BOOT uses/goes back.
// Pressing both opens or closes Doom's menu without needing the touchscreen.
// A short PMIC PWR tap returns to Moss. Doom's own Save Game menu is available
// from the on-screen MENU target before leaving.
void audio_set_mute(bool m); bool audio_is_muted(void);
void doom_toast(const char *line1, const char *line2);
void extras_poll(uint32_t pad_down, bool in_level);
void extras_battery_toast(void);
void doom_power_off(void) { board_return_to_moss(); }
static uint32_t hardware_vkeys(void)
{
    static bool menu_chord;
    if (board_power_tap()) board_return_to_moss();
    const bool key = gpio_get_level(GPIO_NUM_10) == 0;
    const bool boot = gpio_get_level(GPIO_NUM_9) == 0;
    if (key && boot) menu_chord = true;
    if (menu_chord) {
        if (key || boot) return PAD_START;
        menu_chord = false;
    }
    return (key ? PAD_A : 0) | (boot ? PAD_B : 0);
}

static uint32_t touch_vkeys(void)
{
    static uint32_t held;
    static bool down, viewport_gesture;
    static int start_x;
    static int64_t last_valid_touch;
    static int64_t weapon_until;
    static uint32_t weapon_bit;
    int x = 0, y = 0;
    bool contact = false;
    int64_t now = esp_timer_get_time();
    if (board_touch_read(&x, &y, &contact)) {
        last_valid_touch = now;
        if (contact && !down) {
            start_x = x;
            viewport_gesture = y < DOOM_VIEW_HEIGHT;
        }
        down = contact;
        held = 0;
        if (down && viewport_gesture && y < DOOM_VIEW_HEIGHT) {
            // A horizontal swipe across the picture selects the previous or
            // next weapon. This leaves the bottom controls usable for holds.
            if (x - start_x > 80) { weapon_bit = PAD_R; weapon_until = now + 120000; start_x = x; }
            else if (start_x - x > 80) { weapon_bit = PAD_L; weapon_until = now + 120000; start_x = x; }
        } else if (down && !viewport_gesture && y >= DOOM_VIEW_HEIGHT) {
            unsigned column = (unsigned)x / 120, row = (unsigned)(y - DOOM_VIEW_HEIGHT) / 60;
            if (column < 4 && row < 2) {
                static const uint32_t targets[2][4] = {
                    {PAD_LEFT, PAD_UP, PAD_A, PAD_START},
                    {PAD_RIGHT, PAD_DOWN, PAD_B, PAD_SELECT},
                };
                held = targets[row][column];
            }
        }
        if (!down) viewport_gesture = false;
    } else if (down && now - last_valid_touch > 150000) {
        // Transient I2C errors should not flicker a held action, but a failed
        // controller must not leave movement or fire pressed indefinitely.
        down = viewport_gesture = false;
        held = 0;
    }
    return held | (now < weapon_until ? weapon_bit : 0);
}

// Bench input: serial keys act as controls held for 120 ms per keystroke:
// w/s/a/d move/turn, j fire, k use, u strafe, i run, o/p weapons,
// q menu, e automap.
static uint32_t serial_vkeys(void)
{
    static const char keys[] = "wsadjkuiopqe";
    static const uint32_t bits[] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R,
                                     PAD_START, PAD_SELECT };
    static int64_t held_until[sizeof keys - 1];
    int64_t now = esp_timer_get_time();
    uint8_t c;
    while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
        const char *k = memchr(keys, c, sizeof keys - 1);
        if (k) held_until[k - keys] = now + 120000;
    }
    uint32_t m = 0;
    for (unsigned i = 0; i < sizeof keys - 1; i++) if (held_until[i] > now) m |= bits[i];
    return m;
}

void I_InputInit(void)
{
    key_prevweapon = '[';
    key_nextweapon = ']';
    nvs_handle_t h; uint8_t m = 0;
    if (nvs_open("doom", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, "mute", &m); nvs_close(h); }
    audio_set_mute(m);
    if (m) printf("sound muted (NVS doom/mute)\n");
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb);
    esp_vfs_usb_serial_jtag_use_driver();   // console printf must go through the same driver, or the two fight over the FIFO and hang
}

// Keep the display active during play and attract mode. Moss handles its own
// sleep policy after returning from this separate app.
int doom_idle_sleep_s = 0;

void I_GetEvent(void)
{
    static bool boot_modifier_latched;
    uint32_t serial = serial_vkeys();
    uint32_t touch = touch_vkeys();
    uint32_t hardware = hardware_vkeys();
    // BOOT modifies a held touch direction in a level: run forward/backward,
    // or run and strafe left/right. Without a touch direction it remains USE.
    // This uses the board's second physical input to expose Doom's movement
    // modifiers even though the touch controller reports only one contact.
    if (!(hardware & PAD_B)) boot_modifier_latched = false;
    if (gamestate == GS_LEVEL && !menuactive && !demoplayback &&
        (hardware & PAD_B) && (touch & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT))) {
        boot_modifier_latched = true;
        hardware |= PAD_Y;
        if (touch & (PAD_LEFT | PAD_RIGHT)) hardware |= PAD_X;
    }
    // Releasing the touch direction while BOOT is still down must not press
    // USE. BOOT can act as USE again after it is released and pressed anew.
    if (boot_modifier_latched) hardware &= ~PAD_B;
    uint32_t vk = serial | touch | hardware;
    {
        static uint32_t previous_actions;
        extras_poll(vk & ~previous_actions, gamestate == GS_LEVEL && !menuactive && !demoplayback);
        previous_actions = vk;
    }
    post_key_transitions(vk);
}

void I_GetEventTimeout(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
    I_GetEvent();
}

void I_StartTextInput(int x1, int y1, int x2, int y2) {}
void I_StopTextInput(void) {}
void I_ReadMouse(void) {}
void I_BindInputVariables(void) {}
int GetTypedChar(int scancode, boolean shiftdown) { return 0; }
