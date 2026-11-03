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
// Functionality test — read the flashed controller over the Gamepad API.
//--------------------------------------------------------------------+
// Uses the Gamepad API, which works on desktop AND mobile (Android Chrome,
// iOS Safari) — unlike WebHID, which no mobile browser supports. The flashed
// JoyFon enumerates as a DualShock 4, which every platform maps to the
// "standard" gamepad layout; we translate those standard indices back to the
// physical JoyFon buttons, following the firmware's mapping.

const connectBtn = $("connectBtn");
const testStatus = $("testStatus");
const pad = $("pad");
const gpSupported = "getGamepads" in navigator;

let padIndex = null;
let rafId = 0;

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

const isDown = (buttons, i) => !!(buttons[i] && buttons[i].pressed);

// Standard-mapping index -> physical JoyFon button. The phone sees the device
// as a standard gamepad; a physical button lights the pill that matches how it
// actually plays (verified on-device), not the raw DS4 role the firmware uses.
//   face:  A->Circle(1) B->Cross(0) X->Triangle(3) Y->Square(2)
//   bumpers: SL=L1(4) SR=R1(5)
//   center : SELECT=view/share(8) START=menu/options(9)
//   d-pad  : up=12 down=13 left=14 right=15
function readGamepad() {
  const gp = (navigator.getGamepads && navigator.getGamepads()[padIndex]) || null;
  if (!gp) { stopTest("controller disconnected — press a button to reconnect"); return; }

  const b = gp.buttons;
  setPressed("btn-b", isDown(b, 0));
  setPressed("btn-a", isDown(b, 1));
  setPressed("btn-y", isDown(b, 2));
  setPressed("btn-x", isDown(b, 3));
  setPressed("btn-sl", isDown(b, 4)); // physical SL = left shoulder (L1)
  setPressed("btn-sr", isDown(b, 5)); // physical SR = right shoulder (R1)
  setPressed("btn-select", isDown(b, 8)); // physical SELECT = view/share
  setPressed("btn-start",  isDown(b, 9)); // physical START  = menu/options
  setPressed("btn-up",    isDown(b, 12));
  setPressed("btn-down",  isDown(b, 13));
  setPressed("btn-left",  isDown(b, 14));
  setPressed("btn-right", isDown(b, 15));

  rafId = requestAnimationFrame(readGamepad);
}

function startTest(gp) {
  padIndex = gp.index;
  pad?.classList.add("live");
  setTestStatus(`connected: ${(gp.id || "controller").slice(0, 36)} — press buttons`);
  cancelAnimationFrame(rafId);
  rafId = requestAnimationFrame(readGamepad);
}

function stopTest(message) {
  cancelAnimationFrame(rafId);
  rafId = 0;
  padIndex = null;
  pad?.classList.remove("live");
  clearPad();
  setTestStatus(message);
}

function firstGamepad() {
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  return Array.from(pads).find(Boolean) || null;
}

if (!gpSupported) {
  if (connectBtn) { connectBtn.disabled = true; connectBtn.title = "This browser has no Gamepad API."; }
  setTestStatus("gamepad testing unavailable in this browser");
} else {
  // The browser only exposes a gamepad after the first input, so pressing a
  // button both connects it and satisfies the user-gesture requirement.
  window.addEventListener("gamepadconnected", (e) => {
    if (padIndex === null) startTest(e.gamepad);
  });
  window.addEventListener("gamepaddisconnected", (e) => {
    if (e.gamepad.index === padIndex) stopTest("controller disconnected — plug it back in to test again");
  });

  connectBtn?.addEventListener("click", () => {
    const gp = firstGamepad();
    if (gp) startTest(gp);
    else setTestStatus("press any button on the controller to connect…");
  });

  const existing = firstGamepad();
  if (existing) startTest(existing);
  else setTestStatus("plug in or pair your JoyFon, then press any button");
}
