// Raw USB disk backed by the unused part of the W25Q128 (16 MiB).
// Blank flash is initialized as FAT16; the USB host then owns the filesystem.
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "tusb.h"

#define BLOCK_SIZE 512u
#define DISK_BYTES (PICO_FLASH_SIZE_BYTES - JOYFON_STORAGE_OFFSET)
#define BLOCK_COUNT (DISK_BYTES / BLOCK_SIZE)

_Static_assert(JOYFON_STORAGE_OFFSET % FLASH_SECTOR_SIZE == 0, "Storage must be erase aligned");
_Static_assert(JOYFON_STORAGE_OFFSET < PICO_FLASH_SIZE_BYTES, "Storage must fit flash");
_Static_assert(DISK_BYTES % FLASH_SECTOR_SIZE == 0, "Disk must end on an erase boundary");

static uint8_t sector[FLASH_SECTOR_SIZE];
static bool ejected;
static bool init_failed;

void msc_disk_mount(void) { ejected = false; }

static bool ready(uint8_t lun)
{
  if (lun != 0 || ejected || init_failed) {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return false;
  }
  return true;
}

// FAT16 superfloppy (volume starts at LBA 0, no partition table).
// Align all metadata regions to the flash's eight-block erase sectors.
#define FAT_RESERVED_BLOCKS 8u
#define FAT_BLOCKS 64u
#define FAT_ROOT_BLOCKS 32u
#define FAT_ROOT_START (FAT_RESERVED_BLOCKS + 2u * FAT_BLOCKS)
#define FAT_DATA_START (FAT_ROOT_START + FAT_ROOT_BLOCKS)
#define FAT_CLUSTER_BLOCKS 2u
#define FAT_CLUSTERS ((BLOCK_COUNT - FAT_DATA_START) / FAT_CLUSTER_BLOCKS)
_Static_assert(BLOCK_COUNT < 65536, "FAT16 small-volume sector count must fit");
_Static_assert(FAT_CLUSTERS >= 4085 && FAT_CLUSTERS < 65525, "Invalid FAT16 cluster count");
_Static_assert(FAT_CLUSTERS + 2 <= FAT_BLOCKS * BLOCK_SIZE / 2, "FAT must fit all clusters");

