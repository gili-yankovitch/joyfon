#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

// We expose a single HID gamepad interface, so a report ID is not required.
// Using 0 means "no report ID prefix" when calling tud_hid_report().
enum {
  REPORT_ID_GAMEPAD = 0,
};

#endif /* USB_DESCRIPTORS_H_ */
