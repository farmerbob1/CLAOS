/*
 * CLAOS — Claude Assisted Operating System
 * chaosfs.h — ChaosFS Custom Filesystem
 *
 * A simple from-scratch filesystem designed for CLAOS.
 * Features: long filenames (108 chars), contiguous allocation,
 * no legacy cruft. Named "ChaosFS" because... CLAOS.
 *
 * On-disk layout:
 *   [Superblock 1 sector] [File Table N sectors] [Data Region]
 *
 * All at a fixed offset from the start of the disk image.
 */

#ifndef CLAOS_CHAOSFS_H
#define CLAOS_CHAOSFS_H

#include "types.h"

/* ChaosFS magic signature */
#define CHAOSFS_MAGIC       "CHAOSFS!"
#define CHAOSFS_MAGIC_LEN   8
#define CHAOSFS_VERSION     1

/* Where ChaosFS starts on disk (sector offset) */
#define CHAOSFS_START_SECTOR 2048   /* 1MB into the disk image */

/* Filesystem limits */
#define CHAOSFS_MAX_FILES    256
#define CHAOSFS_BLOCK_SIZE   4096
#define CHAOSFS_MAX_FILENAME 108

/* File entry flags */
#define CHAOSFS_FLAG_DIR     0x01
#define CHAOSFS_FLAG_DELETED 0x02

/* Superblock — stored in the first sector of the ChaosFS region */
struct chaosfs_superblock {
    char     magic[8];          /* "CHAOSFS!" */
    uint32_t version;
    uint32_t block_size;        /* Bytes per block (4096) */
    uint32_t total_blocks;      /* Total blocks in data region */
    uint32_t file_count;        /* Number of active files */
    uint32_t max_files;         /* Max entries in file table */
    uint32_t data_start_sector; /* Absolute sector where data region begins */
    uint32_t next_free_block;   /* Next block to allocate from */
    uint8_t  reserved[468];     /* Pad to 512 bytes */
} __attribute__((packed));

/* File table entry — 128 bytes each */
struct chaosfs_entry {
    char     filename[CHAOSFS_MAX_FILENAME]; /* Full path, null-terminated */
    uint32_t size;              /* File size in bytes */
    uint32_t start_block;       /* First block in data region */
    uint32_t block_count;       /* Number of contiguous blocks */
    uint8_t  flags;             /* CHAOSFS_FLAG_* */
    uint8_t  reserved[3];       /* Padding to 128 bytes */
} __attribute__((packed));

/* Initialize and mount ChaosFS. Returns true if valid FS found. */
bool chaosfs_init(void);

/* List files. Calls `callback` for each non-deleted entry matching `path_prefix`.
 * If path_prefix is "/" or "", lists all files. */
typedef void (*chaosfs_list_cb)(const struct chaosfs_entry* entry, void* ctx);
void chaosfs_list(const char* path_prefix, chaosfs_list_cb callback, void* ctx);

/* Read a file's contents into buf. Returns bytes read, or -1 on error. */
int chaosfs_read(const char* path, void* buf, size_t buf_size);

/* Write/create a file. Returns 0 on success, -1 on error. */
int chaosfs_write(const char* path, const void* data, size_t size);

/* Delete a file. Returns 0 on success, -1 if not found. */
int chaosfs_delete(const char* path);

/* Get file info. Returns 0 on success, -1 if not found. */
int chaosfs_stat(const char* path, uint32_t* size, uint8_t* flags);

/* Create a directory entry. Returns 0 on success. */
int chaosfs_mkdir(const char* path);

/* Get filesystem stats */
void chaosfs_disk_stats(uint32_t* total_blocks, uint32_t* used_blocks,
                         uint32_t* file_count, uint32_t* block_size);

#endif /* CLAOS_CHAOSFS_H */