static void put_le16(uint8_t *p, uint16_t value)
{
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

// Call once on core 0, before USB enumeration. Never infer blankness from an
// invalid boot signature: an unknown or damaged filesystem may contain files.
void msc_disk_init(void)
{
  const uint8_t *disk = (const uint8_t *)(XIP_BASE + JOYFON_STORAGE_OFFSET);
  init_failed = false;
  for (uint32_t i = 0; i < DISK_BYTES; ++i) {
    if (disk[i] != 0xff) return;
  }

  static uint8_t metadata[FLASH_SECTOR_SIZE];
  // Initialize both FATs and the root directory before publishing the BPB.
  // If power fails, an incomplete disk is left for manual recovery, never
  // automatically erased on the next boot.
  for (uint32_t block = FAT_RESERVED_BLOCKS; block < FAT_DATA_START; block += 8) {
    memset(metadata, 0, sizeof metadata);
    if (block == FAT_RESERVED_BLOCKS || block == FAT_RESERVED_BLOCKS + FAT_BLOCKS) {
      metadata[0] = 0xf8; // fixed-disk media descriptor, FAT entry 0 = 0xfff8
      metadata[1] = metadata[2] = metadata[3] = 0xff;
    }
    if (block == FAT_ROOT_START) {
      memcpy(metadata, "JOYFON     ", 11);
      metadata[11] = 0x08; // root directory volume label
    }
    if (tud_msc_write10_cb(0, block, 0, metadata, sizeof metadata) < 0) {
      init_failed = true;
      return;
    }
  }

  memset(metadata, 0, sizeof metadata);
  metadata[0] = 0xeb; metadata[1] = 0x3c; metadata[2] = 0x90;
  memcpy(metadata + 3, "MSWIN4.1", 8);
  put_le16(metadata + 11, BLOCK_SIZE);
  metadata[13] = FAT_CLUSTER_BLOCKS;
  put_le16(metadata + 14, FAT_RESERVED_BLOCKS);
  metadata[16] = 2;
  put_le16(metadata + 17, FAT_ROOT_BLOCKS * BLOCK_SIZE / 32);
  put_le16(metadata + 19, BLOCK_COUNT);
  metadata[21] = 0xf8;
  put_le16(metadata + 22, FAT_BLOCKS);
  put_le16(metadata + 24, 32); // nominal sectors per track
  put_le16(metadata + 26, 64); // nominal heads
  metadata[36] = 0x80;
  metadata[38] = 0x29; // extended boot signature
  memcpy(metadata + 39, "JF01", 4); // volume ID
  memcpy(metadata + 43, "JOYFON     ", 11);
  memcpy(metadata + 54, "FAT16   ", 8);
  metadata[62] = 0xeb; metadata[63] = 0xfe; // non-bootable loop
  metadata[510] = 0x55; metadata[511] = 0xaa;
  if (tud_msc_write10_cb(0, 0, 0, metadata, sizeof metadata) < 0) init_failed = true;
}

static bool address_valid(uint8_t lun, uint32_t lba, uint32_t offset, uint32_t size)
{
  if (!ready(lun)) return false;
  // Widen before multiplying so malformed requests cannot wrap into firmware.
  uint64_t address = (uint64_t)lba * BLOCK_SIZE + offset;
  if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE || address + size > DISK_BYTES) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
    return false;
  }
  return true;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t rev[4])
{
  (void)lun;
  memcpy(vendor, "JoyFon  ", 8);
  memcpy(product, "Flash Storage   ", 16);
  memcpy(rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) { return ready(lun); }
bool tud_msc_is_writable_cb(uint8_t lun) { return ready(lun); }

void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size)
{
  *count = lun == 0 ? BLOCK_COUNT : 0;
  *size = BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool load_eject)
{
  (void)power;
  if (lun != 0) return false;
  if (load_eject) ejected = !start;
  return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
  if (!address_valid(lun, lba, offset, size)) return -1;
  memcpy(buffer, (const void *)(XIP_BASE + JOYFON_STORAGE_OFFSET + lba * BLOCK_SIZE + offset), size);
  return (int32_t)size;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
  if (!address_valid(lun, lba, offset, size)) return -1;
  uint32_t address = JOYFON_STORAGE_OFFSET + lba * BLOCK_SIZE + offset;
  uint32_t remaining = size;
  while (remaining) {
    uint32_t base = address & ~(FLASH_SECTOR_SIZE - 1u);
    uint32_t within = address - base;
    uint32_t chunk = MIN(remaining, FLASH_SECTOR_SIZE - within);
    const uint8_t *mapped = (const uint8_t *)(XIP_BASE + base);
    if (memcmp(mapped + within, buffer, chunk) != 0) {
      memcpy(sector, mapped, sizeof sector);
      memcpy(sector + within, buffer, chunk);
      // This application uses core 0 only. Interrupts must not fetch code from
      // XIP while the SDK's RAM-resident erase/program routines run. If core 1
      // is ever started, add multicore flash lockout before using this driver.
      uint32_t irq_state = save_and_disable_interrupts();
      flash_range_erase(base, FLASH_SECTOR_SIZE);
      flash_range_program(base, sector, sizeof sector);
      restore_interrupts(irq_state);
      if (memcmp(mapped, sector, sizeof sector) != 0) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
        return -1;
      }
    }
    address += chunk;
    buffer += chunk;
    remaining -= chunk;
  }
  // Write-through: success is returned only after the flash is programmed.
  return (int32_t)size;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t command[16], void *buffer, uint16_t size)
{
  (void)buffer; (void)size;
  if (!ready(lun)) return -1;
  switch (command[0]) {
    case 0x35: // SYNCHRONIZE CACHE(10): writes are already persistent.
    case 0x1e: // PREVENT/ALLOW MEDIUM REMOVAL: no physical removable medium.
      return 0;
    default:
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
      return -1;
  }
}
