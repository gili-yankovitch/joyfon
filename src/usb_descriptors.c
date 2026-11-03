#include <string.h>

#include "tusb.h"
#include "usb_descriptors.h"
#include "ds4.h"

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
// Mirrors a wired DualShock 4 (CUH-ZCT2U). iOS binds controllers by
// VID/PID, so these values are what make an iPhone attach a real
// GameController profile instead of ignoring the device.
tusb_desc_device_t const desc_device =
{
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = DS4_VID,
    .idProduct          = DS4_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = 0x00, // the real controller exposes no serial string

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+
// Byte-for-byte the DualShock 4 report descriptor. Only input report 0x01
// is actually produced by this firmware; the remaining feature/output
// reports are declared so host drivers that probe them find what they
// expect (see tud_hid_get_report_cb below).
uint8_t const desc_hid_report[] =
{
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x05,        // Usage (Game Pad)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x09, 0x30,        //   Usage (X)
  0x09, 0x31,        //   Usage (Y)
  0x09, 0x32,        //   Usage (Z)
  0x09, 0x35,        //   Usage (Rz)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x04,        //   Report Count (4)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x09, 0x39,        //   Usage (Hat switch)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x07,        //   Logical Maximum (7)
  0x35, 0x00,        //   Physical Minimum (0)
  0x46, 0x3B, 0x01,  //   Physical Maximum (315)
  0x65, 0x14,        //   Unit (Eng Rot: Degrees)
  0x75, 0x04,        //   Report Size (4)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x42,        //   Input (Data,Var,Abs,Null State)
  0x65, 0x00,        //   Unit (None)
  0x05, 0x09,        //   Usage Page (Button)
  0x19, 0x01,        //   Usage Minimum (1)
  0x29, 0x0E,        //   Usage Maximum (14)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x0E,        //   Report Count (14)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
  0x09, 0x20,        //   Usage (0x20)  - report counter
  0x75, 0x06,        //   Report Size (6)
  0x95, 0x01,        //   Report Count (1)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x7F,        //   Logical Maximum (127)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x05, 0x01,        //   Usage Page (Generic Desktop)
  0x09, 0x33,        //   Usage (Rx)  - L2 analog
  0x09, 0x34,        //   Usage (Ry)  - R2 analog
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x00,  //   Logical Maximum (255)
  0x75, 0x08,        //   Report Size (8)
  0x95, 0x02,        //   Report Count (2)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
  0x09, 0x21,        //   Usage (0x21)  - timestamp, IMU, touchpad
  0x95, 0x36,        //   Report Count (54)
  0x81, 0x02,        //   Input (Data,Var,Abs)
  0x85, 0x05,        //   Report ID (5)   - rumble / lightbar
  0x09, 0x22,        //   Usage (0x22)
  0x95, 0x1F,        //   Report Count (31)
  0x91, 0x02,        //   Output (Data,Var,Abs)
  0x85, 0x04,        //   Report ID (4)
  0x09, 0x23,        //   Usage (0x23)
  0x95, 0x24,        //   Report Count (36)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x02,        //   Report ID (2)   - IMU calibration
  0x09, 0x24,        //   Usage (0x24)
  0x95, 0x24,        //   Report Count (36)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x08,        //   Report ID (8)
  0x09, 0x25,        //   Usage (0x25)
  0x95, 0x03,        //   Report Count (3)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x10,        //   Report ID (16)
  0x09, 0x26,        //   Usage (0x26)
  0x95, 0x04,        //   Report Count (4)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x11,        //   Report ID (17)
  0x09, 0x27,        //   Usage (0x27)
  0x95, 0x02,        //   Report Count (2)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x12,        //   Report ID (18)  - device / host MAC
  0x06, 0x02, 0xFF,  //   Usage Page (Vendor Defined 0xFF02)
  0x09, 0x21,        //   Usage (0x21)
  0x95, 0x0F,        //   Report Count (15)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x13,        //   Report ID (19)
  0x09, 0x22,        //   Usage (0x22)
  0x95, 0x16,        //   Report Count (22)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x14,        //   Report ID (20)
  0x06, 0x05, 0xFF,  //   Usage Page (Vendor Defined 0xFF05)
  0x09, 0x20,        //   Usage (0x20)
  0x95, 0x10,        //   Report Count (16)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x15,        //   Report ID (21)
  0x09, 0x21,        //   Usage (0x21)
  0x95, 0x2C,        //   Report Count (44)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x06, 0x80, 0xFF,  //   Usage Page (Vendor Defined 0xFF80)
  0x85, 0x80,        //   Report ID (128)
  0x09, 0x20,        //   Usage (0x20)
  0x95, 0x06,        //   Report Count (6)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x81,        //   Report ID (129)
  0x09, 0x21,        //   Usage (0x21)
  0x95, 0x06,        //   Report Count (6)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x82,        //   Report ID (130)
  0x09, 0x22,        //   Usage (0x22)
  0x95, 0x05,        //   Report Count (5)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x83,        //   Report ID (131)
  0x09, 0x23,        //   Usage (0x23)
  0x95, 0x01,        //   Report Count (1)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x84,        //   Report ID (132)
  0x09, 0x24,        //   Usage (0x24)
  0x95, 0x04,        //   Report Count (4)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x85,        //   Report ID (133)
  0x09, 0x25,        //   Usage (0x25)
  0x95, 0x06,        //   Report Count (6)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x86,        //   Report ID (134)
  0x09, 0x26,        //   Usage (0x26)
  0x95, 0x06,        //   Report Count (6)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x87,        //   Report ID (135)
  0x09, 0x27,        //   Usage (0x27)
  0x95, 0x23,        //   Report Count (35)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x88,        //   Report ID (136)
  0x09, 0x28,        //   Usage (0x28)
  0x95, 0x22,        //   Report Count (34)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x89,        //   Report ID (137)
  0x09, 0x29,        //   Usage (0x29)
  0x95, 0x02,        //   Report Count (2)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x90,        //   Report ID (144)
  0x09, 0x30,        //   Usage (0x30)
  0x95, 0x05,        //   Report Count (5)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x91,        //   Report ID (145)
  0x09, 0x31,        //   Usage (0x31)
  0x95, 0x03,        //   Report Count (3)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x92,        //   Report ID (146)
  0x09, 0x32,        //   Usage (0x32)
  0x95, 0x03,        //   Report Count (3)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0x93,        //   Report ID (147)
  0x09, 0x33,        //   Usage (0x33)
  0x95, 0x0C,        //   Report Count (12)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA0,        //   Report ID (160)
  0x09, 0x40,        //   Usage (0x40)
  0x95, 0x06,        //   Report Count (6)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA1,        //   Report ID (161)
  0x09, 0x41,        //   Usage (0x41)
  0x95, 0x01,        //   Report Count (1)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA2,        //   Report ID (162)
  0x09, 0x42,        //   Usage (0x42)
  0x95, 0x01,        //   Report Count (1)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA3,        //   Report ID (163)  - firmware / hardware version
  0x09, 0x43,        //   Usage (0x43)
  0x95, 0x30,        //   Report Count (48)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA4,        //   Report ID (164)
  0x09, 0x44,        //   Usage (0x44)
  0x95, 0x0D,        //   Report Count (13)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA5,        //   Report ID (165)
  0x09, 0x45,        //   Usage (0x45)
  0x95, 0x15,        //   Report Count (21)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA6,        //   Report ID (166)
  0x09, 0x46,        //   Usage (0x46)
  0x95, 0x15,        //   Report Count (21)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xF0,        //   Report ID (240)
  0x09, 0x47,        //   Usage (0x47)
  0x95, 0x3F,        //   Report Count (63)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xF1,        //   Report ID (241)
  0x09, 0x48,        //   Usage (0x48)
  0x95, 0x3F,        //   Report Count (63)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xF2,        //   Report ID (242)
  0x09, 0x49,        //   Usage (0x49)
  0x95, 0x0F,        //   Report Count (15)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA7,        //   Report ID (167)
  0x09, 0x4A,        //   Usage (0x4A)
  0x95, 0x01,        //   Report Count (1)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA8,        //   Report ID (168)
  0x09, 0x4B,        //   Usage (0x4B)
  0x95, 0x01,        //   Report Count (1)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xA9,        //   Report ID (169)
  0x09, 0x4C,        //   Usage (0x4C)
  0x95, 0x08,        //   Report Count (8)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xAA,        //   Report ID (170)
  0x09, 0x4E,        //   Usage (0x4E)
  0x95, 0x01,        //   Report Count (1)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xAB,        //   Report ID (171)
  0x09, 0x4F,        //   Usage (0x4F)
  0x95, 0x39,        //   Report Count (57)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xAC,        //   Report ID (172)
  0x09, 0x50,        //   Usage (0x50)
  0x95, 0x39,        //   Report Count (57)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xAD,        //   Report ID (173)
  0x09, 0x51,        //   Usage (0x51)
  0x95, 0x0B,        //   Report Count (11)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xAE,        //   Report ID (174)
  0x09, 0x52,        //   Usage (0x52)
  0x95, 0x01,        //   Report Count (1)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xAF,        //   Report ID (175)
  0x09, 0x53,        //   Usage (0x53)
  0x95, 0x02,        //   Report Count (2)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0x85, 0xB0,        //   Report ID (176)
  0x09, 0x54,        //   Usage (0x54)
  0x95, 0x3F,        //   Report Count (63)
  0xB1, 0x02,        //   Feature (Data,Var,Abs)
  0xC0               // End Collection
};

// Invoked when received GET HID REPORT DESCRIPTOR
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
  (void) instance;
  return desc_hid_report;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum
{
  ITF_NUM_HID,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

// Endpoint addresses match the real controller's HID interface.
#define EPNUM_HID_OUT   0x03
#define EPNUM_HID_IN    0x84

uint8_t const desc_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

  // Interface number, string index, protocol, report descriptor len, EP Out, EP In, size, polling interval (ms)
  TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                           EPNUM_HID_OUT, EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 5)
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index;
  return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

char const *string_desc_arr[] =
{
  (const char[]) { 0x09, 0x04 },      // 0: supported language is English (0x0409)
  "Sony Interactive Entertainment",   // 1: Manufacturer
  "JoyFon",                           // 2: Product
};

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;
  size_t chr_count;

  if ( index == STRID_LANGID )
  {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  }
  else
  {
    if ( !(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) ) return NULL;

    const char *str = string_desc_arr[index];

    chr_count = strlen(str);
    size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
    if ( chr_count > max_count ) chr_count = max_count;

    for ( size_t i = 0; i < chr_count; i++ )
    {
      _desc_str[1 + i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}

//--------------------------------------------------------------------+
// Feature reports
//--------------------------------------------------------------------+
// Host drivers probe a handful of feature reports while attaching. We have
// no IMU and no bluetooth radio, so we answer with well-formed placeholder
// data. Stalling here is not an option: TinyUSB asserts on a zero-length
// reply, and a driver that gets a STALL mid-probe may drop the device.

// Report 0x02: IMU calibration. Biases are zero and the plus/minus ranges are
// symmetric and non-zero, so a driver computing a scale factor from
// (plus - minus) never divides by zero.
static uint16_t ds4_calibration(uint8_t *buffer, uint16_t reqlen)
{
  // 17 int16 values, then two trailing bytes the controller leaves at zero.
  static const int16_t calib[17] = {
    0, 0, 0,                      // gyro pitch/yaw/roll bias
    16384, -16384,                // gyro pitch plus/minus
    16384, -16384,                // gyro yaw   plus/minus
    16384, -16384,                // gyro roll  plus/minus
    512,   -512,                  // gyro speed plus/minus
    8192,  -8192,                 // accel X plus/minus
    8192,  -8192,                 // accel Y plus/minus
    8192,  -8192,                 // accel Z plus/minus
  };
  TU_VERIFY_STATIC(sizeof(calib) == 34, "DS4 calibration block is 34 bytes + 2 pad");

  uint8_t body[36] = { 0 };
  memcpy(body, calib, sizeof(calib));

  uint16_t len = tu_min16(reqlen, sizeof(body));
  memcpy(buffer, body, len);
  return len;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
  (void) instance;

  if ( report_type != HID_REPORT_TYPE_FEATURE || reqlen == 0 ) return 0;

  if ( report_id == DS4_FEATURE_CALIB ) return ds4_calibration(buffer, reqlen);

  // Everything else: a zero-filled reply of the requested length. The stack
  // has already placed the report ID byte ahead of `buffer`.
  memset(buffer, 0, reqlen);
  return reqlen;
}

// Rumble / lightbar / bluetooth pairing writes. Nothing to drive here.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
  (void) instance; (void) report_id; (void) report_type;
  (void) buffer; (void) bufsize;
}
