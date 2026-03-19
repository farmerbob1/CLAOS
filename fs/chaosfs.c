/*
 * CLAOS — Claude Assisted Operating System
 * chaosfs.c — ChaosFS Custom Filesystem
 *
 * "Mounting consciousness storage... done."
 *
 * ChaosFS is beautifully simple:
 *   - Superblock tells us where everything is
 *   - File table is a flat array of entries (loaded into RAM)
 *   - Data region stores files contiguously (append-only allocation)
 *   - No cluster chains, no allocation tables, no fragmentation logic
 *
 * File paths use forward slashes: /scripts/hello.lua
 * The file table stores full paths, so "directories" are just a
 * naming convention — /scripts/ is a prefix, not a real inode.
 */

#include "chaosfs.h"
#include "ata.h"
#include "string.h"
#include "io.h"

/* In-memory copy of the superblock and file table */
static struct chaosfs_superblock superblock;
static struct chaosfs_entry file_table[CHAOSFS_MAX_FILES];
static bool mounted = false;

/* Sector buffer for disk I/O */
static uint8_t sector_buf[4096] __attribute__((aligned(4)));

/* Save the superblock and file table back to disk */
static int chaosfs_sync(void) {
    /* Write superblock (1 sector) */
    if (ata_write_sectors(CHAOSFS_START_SECTOR, 1, &superblock) < 0) {
        serial_print("[ChaosFS] Failed to write superblock\n");
        return -1;
    }

    /* Write file table.
     * File table size = max_files * 128 bytes.
     * 256 entries * 128 bytes = 32768 bytes = 64 sectors */
    uint32_t table_sectors = (CHAOSFS_MAX_FILES * sizeof(struct chaosfs_entry) + 511) / 512;
    uint32_t table_start = CHAOSFS_START_SECTOR + 1;

    /* Write in chunks of 255 sectors (ATA limit per call) */
    uint8_t* table_ptr = (uint8_t*)file_table;
    uint32_t remaining = table_sectors;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint8_t chunk = remaining > 128 ? 128 : (uint8_t)remaining;
        if (ata_write_sectors(table_start + offset, chunk, table_ptr + offset * 512) < 0) {
            serial_print("[ChaosFS] Failed to write file table\n");
            return -1;
        }
        offset += chunk;
        remaining -= chunk;
    }

    return 0;
}

/* Find a file entry by path. Returns index or -1. */
static int chaosfs_find(const char* path) {
    for (int i = 0; i < CHAOSFS_MAX_FILES; i++) {
        if (file_table[i].filename[0] == '\0') continue;
        if (file_table[i].flags & CHAOSFS_FLAG_DELETED) continue;
        if (strcmp(file_table[i].filename, path) == 0) {
            return i;
        }
    }
    /* Debug: log failed lookups */
    serial_print("[ChaosFS] File not found: ");
    serial_print(path);
    serial_print("\n");
    return -1;
}

/* Find a free entry in the file table */
static int chaosfs_find_free(void) {
    for (int i = 0; i < CHAOSFS_MAX_FILES; i++) {
        if (file_table[i].filename[0] == '\0' ||
            (file_table[i].flags & CHAOSFS_FLAG_DELETED)) {
            return i;
        }
    }
    return -1;
}

