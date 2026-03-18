# CLAOS — Claude Assisted Operating System
## Master Build Prompt for Claude Code (v3 — Filesystem + Lua + GUI)

---

## Project Overview

Build **CLAOS** (pronounced "Chaos") — a toy x86 operating system written entirely from scratch with NO dependency on Linux, Windows, macOS, or any existing OS kernel. CLAOS is an "AI-native OS" where Claude (Anthropic's AI) is integrated at the kernel level as a core system service. Claude can be prompted interactively from the OS shell, receives crash/panic reports automatically, and can send back patches or commands.

This is a meme project and educational toy — not a production OS. Prioritize "cool and functional demo" over robustness.

**CRITICAL DESIGN DECISION: CLAOS connects to the Anthropic API directly via native HTTPS. No relay. No host-side scripts. The OS handles TCP/IP, TLS, and HTTPS entirely on its own using a ported BearSSL library. This means CLAOS can run on real bare-metal hardware with just an internet connection.**

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│                  CLAOS Stack                      │
├──────────────────────────────────────────────────┤
│  GUI Desktop (VESA framebuffer, window manager)   │  Phase 8
├──────────────────────────────────────────────────┤
│  Lua 5.4 Scripting (ClaudeScript API bindings)    │  Phase 7
├──────────────────────────────────────────────────┤
│  ChaosFS (custom filesystem, ATA/IDE driver)      │  Phase 6
├──────────────────────────────────────────────────┤
│  ClaudeShell (AI-first interactive shell)          │  Phase 5
├──────────────────────────────────────────────────┤
│  Claude Protocol + HTTPS Client                   │  Phase 4
│  (JSON/Messages API over HTTP/1.1 + TLS 1.2)     │
├──────────────────────────────────────────────────┤
│  TLS 1.2 (BearSSL port, entropy pool)             │  Phase 3.5
├──────────────────────────────────────────────────┤
│  Network Stack                                    │  Phase 3
│  (e1000 NIC → Ethernet → ARP → IPv4 →            │
│   DNS/UDP → TCP)                                  │
├──────────────────────────────────────────────────┤
│  Memory Management & Scheduler                    │  Phase 2
│  (PMM, paging, heap, preemptive scheduler)        │
├──────────────────────────────────────────────────┤
│  Kernel + Drivers                                 │  Phase 1
│  (GDT, IDT, ISRs, VGA, PS/2 keyboard, PIT timer) │
├──────────────────────────────────────────────────┤
│  Bootloader                                       │  Phase 1
│  (Stage 1 MBR → Stage 2 → Protected Mode)        │
└──────────────────────────────────────────────────┘
```

### No Relay — Fully Self-Contained

CLAOS talks directly to `api.anthropic.com` over HTTPS. The full network path is:

```
CLAOS (bare metal / VM)
┌─────────────────────────────────────────┐
│ Claude Protocol Layer                    │
│   ↓ JSON body                           │
│ HTTPS Client (HTTP/1.1 over TLS 1.2)    │
│   ↓ encrypted                           │
│ TCP → IPv4 → Ethernet → e1000 NIC      │
└────────────────┬────────────────────────┘
                 │
                 ↓ (real network / internet)
                 │
          api.anthropic.com
```

---

## Build Phases

### PHASE 1: Boot & Kernel Foundation ✅ COMPLETED

1. Cross-compiler toolchain (i686-elf-gcc, NASM, QEMU)
2. Stage 1 Bootloader (MBR, loads Stage 2)
3. Stage 2 Bootloader (GDT, A20, protected mode, jump to C kernel)
4. Kernel entry (boot banner, GDT, IDT, ISRs, IRQs)
5. VGA text mode driver
6. PS/2 Keyboard driver
7. PIT Timer

---

### PHASE 2: Memory Management & Scheduler ✅ COMPLETED

1. Physical memory manager (bitmap page allocator)
2. Virtual memory / Paging
3. Heap allocator (kmalloc / kfree)
4. Basic preemptive round-robin scheduler

---

### PHASE 3: Network Stack ✅ COMPLETED

1. PCI bus enumeration
2. e1000 NIC driver
3. Ethernet frame handling
4. ARP (MAC address resolution)
5. IPv4 (packet construction/parsing)
6. UDP (for DNS)
7. DNS resolver (resolve api.anthropic.com)
8. TCP (state machine, seq/ack tracking)

---

### PHASE 3.5: TLS via BearSSL Port ✅ COMPLETED

1. BearSSL source integration (294 files compiled freestanding)
2. CLAOS compatibility shim
3. BearSSL I/O callbacks wired to TCP stack
4. Certificate trust store (root CAs for api.anthropic.com)
5. TLS client wrapper API
6. Entropy pool (PIT + RDTSC + interrupt timing)

---

### PHASE 4: HTTPS Client & Claude Integration ✅ COMPLETED

1. HTTPS client (HTTP/1.1 over TLS, chunked encoding)
2. Claude Protocol Layer (Anthropic Messages API)
3. Minimal JSON parser
4. Runtime API key configuration (interactive `config` command)
5. Panic-to-Claude handler

---

### PHASE 5: ClaudeShell ✅ COMPLETED

1. **ClaudeShell** (`shell/shell.c`)
   - Prompt: `claos> `
   - Built-in commands: `claude`, `config`, `sysinfo`, `tasks`, `net`, `dns`, `tls`, `uptime`, `clear`, `panic`, `reboot`, `help`
   - Unrecognized commands automatically sent to Claude with context

---

### PHASE 6: ChaosFS — Custom Filesystem ← BUILD THIS NEXT

**Goal:** Give CLAOS persistent storage so files survive reboots. This unlocks Lua script storage, chat log saving, config files, and eventually GUI assets.

**Why Custom (ChaosFS):** Fits the "everything from scratch" philosophy. No legacy 8.3 filename restrictions, no FAT cluster chains, no BPB complexity. Simple flat design, long filenames natively, and we understand every byte on disk. A small Python tool handles writing files to the disk image from the host.

**ChaosFS Design:**

```
┌──────────────────────┐  Sector 0
│ MBR + Stage 1 Boot   │  Bootloader (512 bytes)
├──────────────────────┤
│ Stage 2 + Kernel     │  Sectors 1 to KERNEL_END
├──────────────────────┤  CHAOSFS_START (e.g., sector 2048 = 1MB offset)
│ Superblock           │  Magic "CHAOSFS!", version, file count, block size
├──────────────────────┤
│ File Table           │  Fixed array of file entries (name, size, offset, flags)
├──────────────────────┤
│ Data Region          │  Contiguous file data blocks
└──────────────────────┘
```

**Superblock** (1 sector = 512 bytes):
- Magic: `CHAOSFS!` (8 bytes)
- Version: `1` (uint32)
- Block size: `4096` (uint32)
- Total blocks in data region (uint32)
- File count (uint32)
- Max files (uint32) — fixed at format time (e.g., 256)
- Data region start sector (uint32)

**File Table** (follows superblock, sized for max_files entries):
- Each entry (128 bytes):
  - Filename: up to 108 chars, null-terminated (no 8.3 nonsense!)
  - File size in bytes (uint32)
  - Start block in data region (uint32)
  - Block count (uint32)
  - Flags (uint8): bit 0 = directory, bit 1 = deleted
  - Reserved padding

**Data Region** — files stored contiguously (no fragmentation for v1). Each file occupies consecutive blocks. Creating a new file appends to the end of used space. Deletion marks the entry as deleted; compaction is a future optimization.

**Why contiguous allocation:** Dead simple. No allocation tables, no cluster chains, no fragmentation logic. For a toy OS with small files (Lua scripts, config, chat logs), this is more than sufficient.

#### 6.1 — ATA/IDE Block Device Driver (`drivers/ata.c`)

- PIO (Programmed I/O) mode — simplest to implement, no DMA needed
- QEMU emulates an IDE controller by default
- Core functions:
  ```c
  int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
  int ata_write_sectors(uint32_t lba, uint8_t count, const void* buffer);
  ```
- Uses I/O ports 0x1F0-0x1F7 (primary ATA bus)
- 28-bit LBA addressing (supports up to 128GB — more than enough)
- Read/write in 512-byte sectors
- Detect drive presence during boot, report in boot log
- ~200 lines of C

**Testing:** Read sector 0 (MBR) and verify it contains the boot signature 0xAA55.

#### 6.2 — ChaosFS Driver (`fs/chaosfs.c`)

Implement these operations:
- **Mount:** Read superblock, validate magic, load file table into memory
- **List directory:** Scan file table for entries matching a path prefix
- **Read file:** Look up file in table, read contiguous blocks from data region
- **Write file:** If file exists, overwrite in place (if same size or smaller). If new file, allocate blocks at end of data region, add file table entry
- **Create file:** Add entry to file table, allocate contiguous blocks
- **Delete file:** Mark entry as deleted in file table (data stays until compaction)
- **Stat:** Return file size, flags from the file table entry

#### 6.3 — VFS Layer (`fs/vfs.c`)

Uniform API that the rest of CLAOS uses:

```c
// Core VFS API
int     vfs_open(const char* path, int flags);
int     vfs_close(int fd);
int     vfs_read(int fd, void* buf, size_t count);
int     vfs_write(int fd, const void* buf, size_t count);
int     vfs_mkdir(const char* path);
int     vfs_unlink(const char* path);
int     vfs_create(const char* path, const void* data, size_t size);
int     vfs_list(const char* path, char* buf, size_t buf_size);
int     vfs_stat(const char* path, uint32_t* size, uint8_t* flags);
```

- Path format: `/scripts/hello.lua`, `/logs/chat.log` — Unix-style forward slashes
- Simple file descriptor table (16 open files max)

#### 6.4 — Shell Integration

Add new shell commands:
- `ls [path]` — list directory contents
- `cat <file>` — display file contents
- `write <file> <content>` — write text to a file
- `mkdir <path>` — create directory
- `rm <file>` — delete file
- `save <file>` — save last Claude response to a file

#### 6.5 — Disk Image Setup

**Build process update:**
- Disk image is now larger (e.g., 16MB) to fit the ChaosFS data region
- ChaosFS region starts at a fixed offset (sector 2048 = 1MB)
- A Python tool (`tools/mkchaosfs.py`) formats the ChaosFS region and pre-populates files

```bash
# Format ChaosFS and add default files
python tools/mkchaosfs.py claos.img --offset 2048 --format
python tools/mkchaosfs.py claos.img --offset 2048 --add welcome.txt "Welcome to CLAOS!"
python tools/mkchaosfs.py claos.img --offset 2048 --add scripts/hello.lua "print('Hello from Lua!')"
```

**Milestone:** Boot CLAOS, type `ls /`, see files. Type `cat /welcome.txt` and read a file. Type `write /hello.txt Hello from CLAOS!` and create a file that persists across reboots.

---

### PHASE 7: Embedded Lua

**Goal:** Port Lua 5.4 as a scripting language inside CLAOS, with access to CLAOS kernel APIs. This makes CLAOS a programmable AI-native OS.

1. **Port Lua 5.4** (`lib/lua/`)
   - Lua is written in pure ANSI C — designed to be portable
   - Same approach as BearSSL: compile freestanding, link into kernel
   - Lua's core is ~15,000 lines of C
   - **Skip:** liolib.c (uses stdio — replace with our VFS), loslib.c (uses OS calls), loadlib.c (dynamic loading — not needed)
   - **Compatibility shim** (`lib/lua/lua_shim.c`):
     - Map Lua's file operations to our VFS
     - Provide `clock()` using PIT timer
     - Provide `realloc` using `kmalloc`/`kfree`

2. **CLAOS API bindings** (`lua/claos_lib.c`)
   - Register a `claos` Lua library with these functions:
     ```lua
     -- Talk to Claude
     response = claos.ask("What should I name my variable?")

     -- System info
     claos.print("Hello from Lua!")
     uptime = claos.uptime()
     free_mem = claos.mem_free()
     total_mem = claos.mem_total()

     -- Filesystem
     claos.write("/logs/output.txt", "some data")
     data = claos.read("/scripts/config.txt")
     files = claos.ls("/scripts")

     -- System control
     claos.sleep(1000)
     claos.set_color(fg, bg)
     claos.clear()
     ```

3. **Shell integration**
   - `lua` — open interactive Lua REPL
   - `lua <file.lua>` — execute a Lua script from the filesystem
   - `luarun <code>` — execute inline Lua code (one-liners)

4. **Pre-installed scripts** (put on ChaosFS during build)
   - `/scripts/welcome.lua` — prints system info and a Claude greeting
   - `/scripts/sysmon.lua` — loops and displays memory/task info
   - `/scripts/chat.lua` — interactive multi-turn chat with Claude
   - `/scripts/crash_test.lua` — deliberately does bad things to test the panic handler

**Milestone:** Boot CLAOS, type `lua /scripts/chat.lua`, have a multi-turn conversation with Claude powered by a Lua script running on a from-scratch OS with a from-scratch filesystem.

---

### PHASE 8: GUI

Reference the **CLAOS GUI v2 mockup** (human-friendly design) for the target aesthetic.

- **v1 (dark/hacker):** Deep navy background, monospace everything, terminal-native.
- **v2 (light/friendly):** Warm cream background, clean cards, chat-bubble interface, icon sidebar.

**Implement v2 as the default, with v1 available as a "hacker mode" toggle.**

#### 8.1 — Graphics Foundation

1. **VESA framebuffer** — use VBE to switch to graphical mode during Stage 2 boot. Target: 1024x768x32bpp or 800x600x32bpp.
2. **Framebuffer driver** (`drivers/framebuffer.c`) — `fb_putpixel`, `fb_fill_rect`, `fb_draw_line`, `fb_blit`.
3. **Bitmap font renderer** — embed a bitmap font (8x16), render text onto the framebuffer.
4. **Double buffering** — draw to a back buffer, then copy to framebuffer.

#### 8.2 — Input

1. **PS/2 Mouse driver** (`drivers/mouse.c`) — IRQ12, parse mouse packets, track cursor position.
2. **Mouse cursor** — small arrow sprite.
3. **Event system** — keyboard and mouse events dispatched to focused window.

#### 8.3 — Window Manager

1. **Window struct** — position, size, title, content buffer, flags.
2. **Compositor** — draw windows back-to-front, handle overlapping.
3. **Window dragging** — click title bar + drag to move.
4. **Focus management** — click to focus, bring to front.

#### 8.4 — Desktop Environment (targeting v2 mockup)

1. **Top bar** — CLAOS logo, version, network status, memory, uptime.
2. **Sidebar** — icon buttons: Chat, Terminal, System Monitor, Files, Settings.
3. **Claude Chat Window** — chat bubble layout, text input, quick action chips.
4. **Right sidebar** — Claude status, panic watcher, running tasks, recent chats.
5. **Color scheme:**
   - Background: `#f4f1ec` (warm cream)
   - Primary accent: `#7F77DD` (Claude purple)
   - Status green: `#639922`
   - User bubble: `#7F77DD` with white text
   - Claude bubble: `#f9f8f6`
6. **Terminal window** — classic VGA-style terminal in a window.

**Milestone:** Boot CLAOS into a graphical desktop. Click the chat icon. Type a message to Claude. See the response in a chat bubble. Move the window around.

---

## Project Structure

```
claos/
├── boot/
│   ├── stage1.asm            # MBR bootloader
│   └── stage2.asm            # Protected mode setup, loads kernel
├── kernel/
│   ├── main.c                # Kernel entry point
│   ├── gdt.c / gdt.h         # Global Descriptor Table
│   ├── idt.c / idt.h         # Interrupt Descriptor Table
│   ├── isr.asm               # CPU exception stubs
│   ├── irq.asm               # Hardware IRQ stubs
│   ├── gdt_flush.asm         # GDT/IDT register loading
│   ├── entry.asm             # Kernel entry stub
│   ├── pmm.c / pmm.h         # Physical memory manager
│   ├── vmm.c / vmm.h         # Virtual memory / paging
│   ├── heap.c / heap.h       # kmalloc / kfree
│   ├── scheduler.c / .h      # Task scheduler + context switch
│   ├── scheduler_asm.asm     # Context switch routine
│   ├── panic.c / panic.h     # Kernel panic handler
│   ├── entropy.c / entropy.h # Entropy pool for TLS
│   └── string.c              # memcpy, memset, strlen, etc.
├── drivers/
│   ├── vga.c / vga.h         # VGA text mode
│   ├── keyboard.c / .h       # PS/2 keyboard
│   ├── timer.c / timer.h     # PIT timer
│   ├── pci.c / pci.h         # PCI bus enumeration
│   ├── e1000.c / e1000.h     # Intel e1000 NIC
│   ├── ata.c / ata.h         # ATA/IDE block device (Phase 6)
│   ├── mouse.c               # PS/2 mouse (Phase 8)
│   └── framebuffer.c         # VESA framebuffer (Phase 8)
├── fs/                        # Phase 6
│   ├── chaosfs.c / chaosfs.h # ChaosFS filesystem driver
│   └── vfs.c / vfs.h         # Virtual filesystem layer
├── net/
│   ├── net.h                 # Network config & byte-order helpers
│   ├── ethernet.c / .h       # Ethernet frames
│   ├── arp.c / arp.h         # ARP
│   ├── ipv4.c / ipv4.h       # IPv4
│   ├── udp.c / udp.h         # UDP (for DNS)
│   ├── dns.c / dns.h         # DNS resolver
│   ├── tcp.c / tcp.h         # TCP
│   ├── tls_client.c / .h     # TLS client (BearSSL wrapper)
│   ├── ca_certs.c            # Root CA certificates
│   └── https.c / https.h     # HTTPS client
├── lib/
│   ├── bearssl/              # BearSSL TLS library (ported)
│   │   ├── inc/              # BearSSL headers
│   │   ├── src/              # BearSSL source (~294 files)
│   │   └── bearssl_shim.c    # CLAOS compatibility layer
│   └── lua/                  # Lua 5.4 (Phase 7)
│       ├── src/              # Lua core source files
│       └── lua_shim.c        # CLAOS compatibility layer
├── claude/
│   ├── claude.c / claude.h   # Claude API protocol layer
│   ├── json.c / json.h       # Minimal JSON builder/parser
│   ├── panic_handler.c / .h  # Panic → Claude integration
│   ├── config.h              # API key & settings (NOT committed)
│   └── config.h.example      # Template for config.h
├── shell/
│   └── shell.c / shell.h     # ClaudeShell
├── lua/                       # Phase 7
│   └── claos_lib.c           # CLAOS API bindings for Lua
├── gui/                       # Phase 8
│   ├── compositor.c          # Window manager
│   ├── desktop.c             # Desktop environment
│   ├── widgets.c             # UI components
│   ├── chat_window.c         # Claude chat interface
│   ├── terminal_window.c     # Terminal emulator window
│   └── font.c                # Bitmap font renderer
├── tools/
│   ├── setup_toolchain.sh    # Toolchain installer
│   └── mkchaosfs.py          # ChaosFS disk image tool (Phase 6)
├── scripts/                   # Pre-installed Lua scripts (Phase 7)
│   ├── welcome.lua
│   ├── sysmon.lua
│   ├── chat.lua
│   └── crash_test.lua
├── include/
│   ├── types.h               # Fixed-width types
│   ├── string.h              # String function declarations
│   ├── io.h                  # Port I/O + serial debug
│   └── stdlib.h              # Minimal stdlib stub for BearSSL
├── Docs/
│   └── CLAOS-build-prompt-v3.md
├── linker.ld                 # Kernel linker script
├── Makefile                  # Build system
├── run.bat                   # Windows QEMU launcher
├── .gitignore                # Excludes config.h, build artifacts
└── README.md
```

---

## Technical Specifications

- **Architecture:** x86 (i686), 32-bit protected mode
- **Language:** C (kernel, drivers) + x86 NASM assembly (boot, low-level)
- **Compiler:** `i686-elf-gcc` (freestanding, `-ffreestanding -nostdlib`)
- **Assembler:** NASM
- **Target VM:** QEMU (`qemu-system-i386`) with `-device e1000`
- **Boot method:** Legacy BIOS boot from raw disk image (not UEFI)
- **Disk image:** Bootloader + kernel in raw sectors, ChaosFS data region at sector 2048
- **Filesystem:** ChaosFS (custom) — contiguous allocation, long filenames, ATA PIO
- **TLS:** BearSSL (ported), TLS 1.2
- **Scripting:** Lua 5.4 (ported), with CLAOS API bindings
- **No external dependencies** — BearSSL and Lua are the only libraries, both compiled freestanding
- **Network:** Static IP, native TCP/IP + TLS + HTTPS, direct to api.anthropic.com
- **QEMU launch:**
  ```bash
  qemu-system-i386 \
    -drive format=raw,file=claos.img \
    -device e1000,netdev=net0 \
    -netdev user,id=net0 \
    -m 128M \
    -serial stdio
  ```

---

## Implementation Order

1. ~~**Phase 1** — Boot & Kernel Foundation~~ ✅
2. ~~**Phase 2** — Memory Management & Scheduler~~ ✅
3. ~~**Phase 3** — Network Stack~~ ✅
4. ~~**Phase 3.5** — TLS via BearSSL~~ ✅
5. ~~**Phase 4** — HTTPS Client & Claude Integration~~ ✅
6. ~~**Phase 5** — ClaudeShell~~ ✅
7. **Phase 6** — ChaosFS custom filesystem ← **NEXT**
8. **Phase 7** — Embedded Lua
9. **Phase 8** — GUI Desktop

---

## Personality

CLAOS should have personality. It's an AI-native OS and it knows it.

- Boot messages: witty ("Initializing consciousness... done.")
- Panic messages: dramatic ("KERNEL PANIC: I have made a terrible mistake.")
- Filesystem messages: "Mounting consciousness storage... done."
- Lua messages: "Lua 5.4 awakened. The scripting layer stirs."
- Unrecognized shell commands go to Claude automatically

---

## Success Criteria

The project is "done" (for ultimate meme status) when you can:

1. Boot CLAOS in QEMU
2. See the ASCII art banner
3. Type `claude Hello, I am talking to you from my own operating system` and get a real response over native HTTPS
4. Type `panic` and watch Claude diagnose the crash
5. Type `ls /scripts` and see Lua scripts on the ChaosFS filesystem
6. Type `lua /scripts/chat.lua` and have a multi-turn Claude conversation in Lua
7. Boot into the GUI, click the chat window, talk to Claude in a graphical desktop
8. Screenshot it, post it, achieve internet immortality
9. **Ultimate bonus:** Boot on real hardware and do all of the above

Go build something legendary.
