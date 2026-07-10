/*
 * Joyfon - a USB HID game controller for the Raspberry Pi Pico.
 *
 * Mimics a Game Boy Color button layout (D-pad + A/B + START/SELECT) and
 * presents itself to a host (e.g. a phone) as a standard USB HID gamepad.
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
 *   GPIO23 -> START
 *   GPIO24 -> SELECT
 *   GPIO8  -> status LED (active-high)
 */

#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// Pin configuration
//--------------------------------------------------------------------+

#define PIN_DOWN    0u
#define PIN_RIGHT   1u
#define PIN_UP      2u
#define PIN_LEFT    3u
#define PIN_B       4u
#define PIN_A       5u
#define PIN_START   23u
#define PIN_SELECT  24u

#define PIN_LED     8u

// All face/dpad buttons used by the controller, so we can init them in a loop.
static const uint8_t k_button_pins[] = {
  PIN_DOWN, PIN_RIGHT, PIN_UP, PIN_LEFT,
  PIN_B, PIN_A, PIN_START, PIN_SELECT,
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
// HID gamepad
//--------------------------------------------------------------------+

static uint8_t compute_hat(bool up, bool down, bool left, bool right)
{
  // Opposite directions cancel out so we never report an impossible diagonal.
  if (up && down)    { up = down = false; }
  if (left && right) { left = right = false; }

  if (up && right)   return GAMEPAD_HAT_UP_RIGHT;
  if (up && left)    return GAMEPAD_HAT_UP_LEFT;
  if (down && right) return GAMEPAD_HAT_DOWN_RIGHT;
  if (down && left)  return GAMEPAD_HAT_DOWN_LEFT;
  if (up)            return GAMEPAD_HAT_UP;
  if (down)          return GAMEPAD_HAT_DOWN;
  if (left)          return GAMEPAD_HAT_LEFT;
  if (right)         return GAMEPAD_HAT_RIGHT;
  return GAMEPAD_HAT_CENTERED;
}

static void build_report(hid_gamepad_report_t *report)
{
  report->x = report->y = report->z = 0;
  report->rz = report->rx = report->ry = 0;

  report->hat = compute_hat(
      button_pressed(PIN_UP),
      button_pressed(PIN_DOWN),
      button_pressed(PIN_LEFT),
      button_pressed(PIN_RIGHT));

  uint32_t buttons = 0;
  if (button_pressed(PIN_A))      buttons |= GAMEPAD_BUTTON_A; // GAMEPAD_BUTTON_A;
  if (button_pressed(PIN_B))      buttons |= GAMEPAD_BUTTON_B; // GAMEPAD_BUTTON_B;
  if (button_pressed(PIN_START))  buttons |= GAMEPAD_BUTTON_START;
  if (button_pressed(PIN_SELECT)) buttons |= GAMEPAD_BUTTON_SELECT;
  report->buttons = buttons;
}

// Poll buttons and push a report to the host whenever the state changes.
static void hid_task(void)
{
  const uint32_t poll_interval_ms = 5;
  static uint32_t last_poll_ms = 0;

  if ( now_ms() - last_poll_ms < poll_interval_ms ) return;
  last_poll_ms = now_ms();

  hid_gamepad_report_t report;
  build_report(&report);

  // Wake the host if we are suspended and the user presses something.
  if ( tud_suspended() && (report.hat != GAMEPAD_HAT_CENTERED || report.buttons) )
  {
    tud_remote_wakeup();
    return;
  }

  if ( !tud_hid_ready() ) return;

  // Only transmit on change to keep the bus quiet and latency low.
  static hid_gamepad_report_t last_report = { 0 };
  static bool have_last = false;

  if ( !have_last || memcmp(&report, &last_report, sizeof(report)) != 0 )
  {
    if ( tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report)) )
    {
      last_report = report;
      have_last = true;
    }
  }
}

// Required by TinyUSB; nothing to do for an input-only gamepad.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
  (void) instance; (void) report_id; (void) report_type;
  (void) buffer; (void) reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
  (void) instance; (void) report_id; (void) report_type;
  (void) buffer; (void) bufsize;
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
      if ( button_pressed(k_button_pins[i]) ) { any = true; break; }
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
    tud_task();   // service the USB device stack
    hid_task();   // read buttons and send reports
    led_task();   // status / activity LED
  }
}
