#ifndef DS4_H_
#define DS4_H_

/*
 * DualShock 4 (wired) HID definitions.
 *
 * iOS/iPadOS does not bind generic HID gamepads - Apple's GameController
 * framework only attaches to a fixed set of controllers. Presenting the
 * DualShock 4's USB identity and input report is what makes an iPhone see
 * this device as a real controller. Android/PC/Linux also recognise it, so
 * this replaces (rather than supplements) the old generic gamepad persona.
 */

#include <stdint.h>
#include "tusb.h"

// Sony Interactive Entertainment / Wireless Controller (CUH-ZCT2U, "v2").
#define DS4_VID   0x054C
#define DS4_PID   0x09CC

// The controller streams input as report ID 0x01, 64 bytes on the wire
// (1 report-ID byte + 63 payload bytes).
#define DS4_REPORT_ID_INPUT   0x01
#define DS4_REPORT_LEN        64

// Feature reports the host may ask for during probing.
#define DS4_FEATURE_CALIB     0x02  // IMU calibration, 37 bytes incl. ID
#define DS4_FEATURE_MAC       0x12  // device + host bluetooth address
#define DS4_FEATURE_VERSION   0xA3  // firmware/hardware date strings

//--------------------------------------------------------------------+
// Input report 0x01 payload (63 bytes, little-endian)
//--------------------------------------------------------------------+
typedef struct TU_ATTR_PACKED
{
  uint8_t lx, ly;          // left stick,  0x80 = centred
  uint8_t rx, ry;          // right stick, 0x80 = centred

  uint8_t buttons0;        // bits 0-3 D-pad hat, 4 Square, 5 Cross, 6 Circle, 7 Triangle
  uint8_t buttons1;        // 0 L1, 1 R1, 2 L2, 3 R2, 4 Share, 5 Options, 6 L3, 7 R3
  uint8_t buttons2;        // 0 PS, 1 touchpad click, 2-7 report counter

  uint8_t l2, r2;          // analog trigger travel, 0..255

  uint16_t timestamp;      // 5.33 us units, free-running
  uint8_t  battery;

  int16_t gyro[3];         // pitch, yaw, roll
  int16_t accel[3];        // x, y, z

  uint8_t reserved1[5];
  uint8_t status;          // bit 4 = cable connected, bits 0-3 battery level
  uint8_t reserved2[2];

  uint8_t touch_count;
  uint8_t touch[30];
} ds4_input_report_t;

TU_VERIFY_STATIC(sizeof(ds4_input_report_t) == DS4_REPORT_LEN - 1, "DS4 input report must be 63 bytes");

// D-pad hat values. 8 means "no direction held".
enum {
  DS4_HAT_N = 0, DS4_HAT_NE, DS4_HAT_E, DS4_HAT_SE,
  DS4_HAT_S,     DS4_HAT_SW, DS4_HAT_W, DS4_HAT_NW,
  DS4_HAT_NONE = 8,
};

// buttons0 (upper nibble)
#define DS4_BTN_SQUARE    0x10
#define DS4_BTN_CROSS     0x20
#define DS4_BTN_CIRCLE    0x40
#define DS4_BTN_TRIANGLE  0x80

// buttons1
#define DS4_BTN_L1        0x01
#define DS4_BTN_R1        0x02
#define DS4_BTN_L2        0x04
#define DS4_BTN_R2        0x08
#define DS4_BTN_SHARE     0x10
#define DS4_BTN_OPTIONS   0x20
#define DS4_BTN_L3        0x40
#define DS4_BTN_R3        0x80

// buttons2
#define DS4_BTN_PS        0x01
#define DS4_BTN_TOUCHPAD  0x02

#define DS4_STICK_CENTER  0x80

// status: plugged into USB, battery reported as full.
#define DS4_STATUS_CABLE  0x10
#define DS4_STATUS_FULL   (DS4_STATUS_CABLE | 0x0B)

#endif /* DS4_H_ */
