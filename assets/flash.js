// Flash-page controller: wire the UI to the PICOBOOT WebUSB driver.
import { parseUf2, planSectors, describe } from "./uf2.js";
import { Picoboot, flashSectors, BOOT_FILTERS } from "./picoboot.js";

const FIRMWARE_URL = "firmware/joyfon.uf2";

const $ = (id) => document.getElementById(id);
const usbState   = $("usbState");
const flashBtn   = $("flashBtn");
const downloadBtn = $("downloadBtn");
const fileInput  = $("file");
const statusEl   = $("flashStatus");
const progress   = $("progress");
const logEl      = $("flashLog");

const supported = "usb" in navigator;

// Firmware currently selected: either the bundled build or a user file.
let firmware = null; // { name, bytes: Uint8Array }

function log(msg, kind = "") {
  const li = document.createElement("li");
  if (kind) li.className = kind;
  li.textContent = msg;
  logEl.prepend(li);
}

function setStatus(text) { statusEl.textContent = text; }

function fmtBytes(n) {
  return n < 1024 ? `${n} B` : `${(n / 1024).toFixed(1)} KB`;
}

async function selectBundled() {
  try {
    const res = await fetch(FIRMWARE_URL, { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const bytes = new Uint8Array(await res.arrayBuffer());
    firmware = { name: "joyfon.uf2", bytes };
    downloadBtn.href = FIRMWARE_URL;
    downloadBtn.download = "joyfon.uf2";
    const parsed = parseUf2(bytes);
    setStatus(`joyfon.uf2 · ${fmtBytes(bytes.length)} · ${describe(parsed)}`);
    if (supported) flashBtn.disabled = false;
  } catch (err) {
    setStatus("could not load the bundled firmware");
    log(`firmware fetch failed: ${err.message}`, "err");
  }
}

fileInput.addEventListener("change", () => {
  const f = fileInput.files?.[0];
  if (!f) return;
  const reader = new FileReader();
  reader.onload = () => {
    const bytes = new Uint8Array(reader.result);
    try {
      const parsed = parseUf2(bytes); // validate before we offer to flash it
      firmware = { name: f.name, bytes };
      downloadBtn.href = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
      downloadBtn.download = f.name;
      setStatus(`${f.name} · ${fmtBytes(bytes.length)} · ${describe(parsed)}`);
      if (supported) flashBtn.disabled = false;
      log(`loaded ${f.name}`);
    } catch (err) {
      firmware = null;
      flashBtn.disabled = true;
      setStatus(`not a valid UF2: ${err.message}`);
      log(`rejected ${f.name}: ${err.message}`, "err");
    }
  };
  reader.readAsArrayBuffer(f);
});

const PHASE_LABEL = { erase: "erasing", write: "writing", verify: "verifying", reboot: "rebooting" };

async function flash() {
  if (!firmware) return;

  let device, pb;
  flashBtn.disabled = true;
  progress.hidden = false;
  progress.value = 0;

  try {
    const parsed = parseUf2(firmware.bytes);
    const sectors = planSectors(parsed);

    setStatus("choose “RP2 Boot” in the browser prompt…");
    device = await navigator.usb.requestDevice({ filters: BOOT_FILTERS });

    pb = new Picoboot(device);
    pb.log = (m) => log(m);
    await pb.open();
    log(`connected to ${device.productName || "RP2 Boot"}`);

    const sectorCount = sectors.length;
    await flashSectors(pb, sectors, {
      onProgress: ({ phase, done, total }) => {
        // erase+write share the first half of the bar, verify the second.
        const label = PHASE_LABEL[phase] ?? phase;
        if (phase === "verify") progress.value = 0.5 + 0.5 * (done / total);
        else if (phase === "reboot") progress.value = 1;
        else progress.value = 0.5 * (done / total);
        setStatus(`${label} ${done}/${total} sectors…`);
      },
    });

    progress.value = 1;
    setStatus(`done — ${sectorCount} sectors flashed, badge rebooting`);
    log("flash complete, badge rebooting into the new firmware", "ok");
  } catch (err) {
    setStatus(`failed: ${err.message}`);
    log(err.message, "err");
  } finally {
    if (pb) { try { await pb.close(); } catch {} }
    flashBtn.disabled = !firmware;
    setTimeout(() => { progress.hidden = true; }, 2000);
  }
}

flashBtn.addEventListener("click", flash);

// Startup: report WebUSB support, then load the bundled firmware.
if (!supported) {
  if (usbState) {
    usbState.textContent = "WebUSB unavailable — use the download below";
    usbState.classList.add("pad-off");
  }
  flashBtn.disabled = true;
  flashBtn.title = "This browser has no WebUSB. Use a Chromium browser, or download the .uf2.";
} else if (usbState) {
  usbState.textContent = "WebUSB ready";
  usbState.classList.add("pad-ok");
}

selectBundled();

//--------------------------------------------------------------------+
// Functionality test — read the flashed controller over WebHID.
//--------------------------------------------------------------------+
// After flashing, the JoyFon enumerates as a wired DualShock 4. We open it
// over WebHID and parse its input report (ID 0x01) using the exact byte layout
// the firmware emits (see src/ds4.h), lighting up each button as it is pressed.

const DS4_VID = 0x054c;
const DS4_PID = 0x09cc;

const connectBtn = $("connectBtn");
const testStatus = $("testStatus");
const pad = $("pad");
const hidSupported = "hid" in navigator;

let hidDevice = null;

function setTestStatus(text) { if (testStatus) testStatus.textContent = text; }

function setPressed(id, on) {
  const el = document.getElementById(id);
  if (el) el.classList.toggle("on", !!on);
}

function clearPad() {
  for (const id of ["btn-up","btn-down","btn-left","btn-right","btn-a","btn-b",
                    "btn-x","btn-y","btn-sl","btn-sr","btn-start","btn-select"]) {
    setPressed(id, false);
  }
}

// D-pad hat (0..7 clockwise from N, 8 = released) -> four directions.
function applyHat(h) {
  setPressed("btn-up",    h === 0 || h === 1 || h === 7);
  setPressed("btn-right", h === 1 || h === 2 || h === 3);
  setPressed("btn-down",  h === 3 || h === 4 || h === 5);
  setPressed("btn-left",  h === 5 || h === 6 || h === 7);
}

// Input report payload (report ID already stripped by WebHID):
//   offset 4 = buttons0 (hat in low nibble, face buttons in high nibble)
//   offset 5 = buttons1 (shoulders, share/options, stick clicks)
// Face/shoulder bit -> JoyFon label follows the firmware's Nintendo mapping.
function onReport(e) {
  if (e.reportId !== 0x01 || e.data.byteLength < 6) return;
  const b0 = e.data.getUint8(4);
  const b1 = e.data.getUint8(5);

  applyHat(b0 & 0x0f);
  setPressed("btn-a", b0 & 0x40); // A  -> Circle
  setPressed("btn-b", b0 & 0x20); // B  -> Cross
  setPressed("btn-x", b0 & 0x80); // X  -> Triangle
  setPressed("btn-y", b0 & 0x10); // Y  -> Square

  setPressed("btn-sl",     b1 & 0x01); // SL     -> L1
  setPressed("btn-sr",     b1 & 0x02); // SR     -> R1
  setPressed("btn-select", b1 & 0x10); // SELECT -> Share
  setPressed("btn-start",  b1 & 0x20); // START  -> Options
}

async function openController(dev) {
  try {
    if (!dev.opened) await dev.open();
  } catch (err) {
    setTestStatus(`could not open controller: ${err.message}`);
    return;
  }
  hidDevice = dev;
  dev.addEventListener("inputreport", onReport);
  pad?.classList.add("live");
  setTestStatus(`connected: ${dev.productName || "controller"} — press buttons`);
}

async function connectController() {
  if (!hidSupported) return;
  try {
    const devices = await navigator.hid.requestDevice({
      filters: [{ vendorId: DS4_VID, productId: DS4_PID }],
    });
    if (!devices.length) { setTestStatus("no controller selected"); return; }
    await openController(devices[0]);
  } catch (err) {
    setTestStatus(`connect failed: ${err.message}`);
  }
}

if (!hidSupported) {
  if (connectBtn) { connectBtn.disabled = true; connectBtn.title = "This browser has no WebHID."; }
  setTestStatus("WebHID unavailable in this browser");
} else {
  connectBtn?.addEventListener("click", connectController);

  // Re-attach to a controller we were already granted (e.g. after flashing,
  // when the board reboots and re-enumerates).
  navigator.hid.getDevices().then((list) => {
    const dev = list.find((d) => d.vendorId === DS4_VID && d.productId === DS4_PID);
    if (dev) openController(dev);
  });

  navigator.hid.addEventListener("connect", (e) => {
    const d = e.device;
    if (!hidDevice && d.vendorId === DS4_VID && d.productId === DS4_PID) openController(d);
  });

  navigator.hid.addEventListener("disconnect", (e) => {
    if (e.device === hidDevice) {
      hidDevice = null;
      pad?.classList.remove("live");
      clearPad();
      setTestStatus("controller unplugged — plug it back in to test again");
    }
  });
}
