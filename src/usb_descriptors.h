#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

// String descriptor indices. The real DualShock 4 exposes no serial string,
// so we don't either - a serial the host has never seen from a controller
// with this VID/PID is a needless difference.
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
};

#endif /* USB_DESCRIPTORS_H_ */
