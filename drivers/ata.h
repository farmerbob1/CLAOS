/*
 * CLAOS — Claude Assisted Operating System
 * ata.h — ATA/IDE Block Device Driver
 *
 * PIO mode driver for the primary ATA bus. Reads and writes 512-byte
 * sectors using I/O ports. QEMU emulates this by default.
 */

#ifndef CLAOS_ATA_H
#define CLAOS_ATA_H

#include "types.h"

/* Initialize the ATA driver. Returns true if a drive is detected. */
bool ata_init(void);

/* Read `count` sectors starting at LBA `lba` into `buffer`.
 * buffer must be at least count * 512 bytes.
 * Returns 0 on success, -1 on error. */
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);

/* Write `count` sectors starting at LBA `lba` from `buffer`.
 * Returns 0 on success, -1 on error. */
int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);

/* Check if a drive is present */
bool ata_drive_present(void);

#endif /* CLAOS_ATA_H */
