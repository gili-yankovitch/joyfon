// PICOBOOT over WebUSB — what `picotool load` does, in the browser.
//
// When an RP2040 boots with BOOTSEL held it enumerates as a USB device
// (VID 0x2E8A / PID 0x0003) exposing a vendor interface with a bulk IN and a
// bulk OUT endpoint. The protocol is a 32-byte command packet on the OUT
// endpoint, an optional data stage, and a one-byte ACK stage. See the
// RP2040 datasheet, "2.8.5. PICOBOOT Interface".

export const BOOT_VID        = 0x2e8a;
export const BOOT_PID_RP2040 = 0x0003;
export const BOOT_FILTERS    = [{ vendorId: BOOT_VID, productId: BOOT_PID_RP2040 }];

const MAGIC = 0x431fd10b;

// Command IDs. Bit 7 set means the data stage flows device -> host (a read).
const CMD = {
  EXCLUSIVE_ACCESS: 0x01,
  REBOOT:           0x02,
  FLASH_ERASE:      0x03,
  READ:             0x84,
  WRITE:            0x05,
  EXIT_XIP:         0x06,
  ENTER_CMD_XIP:    0x07,
};

// Vendor control requests on the interface.
const REQ_RESET      = 0x41;
const REQ_CMD_STATUS = 0x42;

export const EXCLUSIVE = { NOT: 0, CLAIM: 1, CLAIM_AND_EJECT: 2 };

// picoboot_status result codes (subset we may hit).
const STATUS_TEXT = {
  0: "OK",
  1: "unknown command",
  2: "invalid command length",
  3: "unaligned address",
  4: "bad erase length",
  5: "address out of bounds",
  6: "not permitted (need EXCLUSIVE_ACCESS first)",
  7: "invalid state",
  8: "buffer too small",
  9: "precondition not met",
  10: "modified data during operation",
  11: "invalid data",
  12: "not allowed",
  13: "unknown error",
  14: "failed assertion",
};

export const SRAM_END_RP2040 = 0x20042000;
export const SECTOR = 4096;
export const PAGE   = 256;

const CMD_TIMEOUT_MS  = 3000;
const DATA_TIMEOUT_MS = 10000;

const hex = (n) => "0x" + (n >>> 0).toString(16).padStart(8, "0");

// Build the 32-byte PICOBOOT command packet.
function encodeCommand({ token, id, args = new Uint8Array(0), transferLength = 0 }) {
  if (args.length > 16) throw new Error("PICOBOOT args exceed 16 bytes");
  const pkt = new Uint8Array(32);
  const dv = new DataView(pkt.buffer);
  dv.setUint32(0, MAGIC, true);
  dv.setUint32(4, token >>> 0, true);
  pkt[8] = id;
  pkt[9] = args.length;
  // bytes 10-11 reserved
  dv.setUint32(12, transferLength >>> 0, true);
  pkt.set(args, 16);
  return pkt;
}

// Pack a list of little-endian uint32 args.
function u32s(...vals) {
  const out = new Uint8Array(vals.length * 4);
  const dv = new DataView(out.buffer);
  vals.forEach((v, i) => dv.setUint32(i * 4, v >>> 0, true));
  return out;
}

// Locate the PICOBOOT vendor interface and its bulk endpoints.
function findInterface(device) {
  for (const cfg of device.configurations ?? []) {
    for (const itf of cfg.interfaces ?? []) {
      for (const alt of itf.alternates ?? []) {
        if (alt.interfaceClass !== 0xff) continue;
        const bulk = (alt.endpoints ?? []).filter((e) => e.type === "bulk");
        const out = bulk.find((e) => e.direction === "out");
        const inp = bulk.find((e) => e.direction === "in");
        if (!out || !inp) continue;
        return {
          configurationValue: cfg.configurationValue,
          interfaceNumber: itf.interfaceNumber,
          alternateSetting: alt.alternateSetting,
          outEp: out.endpointNumber,
          inEp: inp.endpointNumber,
        };
      }
    }
  }
  return null;
}