bool chaosfs_init(void) {
    if (!ata_drive_present()) {
        serial_print("[ChaosFS] No ATA drive\n");
        return false;
    }

    /* Read the superblock */
    if (ata_read_sectors(CHAOSFS_START_SECTOR, 1, &superblock) < 0) {
        serial_print("[ChaosFS] Failed to read superblock\n");
        return false;
    }

    /* Validate magic */
    if (memcmp(superblock.magic, CHAOSFS_MAGIC, CHAOSFS_MAGIC_LEN) != 0) {
        serial_print("[ChaosFS] No valid filesystem found (bad magic)\n");
        return false;
    }

    if (superblock.version != CHAOSFS_VERSION) {
        serial_print("[ChaosFS] Unsupported version\n");
        return false;
    }

    /* Read the file table */
    uint32_t table_sectors = (superblock.max_files * sizeof(struct chaosfs_entry) + 511) / 512;
    uint32_t table_start = CHAOSFS_START_SECTOR + 1;

    uint8_t* table_ptr = (uint8_t*)file_table;
    uint32_t remaining = table_sectors;
    uint32_t offset = 0;

    while (remaining > 0) {
        uint8_t chunk = remaining > 128 ? 128 : (uint8_t)remaining;
        if (ata_read_sectors(table_start + offset, chunk, table_ptr + offset * 512) < 0) {
            serial_print("[ChaosFS] Failed to read file table\n");
            return false;
        }
        offset += chunk;
        remaining -= chunk;
    }

    mounted = true;
    serial_print("[ChaosFS] Mounted! ");

    /* Count active files */
    int count = 0;
    for (int i = 0; i < (int)superblock.max_files; i++) {
        if (file_table[i].filename[0] != '\0' &&
            !(file_table[i].flags & CHAOSFS_FLAG_DELETED)) {
            count++;
        }
    }

    /* Print file count to serial */
    char buf[12]; int pos = 0;
    int tmp = count;
    if (tmp == 0) buf[pos++] = '0';
    else { char r[12]; int ri=0; while(tmp>0){r[ri++]='0'+tmp%10;tmp/=10;} while(ri>0)buf[pos++]=r[--ri]; }
    buf[pos]='\0';
    serial_print(buf);
    serial_print(" files\n");

    /* Debug: dump all entries */
    for (int i = 0; i < (int)superblock.max_files && i < 20; i++) {
        if (file_table[i].filename[0] == '\0') continue;
        if (file_table[i].flags & CHAOSFS_FLAG_DELETED) continue;
        serial_print("  ["); serial_print(buf); serial_print("] ");
        char ibuf[4]; ibuf[0]='0'+i/10; ibuf[1]='0'+i%10; ibuf[2]=0;
        serial_print(ibuf);
        serial_print(": ");
        serial_print(file_table[i].filename);
        serial_print("\n");
    }

    return true;
}

void chaosfs_list(const char* path_prefix, chaosfs_list_cb callback, void* ctx) {
    if (!mounted) return;

    int prefix_len = strlen(path_prefix);
    /* Sanity check — never scan beyond our array */
    int max = (int)superblock.max_files;
    if (max > CHAOSFS_MAX_FILES) max = CHAOSFS_MAX_FILES;

    for (int i = 0; i < max; i++) {
        if (file_table[i].filename[0] == '\0') continue;
        if (file_table[i].flags & CHAOSFS_FLAG_DELETED) continue;

        /* Check if this entry matches the prefix */
        if (prefix_len == 0 || prefix_len == 1 ||
            strncmp(file_table[i].filename, path_prefix, prefix_len) == 0) {
            serial_print("[ChaosFS] list entry: ");
            serial_print(file_table[i].filename);
            serial_print("\n");
            callback(&file_table[i], ctx);
        }
    }
}

int chaosfs_read(const char* path, void* buf, size_t buf_size) {
    if (!mounted) return -1;

    int idx = chaosfs_find(path);
    if (idx < 0) return -1;

    struct chaosfs_entry* entry = &file_table[idx];
    size_t to_read = entry->size;
    if (to_read > buf_size) to_read = buf_size;

    /* Calculate the absolute sector of the file's data */
    uint32_t data_sector = superblock.data_start_sector +
                           (entry->start_block * CHAOSFS_BLOCK_SIZE / 512);

    /* Read in chunks */
    uint8_t* dst = (uint8_t*)buf;
    size_t remaining = to_read;
    uint32_t sector = data_sector;

    while (remaining > 0) {
        /* Read through sector_buf (4KB = 8 sectors max per chunk) */
        uint8_t count = remaining >= sizeof(sector_buf) ? (sizeof(sector_buf) / 512) :
                        (uint8_t)((remaining + 511) / 512);
        if (ata_read_sectors(sector, count, sector_buf) < 0) {
            return -1;
        }

        size_t copy = count * 512;
        if (copy > remaining) copy = remaining;
        memcpy(dst, sector_buf, copy);

        dst += copy;
        remaining -= copy;
        sector += count;
    }

    return (int)to_read;
}

