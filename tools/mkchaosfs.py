#!/usr/bin/env python3
"""
mkchaosfs.py — ChaosFS Disk Image Tool

Formats a ChaosFS region in a CLAOS disk image and manages files.

Usage:
    python mkchaosfs.py <image> --format              Format ChaosFS region
    python mkchaosfs.py <image> --add <path> <data>   Add a file (inline data)
    python mkchaosfs.py <image> --addfile <path> <src> Add a file from host filesystem
    python mkchaosfs.py <image> --list                List files
    python mkchaosfs.py <image> --mkdir <path>        Create directory entry
"""

import sys
import struct
import os

# ChaosFS constants (must match chaosfs.h)
CHAOSFS_MAGIC = b"CHAOSFS!"
CHAOSFS_VERSION = 1
CHAOSFS_START_SECTOR = 2048       # 1MB into the disk
CHAOSFS_BLOCK_SIZE = 4096
CHAOSFS_MAX_FILES = 256
CHAOSFS_MAX_FILENAME = 108
ENTRY_SIZE = 128                  # Bytes per file table entry
SECTOR_SIZE = 512

# Superblock format (512 bytes total)
# magic(8) + version(4) + block_size(4) + total_blocks(4) + file_count(4)
# + max_files(4) + data_start_sector(4) + next_free_block(4) + reserved(468)
SUPERBLOCK_FMT = "<8sIIIIIII"
SUPERBLOCK_SIZE = SECTOR_SIZE

# File entry format (128 bytes)
# filename(108) + size(4) + start_block(4) + block_count(4) + flags(1) + reserved(7)
ENTRY_FMT = "<108sIIIB7s"

FLAG_DIR = 0x01
FLAG_DELETED = 0x02


def read_superblock(f):
    f.seek(CHAOSFS_START_SECTOR * SECTOR_SIZE)
    data = f.read(SUPERBLOCK_SIZE)
    fields = struct.unpack(SUPERBLOCK_FMT, data[:36])
    return {
        'magic': fields[0],
        'version': fields[1],
        'block_size': fields[2],
        'total_blocks': fields[3],
        'file_count': fields[4],
        'max_files': fields[5],
        'data_start_sector': fields[6],
        'next_free_block': fields[7],
    }


def write_superblock(f, sb):
    f.seek(CHAOSFS_START_SECTOR * SECTOR_SIZE)
    header = struct.pack(SUPERBLOCK_FMT,
        sb['magic'], sb['version'], sb['block_size'], sb['total_blocks'],
        sb['file_count'], sb['max_files'], sb['data_start_sector'],
        sb['next_free_block'])
    f.write(header + b'\x00' * (SUPERBLOCK_SIZE - len(header)))


def read_entries(f, max_files):
    table_offset = (CHAOSFS_START_SECTOR + 1) * SECTOR_SIZE
    f.seek(table_offset)
    entries = []
    for i in range(max_files):
        data = f.read(ENTRY_SIZE)
        if len(data) < ENTRY_SIZE:
            break
        fields = struct.unpack(ENTRY_FMT, data)
        name = fields[0].split(b'\x00')[0].decode('ascii', errors='replace')
        entries.append({
            'filename': name,
            'size': fields[1],
            'start_block': fields[2],
            'block_count': fields[3],
            'flags': fields[4],
        })
    return entries


def write_entry(f, idx, entry):
    table_offset = (CHAOSFS_START_SECTOR + 1) * SECTOR_SIZE
    f.seek(table_offset + idx * ENTRY_SIZE)
    name_bytes = entry['filename'].encode('ascii')[:CHAOSFS_MAX_FILENAME]
    name_bytes = name_bytes.ljust(CHAOSFS_MAX_FILENAME, b'\x00')
    data = struct.pack(ENTRY_FMT, name_bytes, entry['size'],
                       entry['start_block'], entry['block_count'],
                       entry['flags'], b'\x00' * 7)
    f.write(data)


