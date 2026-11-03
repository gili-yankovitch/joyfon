// UF2 parsing for the in-browser flasher.
//
// A UF2 file is a flat sequence of 512-byte blocks. Each block carries up to
// 476 bytes of payload plus a small header that says where in flash it goes.
// We validate every block, collect the payloads, then pack them into the
// 4 KB sectors the RP2040 bootloader erases and writes.
//
// Format: https://github.com/microsoft/uf2

export const UF2_MAGIC_START0 = 0x0a324655; // "UF2\n"
export const UF2_MAGIC_START1 = 0x9e5d5157;
export const UF2_MAGIC_END    = 0x0ab16f30;

export const UF2_FLAG_NOT_MAIN_FLASH = 0x00000001;
export const UF2_FLAG_FAMILY_ID      = 0x00002000;

export const RP2040_FAMILY_ID = 0xe48bff56;

export const FLASH_BASE = 0x10000000;
export const FLASH_END  = 0x11000000;
export const PAGE       = 256;
export const SECTOR     = 4096;

export const hex = (n) => "0x" + (n >>> 0).toString(16).padStart(8, "0");

// Parse a UF2 image into a list of { addr, data } payload chunks.
export function parseUf2(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  if (bytes.length === 0 || bytes.length % 512 !== 0) {
    throw new Error(`not a UF2: ${bytes.length} bytes is not a multiple of 512`);
  }

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const total = bytes.length / 512;
  const chunks = [];
  let family = null;

  for (let i = 0; i < total; i++) {
    const base = i * 512;

    const magic0 = view.getUint32(base, true);
    const magic1 = view.getUint32(base + 4, true);
    const magicEnd = view.getUint32(base + 508, true);
    if (magic0 !== UF2_MAGIC_START0 || magic1 !== UF2_MAGIC_START1 || magicEnd !== UF2_MAGIC_END) {
      throw new Error(`not a UF2: bad magic in block ${i}`);
    }

    const flags     = view.getUint32(base + 8, true);
    const addr      = view.getUint32(base + 12, true);
    const payLen    = view.getUint32(base + 16, true);
    const blockNo   = view.getUint32(base + 20, true);
    const numBlocks = view.getUint32(base + 24, true);
    const familyId  = view.getUint32(base + 28, true); // only meaningful with FAMILY_ID flag

    if (numBlocks !== total) throw new Error(`UF2 declares ${numBlocks} blocks but holds ${total} - truncated download?`);
    if (blockNo !== i)       throw new Error(`UF2 block ${i} is numbered ${blockNo}`);

    // Blocks not destined for main flash (e.g. metadata) are skipped.
    if (flags & UF2_FLAG_NOT_MAIN_FLASH) continue;

    const thisFamily = (flags & UF2_FLAG_FAMILY_ID) ? familyId : 0;
    if (family === null) family = thisFamily;
    else if (family !== thisFamily) throw new Error(`UF2 mixes families (${hex(family)} and ${hex(thisFamily)})`);

    if (payLen === 0 || payLen > 476) throw new Error(`UF2 block ${i} carries ${payLen} bytes`);
    if (payLen % PAGE || addr % PAGE) throw new Error(`UF2 block ${i} (${payLen} bytes at ${hex(addr)}) is not page-aligned`);
    if (addr < FLASH_BASE || addr + payLen > FLASH_END) throw new Error(`UF2 block ${i} targets ${hex(addr)}, which is not flash`);

    chunks.push({ addr, data: bytes.slice(base + 32, base + 32 + payLen) });
  }

  if (!chunks.length) throw new Error("UF2 has no flash data");
  if (family !== 0 && family !== RP2040_FAMILY_ID) {
    throw new Error(`this image is for family ${hex(family)}, the badge is an RP2040 (${hex(RP2040_FAMILY_ID)})`);
  }

  const bytesTotal = chunks.reduce((n, c) => n + c.data.length, 0);
  return { chunks, family, bytes: bytesTotal };
}

// Pack payload chunks into aligned 4 KB sectors, padding the gaps with 0xFF
// (erased-flash value). The bootloader erases and writes a whole sector at a
// time, so every byte we don't touch has to read back as 0xFF.
export function planSectors({ chunks }) {
  const sectors = new Map();

  for (const { addr, data } of chunks) {
    let off = 0;
    while (off < data.length) {
      const sectorAddr = (addr + off) & ~(SECTOR - 1);
      let sector = sectors.get(sectorAddr);
      if (!sector) {
        sector = new Uint8Array(SECTOR).fill(0xff);
        sectors.set(sectorAddr, sector);
      }
      const into = addr + off - sectorAddr;
      const n = Math.min(SECTOR - into, data.length - off);
      sector.set(data.subarray(off, off + n), into);
      off += n;
    }
  }

  return [...sectors.keys()]
    .sort((a, b) => a - b)
    .map((addr) => ({ addr, data: sectors.get(addr) }));
}

export function describe(parsed) {
  const first = parsed.chunks[0].addr;
  const last = parsed.chunks[parsed.chunks.length - 1];
  return `${parsed.chunks.length} blocks, ${parsed.bytes} bytes, ${hex(first)}-${hex(last.addr + last.data.length)}`;
}