int chaosfs_write(const char* path, const void* data, size_t size) {
    if (!mounted) return -1;

    /* Check if file already exists */
    int idx = chaosfs_find(path);

    if (idx >= 0) {
        /* File exists — if new data fits in existing blocks, overwrite */
        struct chaosfs_entry* entry = &file_table[idx];
        uint32_t needed_blocks = (size + CHAOSFS_BLOCK_SIZE - 1) / CHAOSFS_BLOCK_SIZE;
        if (needed_blocks == 0) needed_blocks = 1;

        if (needed_blocks <= entry->block_count) {
            /* Fits! Overwrite in place */
            entry->size = size;

            uint32_t data_sector = superblock.data_start_sector +
                                   (entry->start_block * CHAOSFS_BLOCK_SIZE / 512);

            const uint8_t* src = (const uint8_t*)data;
            size_t remaining = size;
            uint32_t sector = data_sector;

            while (remaining > 0) {
                /* Prepare a sector-aligned buffer */
                size_t chunk = remaining > sizeof(sector_buf) ? sizeof(sector_buf) : remaining;
                uint8_t sectors = (uint8_t)((chunk + 511) / 512);

                memset(sector_buf, 0, sectors * 512);
                memcpy(sector_buf, src, chunk);

                if (ata_write_sectors(sector, sectors, sector_buf) < 0) return -1;

                src += chunk;
                remaining -= chunk;
                sector += sectors;
            }

            return chaosfs_sync();
        }

        /* Doesn't fit — delete old, create new */
        file_table[idx].flags |= CHAOSFS_FLAG_DELETED;
    }

    /* Create new file */
    idx = chaosfs_find_free();
    if (idx < 0) {
        serial_print("[ChaosFS] File table full\n");
        return -1;
    }

    uint32_t needed_blocks = (size + CHAOSFS_BLOCK_SIZE - 1) / CHAOSFS_BLOCK_SIZE;
    if (needed_blocks == 0) needed_blocks = 1;

    /* Allocate from the end of used space */
    struct chaosfs_entry* entry = &file_table[idx];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->filename, path, CHAOSFS_MAX_FILENAME - 1);
    entry->size = size;
    entry->start_block = superblock.next_free_block;
    entry->block_count = needed_blocks;
    entry->flags = 0;

    superblock.next_free_block += needed_blocks;
    superblock.file_count++;

    /* Write the data */
    uint32_t data_sector = superblock.data_start_sector +
                           (entry->start_block * CHAOSFS_BLOCK_SIZE / 512);

    const uint8_t* src = (const uint8_t*)data;
    size_t remaining = size;
    uint32_t sector = data_sector;

    while (remaining > 0) {
        size_t chunk = remaining > sizeof(sector_buf) ? sizeof(sector_buf) : remaining;
        uint8_t sectors = (uint8_t)((chunk + 511) / 512);

        memset(sector_buf, 0, sectors * 512);
        memcpy(sector_buf, src, chunk);

        if (ata_write_sectors(sector, sectors, sector_buf) < 0) return -1;

        src += chunk;
        remaining -= chunk;
        sector += sectors;
    }

    return chaosfs_sync();
}

int chaosfs_delete(const char* path) {
    if (!mounted) return -1;

    int idx = chaosfs_find(path);
    if (idx < 0) return -1;

    file_table[idx].flags |= CHAOSFS_FLAG_DELETED;
    superblock.file_count--;

    return chaosfs_sync();
}

int chaosfs_stat(const char* path, uint32_t* size, uint8_t* flags) {
    if (!mounted) return -1;

    int idx = chaosfs_find(path);
    if (idx < 0) return -1;

    if (size) *size = file_table[idx].size;
    if (flags) *flags = file_table[idx].flags;
    return 0;
}

int chaosfs_mkdir(const char* path) {
    if (!mounted) return -1;

    /* Check if already exists */
    if (chaosfs_find(path) >= 0) return 0;

    int idx = chaosfs_find_free();
    if (idx < 0) return -1;

    struct chaosfs_entry* entry = &file_table[idx];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->filename, path, CHAOSFS_MAX_FILENAME - 1);
    entry->size = 0;
    entry->start_block = 0;
    entry->block_count = 0;
    entry->flags = CHAOSFS_FLAG_DIR;

    superblock.file_count++;
    return chaosfs_sync();
}


void chaosfs_disk_stats(uint32_t* total_blocks, uint32_t* used_blocks,
                         uint32_t* file_count, uint32_t* block_size) {
    if (!mounted) {
        if (total_blocks) *total_blocks = 0;
        if (used_blocks) *used_blocks = 0;
        if (file_count) *file_count = 0;
        if (block_size) *block_size = 0;
        return;
    }
    if (total_blocks) *total_blocks = superblock.total_blocks;
    if (used_blocks) *used_blocks = superblock.next_free_block;
    if (file_count) {
        uint32_t count = 0;
        for (int i = 0; i < (int)superblock.max_files; i++) {
            if (file_table[i].filename[0] != '\0' &&
                !(file_table[i].flags & CHAOSFS_FLAG_DELETED))
                count++;
        }
        *file_count = count;
    }
    if (block_size) *block_size = superblock.block_size;
}