def cmd_format(image_path):
    """Format the ChaosFS region."""
    # Calculate layout
    table_sectors = (CHAOSFS_MAX_FILES * ENTRY_SIZE + SECTOR_SIZE - 1) // SECTOR_SIZE
    data_start = CHAOSFS_START_SECTOR + 1 + table_sectors

    # Calculate total blocks available (assume 16MB disk, ChaosFS gets the rest)
    disk_size = os.path.getsize(image_path)
    data_sectors = (disk_size // SECTOR_SIZE) - data_start
    total_blocks = data_sectors * SECTOR_SIZE // CHAOSFS_BLOCK_SIZE

    sb = {
        'magic': CHAOSFS_MAGIC,
        'version': CHAOSFS_VERSION,
        'block_size': CHAOSFS_BLOCK_SIZE,
        'total_blocks': total_blocks,
        'file_count': 0,
        'max_files': CHAOSFS_MAX_FILES,
        'data_start_sector': data_start,
        'next_free_block': 0,
    }

    with open(image_path, 'r+b') as f:
        write_superblock(f, sb)
        # Zero out file table
        table_offset = (CHAOSFS_START_SECTOR + 1) * SECTOR_SIZE
        f.seek(table_offset)
        f.write(b'\x00' * (CHAOSFS_MAX_FILES * ENTRY_SIZE))

    print(f"ChaosFS formatted: {total_blocks} blocks ({total_blocks * CHAOSFS_BLOCK_SIZE // 1024}KB)")
    print(f"  Data starts at sector {data_start}")


def cmd_add(image_path, path, data):
    """Add a file with inline data."""
    if isinstance(data, str):
        data = data.encode('utf-8')

    with open(image_path, 'r+b') as f:
        sb = read_superblock(f)
        if sb['magic'] != CHAOSFS_MAGIC:
            print("Error: ChaosFS not formatted. Run --format first.")
            return

        entries = read_entries(f, sb['max_files'])

        # Find free entry
        idx = None
        for i, e in enumerate(entries):
            if not e['filename'] or (e['flags'] & FLAG_DELETED):
                idx = i
                break
        if idx is None:
            print("Error: File table full")
            return

        # Allocate blocks
        needed = max(1, (len(data) + CHAOSFS_BLOCK_SIZE - 1) // CHAOSFS_BLOCK_SIZE)

        entry = {
            'filename': path,
            'size': len(data),
            'start_block': sb['next_free_block'],
            'block_count': needed,
            'flags': 0,
        }

        # Write file data
        data_offset = sb['data_start_sector'] * SECTOR_SIZE + sb['next_free_block'] * CHAOSFS_BLOCK_SIZE
        f.seek(data_offset)
        f.write(data)
        # Pad to block boundary
        remainder = CHAOSFS_BLOCK_SIZE - (len(data) % CHAOSFS_BLOCK_SIZE)
        if remainder < CHAOSFS_BLOCK_SIZE:
            f.write(b'\x00' * remainder)

        # Update entry
        write_entry(f, idx, entry)

        # Update superblock
        sb['next_free_block'] += needed
        sb['file_count'] += 1
        write_superblock(f, sb)

    print(f"Added: {path} ({len(data)} bytes, {needed} blocks)")


def cmd_addfile(image_path, path, src_path):
    """Add a file from the host filesystem."""
    with open(src_path, 'rb') as sf:
        data = sf.read()
    cmd_add(image_path, path, data)


def cmd_mkdir(image_path, path):
    """Create a directory entry."""
    with open(image_path, 'r+b') as f:
        sb = read_superblock(f)
        entries = read_entries(f, sb['max_files'])

        idx = None
        for i, e in enumerate(entries):
            if not e['filename'] or (e['flags'] & FLAG_DELETED):
                idx = i
                break
        if idx is None:
            print("Error: File table full")
            return

        entry = {
            'filename': path,
            'size': 0,
            'start_block': 0,
            'block_count': 0,
            'flags': FLAG_DIR,
        }
        write_entry(f, idx, entry)

        sb['file_count'] += 1
        write_superblock(f, sb)

    print(f"Created directory: {path}")


def cmd_list(image_path):
    """List all files."""
    with open(image_path, 'rb') as f:
        sb = read_superblock(f)
        if sb['magic'] != CHAOSFS_MAGIC:
            print("Error: ChaosFS not formatted")
            return

        entries = read_entries(f, sb['max_files'])

    print(f"ChaosFS v{sb['version']} — {sb['file_count']} files")
    print(f"{'FLAGS':6} {'SIZE':>8}  PATH")
    print("-" * 40)
    for e in entries:
        if not e['filename'] or (e['flags'] & FLAG_DELETED):
            continue
        flags = 'd' if (e['flags'] & FLAG_DIR) else '-'
        print(f"  {flags}     {e['size']:>8}  {e['filename']}")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    image = sys.argv[1]
    cmd = sys.argv[2]

    if cmd == '--format':
        cmd_format(image)
    elif cmd == '--add' and len(sys.argv) >= 5:
        cmd_add(image, sys.argv[3], ' '.join(sys.argv[4:]))
    elif cmd == '--addfile' and len(sys.argv) >= 5:
        cmd_addfile(image, sys.argv[3], sys.argv[4])
    elif cmd == '--mkdir' and len(sys.argv) >= 4:
        cmd_mkdir(image, sys.argv[3])
    elif cmd == '--list':
        cmd_list(image)
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == '__main__':
    main()