export class PicobootError extends Error {
  constructor(message, { status } = {}) {
    super(message);
    this.name = "PicobootError";
    this.status = status;
  }
}

function withTimeout(promise, ms, label) {
  let timer;
  const race = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new PicobootError(`${label}: no reply in ${ms} ms`)), ms);
  });
  return Promise.race([promise, race]).finally(() => clearTimeout(timer));
}

export class Picoboot {
  constructor(device) {
    this.device = device;
    this.iface = null;
    this.token = 1;
    this.log = () => {};
  }

  async open() {
    const d = this.device;
    if (!d.opened) await d.open();
    const iface = findInterface(d);
    if (!iface) throw new PicobootError("no PICOBOOT interface - is the device in BOOTSEL mode?");
    if (!d.configuration || d.configuration.configurationValue !== iface.configurationValue) {
      await d.selectConfiguration(iface.configurationValue);
    }
    await d.claimInterface(iface.interfaceNumber);
    this.iface = iface;
    await this.reset();
    return iface;
  }

  async close() {
    if (this.iface) {
      try { await this.device.releaseInterface(this.iface.interfaceNumber); } catch {}
      this.iface = null;
    }
    try { if (this.device.opened) await this.device.close(); } catch {}
  }

  // Reset the PICOBOOT interface to a known state (clears any wedged transfer).
  async reset() {
    const { interfaceNumber, inEp, outEp } = this.iface;
    for (const [dir, ep] of [["in", inEp], ["out", outEp]]) {
      try { await this.device.clearHalt(dir, ep); } catch {}
    }
    const r = await this.device.controlTransferOut({
      requestType: "vendor", recipient: "interface",
      request: REQ_RESET, value: 0, index: interfaceNumber,
    });
    if (r.status !== "ok") throw new PicobootError(`interface reset: ${r.status}`);
  }

  async status() {
    const r = await this.device.controlTransferIn({
      requestType: "vendor", recipient: "interface",
      request: REQ_CMD_STATUS, value: 0, index: this.iface.interfaceNumber,
    }, 16);
    if (r.status !== "ok" || r.data.byteLength < 16) throw new PicobootError(`command status: ${r.status}`);
    return {
      token: r.data.getUint32(0, true),
      status: r.data.getUint32(4, true),
      cmd: r.data.getUint8(8),
      inProgress: !!r.data.getUint8(9),
    };
  }

  exclusiveAccess(mode = EXCLUSIVE.CLAIM) {
    this.log(`EXCLUSIVE_ACCESS ${mode}`);
    return this._cmd(CMD.EXCLUSIVE_ACCESS, new Uint8Array([mode]));
  }

  exitXip()      { this.log("EXIT_XIP");      return this._cmd(CMD.EXIT_XIP); }
  enterCmdXip()  { this.log("ENTER_CMD_XIP"); return this._cmd(CMD.ENTER_CMD_XIP); }

  flashErase(addr, len) {
    if (addr % SECTOR || len % SECTOR) throw new PicobootError(`erase ${hex(addr)}+${len} is not sector-aligned`);
    this.log(`FLASH_ERASE ${hex(addr)}+${hex(len)}`);
    return this._cmd(CMD.FLASH_ERASE, u32s(addr, len));
  }

  write(addr, data) {
    if (addr % PAGE || data.length % PAGE) throw new PicobootError(`write ${hex(addr)}+${data.length} is not page-aligned`);
    this.log(`WRITE ${hex(addr)}+${hex(data.length)}`);
    return this._cmd(CMD.WRITE, u32s(addr, data.length), { data });
  }

  read(addr, len) {
    this.log(`READ ${hex(addr)}+${hex(len)}`);
    return this._cmd(CMD.READ, u32s(addr, len), { length: len });
  }

  reboot(pc = 0, sp = SRAM_END_RP2040, delayMs = 500) {
    this.log(`REBOOT ${hex(pc)} ${hex(sp)} ${delayMs} ms`);
    return this._cmd(CMD.REBOOT, u32s(pc, sp, delayMs));
  }

