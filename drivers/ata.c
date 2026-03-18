/*
 * CLAOS — Claude Assisted Operating System
 * ata.c — ATA/IDE Block Device Driver (PIO Mode)
 *
 * Drives the primary ATA bus using Programmed I/O (PIO). This is the
 * simplest way to talk to a hard disk — no DMA, no interrupts, just
 * read/write I/O ports and transfer data word by word.
 *
 * QEMU emulates an IDE controller by default when you provide a
 * -drive argument, so this just works.
 *
 * Primary ATA bus I/O ports:
 *   0x1F0 - Data register (read/write 16-bit words)
 *   0x1F1 - Error/Features
 *   0x1F2 - Sector count
 *   0x1F3 - LBA low (bits 0-7)
 *   0x1F4 - LBA mid (bits 8-15)
 *   0x1F5 - LBA high (bits 16-23)
 *   0x1F6 - Drive/Head (bit 6 = LBA mode, bit 4 = drive select)
 *   0x1F7 - Status (read) / Command (write)
 *   0x3F6 - Alternate status / Device control
 */

#include "ata.h"
#include "io.h"
#include "string.h"

/* ATA I/O ports (primary bus) */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_FEATURES    0x1F1
#define ATA_SECT_COUNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALT_STATUS  0x3F6
#define ATA_DEV_CTRL    0x3F6

/* ATA commands */
#define ATA_CMD_READ    0x20    /* Read sectors (PIO) */
#define ATA_CMD_WRITE   0x30    /* Write sectors (PIO) */
#define ATA_CMD_IDENTIFY 0xEC   /* Identify drive */

/* ATA status register bits */
#define ATA_SR_BSY      0x80    /* Busy */
#define ATA_SR_DRDY     0x40    /* Drive ready */
#define ATA_SR_DRQ      0x08    /* Data request */
#define ATA_SR_ERR      0x01    /* Error */

static bool drive_detected = false;

/* Wait for the drive to not be busy */
static void ata_wait_bsy(void) {
    while (inb(ATA_STATUS) & ATA_SR_BSY);
}

/* Wait for DRQ (data ready) */
static void ata_wait_drq(void) {
    while (!(inb(ATA_STATUS) & ATA_SR_DRQ));
}

/* 400ns delay by reading alternate status 4 times */
static void ata_delay(void) {
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

bool ata_init(void) {
    /* Select drive 0 (master) */
    outb(ATA_DRIVE_HEAD, 0xA0);
    ata_delay();

    /* Send IDENTIFY command */
    outb(ATA_SECT_COUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    /* Check if drive exists */
    uint8_t status = inb(ATA_STATUS);
    if (status == 0) {
        serial_print("[ATA] No drive detected\n");
        drive_detected = false;
        return false;
    }

    /* Wait for BSY to clear */
    ata_wait_bsy();

    /* Check for non-ATA devices (ATAPI, SATA, etc.) */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) {
        serial_print("[ATA] Non-ATA device detected\n");
        drive_detected = false;
        return false;
    }

    /* Wait for DRQ or ERR */
    while (1) {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) {
            serial_print("[ATA] Drive error during identify\n");
            drive_detected = false;
            return false;
        }
        if (status & ATA_SR_DRQ) break;
    }

    /* Read the 256-word identify data (we don't use it, just flush it) */
    for (int i = 0; i < 256; i++) {
        inw(ATA_DATA);
    }

    drive_detected = true;
    serial_print("[ATA] Drive detected and ready\n");
    return true;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer) {
    if (!drive_detected || count == 0) return -1;

    uint16_t* buf = (uint16_t*)buffer;

    ata_wait_bsy();

    /* Select drive 0, LBA mode, with top 4 bits of LBA */
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECT_COUNT, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_READ);

    /* Read each sector */
    for (int s = 0; s < count; s++) {
        ata_delay();
        ata_wait_bsy();

        /* Check for error */
        uint8_t status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) {
            serial_print("[ATA] Read error\n");
            return -1;
        }

        ata_wait_drq();

        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            buf[s * 256 + i] = inw(ATA_DATA);
        }
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer) {
    if (!drive_detected || count == 0) return -1;

    const uint16_t* buf = (const uint16_t*)buffer;

    ata_wait_bsy();

    /* Select drive 0, LBA mode, with top 4 bits of LBA */
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECT_COUNT, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    /* Write each sector */
    for (int s = 0; s < count; s++) {
        ata_delay();
        ata_wait_bsy();

        uint8_t status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) {
            serial_print("[ATA] Write error\n");
            return -1;
        }

        ata_wait_drq();

        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA, buf[s * 256 + i]);
        }
    }

    /* Flush cache */
    outb(ATA_COMMAND, 0xE7);
    ata_wait_bsy();

    return 0;
}

bool ata_drive_present(void) {
    return drive_detected;
}
