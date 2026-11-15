/*
 * Joyfon - a USB HID game controller for the Raspberry Pi Pico.
 *
 * Mimics a Game Boy Color button layout (D-pad + A/B + START/SELECT) and
 * presents itself to a host as a wired DualShock 4.
 *
 * A generic HID gamepad works on Android, PC and Linux but is invisible on
 * iOS/iPadOS: Apple's GameController framework only attaches to controllers
 * it recognises by USB VID/PID. Speaking the DualShock 4's identity and
 * input report gets an iPhone to bind a real controller profile, and the
 * same descriptor is understood everywhere else too.
 *
 * Wiring (all buttons are active-low, i.e. wired to GND, using the internal
 * pull-ups - external pull-ups are also fine):
 *
 *   GPIO0  -> D-pad DOWN
 *   GPIO1  -> D-pad RIGHT
 *   GPIO2  -> D-pad UP
 *   GPIO3  -> D-pad LEFT
 *   GPIO4  -> B
 *   GPIO5  -> A
 *   GPIO20 -> X
 *   GPIO21 -> Y
 *   GPIO22 -> SR (right shoulder)
 *   GPIO25 -> SL (left shoulder)
 *   GPIO23 -> START
 *   GPIO24 -> SELECT
 *   GPIO8  -> status LED (active-high)
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "tusb.h"

#include "usb_descriptors.h"
#include "ds4.h"

//--------------------------------------------------------------------+
// Pin configuration
//--------------------------------------------------------------------+

#define PIN_DOWN    0u
#define PIN_RIGHT   1u
#define PIN_UP      2u
#define PIN_LEFT    3u
#define PIN_B       4u
#define PIN_A       5u
#define PIN_X       20u
#define PIN_Y       21u
#define PIN_SR      22u
#define PIN_SL      25u
#define PIN_START   23u
#define PIN_SELECT  24u

#define PIN_LED     8u

// All face/dpad buttons used by the controller, so we can init them in a loop.
static const uint8_t k_button_pins[] = {
  PIN_DOWN, PIN_RIGHT, PIN_UP, PIN_LEFT,
  PIN_B, PIN_A, PIN_X, PIN_Y, PIN_SR, PIN_SL, PIN_START, PIN_SELECT,
};

// Buttons are active-low (pressed == logic 0 thanks to the pull-up).
static inline bool button_pressed(uint8_t pin)
{
  return !gpio_get(pin);
}

//--------------------------------------------------------------------+
// Status LED
//--------------------------------------------------------------------+

// Blink intervals (ms) signalling the USB connection state.
enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_SUSPENDED   = 1000,
};

// 0 means "don't blink, let the application drive the LED directly".
static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

static inline uint32_t now_ms(void)
{
  return to_ms_since_boot(get_absolute_time());
}

static void board_setup(void)
{
  for ( size_t i = 0; i < sizeof(k_button_pins) / sizeof(k_button_pins[0]); i++ )
  {
    uint8_t pin = k_button_pins[i];
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    // gpio_pull_up(pin);
  }

  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, GPIO_OUT);
  gpio_put(PIN_LED, 0);
}

//--------------------------------------------------------------------+
// USB device callbacks
//--------------------------------------------------------------------+

void tud_mount_cb(void)
{
  blink_interval_ms = 0; // application now controls the LED
}

void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

void tud_resume_cb(void)
{
  blink_interval_ms = tud_mounted() ? 0 : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// BOOTSEL (firmware reflash) gesture
//--------------------------------------------------------------------+

// Press START + SELECT together this many times, within this window of the
// first press, to reboot into the ROM USB bootloader for reflashing.
enum {
  BOOTSEL_COMBO_PRESSES   = 3,
  BOOTSEL_COMBO_WINDOW_MS = 2000,
};

// Watch for the START+SELECT chord being tapped repeatedly. On the Nth tap
// within the window we hand control back to the RP2040 boot ROM, which
// re-enumerates as the "RPI-RP2" mass-storage drive so the host can drag a
// fresh UF2 on. reset_usb_boot() does not return.
static void bootsel_task(void)
{
  static bool     combo_was_down = false;
  static uint8_t  press_count    = 0;
  static uint32_t first_press_ms = 0;

  bool combo_down = button_pressed(PIN_START) && button_pressed(PIN_SELECT);

  // Act on the rising edge of the chord (both freshly pressed together).
  if ( combo_down && !combo_was_down )
  {
    uint32_t t = now_ms();

    // Start a new run if this is the first tap or the previous run went stale.
    if ( press_count == 0 || (t - first_press_ms) > BOOTSEL_COMBO_WINDOW_MS )
    {
      press_count    = 1;
      first_press_ms = t;
    }
    else
    {
      press_count++;
    }

    if ( press_count >= BOOTSEL_COMBO_PRESSES )
    {
      // Visual confirmation the gesture landed: blink the status LED 5 times.
      for ( int i = 0; i < 5; i++ )
      {
        gpio_put(PIN_LED, 1);
        sleep_ms(100);
        gpio_put(PIN_LED, 0);
        sleep_ms(100);
      }

      // Reboot into BOOTSEL. Mask 0,0 == default: enable both the USB MSD
      // interface and the USB activity LED PIN logic the ROM would normally use.
      reset_usb_boot(0, 0);
    }
  }

  combo_was_down = combo_down;
}

//--------------------------------------------------------------------+
// DualShock 4 input report
//--------------------------------------------------------------------+

static uint8_t compute_hat(bool up, bool down, bool left, bool right)
{
  // Opposite directions cancel out so we never report an impossible diagonal.
  if (up && down)    { up = down = false; }
  if (left && right) { left = right = false; }

  if (up && right)   return DS4_HAT_NE;
  if (up && left)    return DS4_HAT_NW;
  if (down && right) return DS4_HAT_SE;
  if (down && left)  return DS4_HAT_SW;
  if (up)            return DS4_HAT_N;
  if (down)          return DS4_HAT_S;
  if (left)          return DS4_HAT_W;
  if (right)         return DS4_HAT_E;
  return DS4_HAT_NONE;
}

static void build_report(ds4_input_report_t *report, uint8_t counter, uint16_t timestamp)
{
  memset(report, 0, sizeof(*report));

  // No analog sticks on this board: report both centred and both triggers released.
  report->lx = report->ly = DS4_STICK_CENTER;
  report->rx = report->ry = DS4_STICK_CENTER;

  report->buttons0 = compute_hat(
      button_pressed(PIN_UP),
      button_pressed(PIN_DOWN),
      button_pressed(PIN_LEFT),
      button_pressed(PIN_RIGHT));

  // Nintendo face layout on the DualShock diamond: A/B and X/Y swapped from
  // the Sony positions so A sits where Circle is and X where Triangle is.
  if (button_pressed(PIN_A))      report->buttons0 |= DS4_BTN_CIRCLE;
  if (button_pressed(PIN_B))      report->buttons0 |= DS4_BTN_CROSS;
  if (button_pressed(PIN_X))      report->buttons0 |= DS4_BTN_TRIANGLE;
  if (button_pressed(PIN_Y))      report->buttons0 |= DS4_BTN_SQUARE;

  if (button_pressed(PIN_SL))     report->buttons1 |= DS4_BTN_R1;
  if (button_pressed(PIN_SR))     report->buttons1 |= DS4_BTN_L1;
  if (button_pressed(PIN_START))  report->buttons1 |= DS4_BTN_SHARE;
  if (button_pressed(PIN_SELECT)) report->buttons1 |= DS4_BTN_OPTIONS;

  // The low two bits of buttons2 are PS and touchpad-click; the counter lives
  // in the upper six and must advance on every report.
  report->buttons2 = (uint8_t) (counter << 2);

  report->timestamp   = timestamp;
  report->status      = DS4_STATUS_FULL;
  report->touch_count = 0;
}

// Poll buttons and stream a report to the host. Unlike a generic HID gamepad
// we send on every interval rather than only on change: the real controller
// streams continuously, and the report counter has to keep moving for drivers
// that use it to detect a stalled device.
static void hid_task(void)
{
  const uint32_t poll_interval_ms = 5;
  static uint32_t last_poll_ms = 0;
  static uint8_t counter = 0;
  static uint16_t timestamp = 0;

  if ( now_ms() - last_poll_ms < poll_interval_ms ) return;
  last_poll_ms = now_ms();

  ds4_input_report_t report;
  build_report(&report, counter, timestamp);

  // Wake the host if we are suspended and the user presses something.
  if ( tud_suspended() )
  {
    if ( (report.buttons0 & 0xF0) || report.buttons1 ||
         (report.buttons0 & 0x0F) != DS4_HAT_NONE )
    {
      tud_remote_wakeup();
    }
    return;
  }

  if ( !tud_hid_ready() ) return;

  if ( tud_hid_report(DS4_REPORT_ID_INPUT, &report, sizeof(report)) )
  {
    counter = (uint8_t) ((counter + 1) & 0x3F);
    // 5.33 us ticks, so a 5 ms interval advances the stamp by ~938.
    timestamp = (uint16_t) (timestamp + 938);
  }
}

//--------------------------------------------------------------------+
// LED feedback
//--------------------------------------------------------------------+

static void led_task(void)
{
  // When mounted (blink_interval_ms == 0) light the LED while any input is
  // active, giving immediate tactile feedback. Otherwise blink to signal the
  // current USB state.
  if ( blink_interval_ms == 0 )
  {
    bool any = false;

    for ( size_t i = 0; i < sizeof(k_button_pins) / sizeof(k_button_pins[0]); i++ )
    {
      if ( button_pressed(k_button_pins[i]) ) { /* any = true;  */ break; }
    }

    gpio_put(PIN_LED, any);
    return;
  }

  static uint32_t last_toggle_ms = 0;
  static bool led_on = false;

  if ( now_ms() - last_toggle_ms < blink_interval_ms ) return;
  last_toggle_ms = now_ms();

  led_on = !led_on;
  gpio_put(PIN_LED, led_on);
}

//--------------------------------------------------------------------+
// Main
//--------------------------------------------------------------------+

int main(void)
{
  board_setup();

  tud_init(BOARD_TUD_RHPORT);

  while (1)
  {
    tud_task();     // service the USB device stack
    bootsel_task(); // watch for the START+SELECT reflash gesture
    hid_task();     // read buttons and send reports
    led_task();     // status / activity LED
  }
}