  // Run one command: 32-byte packet, optional data stage, one-byte ACK.
  async _cmd(id, args = new Uint8Array(0), { data = null, length = 0 } = {}) {
    const { inEp, outEp } = this.iface;
    const isRead = !!(id & 0x80);
    const xferLen = isRead ? length : (data ? data.length : 0);
    const name = Object.keys(CMD).find((k) => CMD[k] === id) ?? hex(id);

    const pkt = encodeCommand({ token: this.token++, id, args, transferLength: xferLen });
    const sent = await withTimeout(this.device.transferOut(outEp, pkt), CMD_TIMEOUT_MS, name);
    if (sent.status !== "ok" || sent.bytesWritten !== 32) await this._fail(name, sent.status, "command");

    let result = null;
    if (xferLen) {
      if (isRead) {
        const got = await withTimeout(this.device.transferIn(inEp, xferLen), DATA_TIMEOUT_MS, `${name} data`);
        if (got.status !== "ok") await this._fail(name, got.status, "data");
        if (got.data.byteLength !== xferLen) throw new PicobootError(`${name}: got ${got.data.byteLength} of ${xferLen} bytes`);
        result = new Uint8Array(got.data.buffer, got.data.byteOffset, got.data.byteLength);
      } else {
        const put = await withTimeout(this.device.transferOut(outEp, data), DATA_TIMEOUT_MS, `${name} data`);
        if (put.status !== "ok") await this._fail(name, put.status, "data");
        if (put.bytesWritten !== xferLen) throw new PicobootError(`${name}: sent ${put.bytesWritten} of ${xferLen} bytes`);
      }
    }

    // ACK stage: zero-length transfer in the opposite direction of the data.
    const ackTimeout = xferLen ? CMD_TIMEOUT_MS : DATA_TIMEOUT_MS;
    const ack = isRead
      ? await withTimeout(this.device.transferOut(outEp, new Uint8Array(0)), ackTimeout, `${name} ack`)
      : await withTimeout(this.device.transferIn(inEp, 1), ackTimeout, `${name} ack`);
    if (ack.status !== "ok") await this._fail(name, ack.status, "ack");

    return result;
  }

  async _fail(name, usbStatus, stage) {
    let message = `${usbStatus} during ${stage}`;
    let code;
    try {
      const s = await this.status();
      code = s.status;
      message = `${STATUS_TEXT[s.status] ?? `status ${s.status}`} (${usbStatus} during ${stage})`;
    } catch {}
    try { await this.reset(); } catch {}
    throw new PicobootError(`${name}: ${message}`, { status: code });
  }
}

// Erase + program + verify a planned sector list, then reboot into it.
export async function flashSectors(pb, sectors, { onProgress = () => {}, verify = true } = {}) {
  const n = sectors.length;

  await pb.exclusiveAccess(EXCLUSIVE.CLAIM);
  await pb.exitXip();

  for (let i = 0; i < n; i++) {
    const { addr, data } = sectors[i];
    onProgress({ phase: "erase", done: i, total: n, addr });
    await pb.flashErase(addr, SECTOR);
    onProgress({ phase: "write", done: i, total: n, addr });
    await pb.write(addr, data);
  }
  onProgress({ phase: "write", done: n, total: n });

  if (verify) {
    for (let i = 0; i < n; i++) {
      const { addr, data } = sectors[i];
      onProgress({ phase: "verify", done: i, total: n, addr });
      const back = await pb.read(addr, data.length);
      for (let j = 0; j < data.length; j++) {
        if (back[j] !== data[j]) {
          throw new PicobootError(`verify: ${hex(addr + j)} reads ${hex(back[j]).slice(-2)}, wrote ${hex(data[j]).slice(-2)}`);
        }
      }
    }
    onProgress({ phase: "verify", done: n, total: n });
  }

  onProgress({ phase: "reboot", done: n, total: n });
  await pb.reboot(0, SRAM_END_RP2040, 500);
}

export { hex };
