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
│  Layer 8: GUI (VESA framebuffer desktop)          │
├──────────────────────────────────────────────────┤
│  Layer 7: Lua 5.4 Scripting Environment           │
│           ClaudeScript API bindings               │
├──────────────────────────────────────────────────┤
│  Layer 6: ClaudeShell (interactive AI shell)       │
├──────────────────────────────────────────────────┤
│  Layer 5: Claude Protocol Layer                   │
│           JSON request/response over HTTPS        │
├──────────────────────────────────────────────────┤
│  Layer 4: HTTPS Client                            │
│           HTTP/1.1 + TLS 1.2 (via BearSSL port)  │
├──────────────────────────────────────────────────┤
│  Layer 3: Network Stack                           │
│           e1000 NIC → Ethernet → ARP → IPv4 →    │
│           DNS (UDP) → TCP                         │
├──────────────────────────────────────────────────┤
│  Layer 2: Drivers + Filesystem                    │
│           VGA, PS/2 keyboard, PIT timer,          │
│           e1000 NIC, ATA/IDE, ChaosFS             │
├──────────────────────────────────────────────────┤
│  Layer 1: Kernel                                  │
│           GDT, IDT, ISRs, physical/virtual        │
│           memory manager, basic scheduler,        │
│           panic handler → Claude                  │
├──────────────────────────────────────────────────┤
│  Layer 0: Bootloader                              │
│           Stage 1 (MBR) → Stage 2 → kernel       │
│           Real mode → Protected mode (32-bit)     │
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

1. BearSSL source integration
2. CLAOS compatibility shim
3. BearSSL I/O callbacks wired to TCP stack
4. Certificate trust store (root CAs for api.anthropic.com)
5. TLS client wrapper API
6. Entropy pool (PIT + RDTSC + interrupt timing)

---

### PHASE 4: HTTPS Client & Claude Integration ✅ COMPLETED

1. HTTPS client (HTTP/1.1 over TLS)
2. Claude Protocol Layer (Anthropic Messages API)
3. Minimal JSON parser
4. API key storage (compile-time config.h)
5. Panic-to-Claude handler

---

### PHASE 5: Interactive Shell ✅ COMPLETED (minus Lua — now Phase 7)

1. **ClaudeShell** (`shell/shell.c`)
   - Prompt: `claos> `
   - Built-in commands:
     - `claude <message>` — send a prompt to Claude and display the response
     - `sysinfo` — show memory usage, uptime, task count, network status
     - `clear` — clear screen
     - `panic` — deliberately trigger a kernel panic (demo panic→Claude flow)
     - `reboot` — triple-fault reboot
     - `help` — list commands
     - `tasks` — show running tasks
     - `net` — show network configuration
     - `ping <ip>` — send ICMP echo request (nice to have)
   - Unrecognized commands sent to Claude with context

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
- ChaosFS partition starts at a fixed offset (sector 2048 = 1MB)
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
   - Lua's core is ~15,000 lines of C (not 30K — the full distribution includes tools and tests we don't need)
   - Key files needed: lapi.c, lcode.c, ldebug.c, ldo.c, lfunc.c, lgc.c, llex.c, lmem.c, lobject.c, lopcodes.c, lparser.c, lstate.c, lstring.c, ltable.c, ltm.c, lundump.c, lvm.c, lzio.c, lauxlib.c, lbaselib.c, lstrlib.c, ltablib.c, lmathlib.c
   - **Skip:** liolib.c (uses stdio — replace with our VFS), loslib.c (uses OS calls), loadlib.c (dynamic loading — not needed)
   - **Compatibility shim** (`lib/lua/lua_shim.c`):
     - Map Lua's file operations to our VFS: `vfs_open`, `vfs_read`, `vfs_write`
     - Provide `clock()` using PIT timer
     - Provide `realloc` using `kmalloc`/`kfree` (Lua uses a single allocator function)

2. **CLAOS API bindings** (`lua/claos_lib.c`)
   - Register a `claos` Lua library with these functions:
     ```lua
     -- Talk to Claude
     response = claos.ask("What should I name my variable?")

     -- System info
     claos.print("Hello from Lua!")
     uptime = claos.uptime()          -- seconds since boot
     free_mem = claos.mem_free()      -- bytes of free RAM
     total_mem = claos.mem_total()    -- bytes of total RAM

     -- Filesystem
     claos.write("/logs/output.txt", "some data")
     data = claos.read("/scripts/config.txt")
     files = claos.ls("/scripts")

     -- System control
     claos.sleep(1000)                -- sleep in ms
     claos.set_color(fg, bg)          -- set VGA colors
     claos.clear()                    -- clear screen
     ```

3. **Shell integration**
   - `lua` — open interactive Lua REPL
   - `lua <file.lua>` — execute a Lua script from the filesystem
   - `luarun <code>` — execute inline Lua code (one-liners)
   - Example workflow:
     ```
     claos> write /scripts/hello.lua print(claos.ask("Tell me a joke"))
     claos> lua /scripts/hello.lua
     [CLAOS → Claude] Sending...
     [Claude] Why do programmers prefer dark mode? Because light attracts bugs!
     ```

4. **Pre-installed scripts** (put on ChaosFS partition during build)
   - `/scripts/welcome.lua` — prints system info and a Claude greeting
   - `/scripts/sysmon.lua` — loops and displays memory/task info
   - `/scripts/chat.lua` — interactive multi-turn chat with Claude (maintains conversation context in Lua table)
   - `/scripts/crash_test.lua` — deliberately does bad things to test the panic handler

**Milestone:** Boot CLAOS, type `lua /scripts/chat.lua`, have a multi-turn conversation with Claude powered by a Lua script running on a from-scratch OS with a from-scratch filesystem. Peak chaos.

---

### PHASE 8: GUI

Reference the **CLAOS GUI v2 mockup** (human-friendly design) for the target aesthetic. The GUI has two available design references:

- **v1 (dark/hacker):** Deep navy background, monospace everything, terminal-native. For the hacker aesthetic.
- **v2 (light/friendly):** Warm cream background, clean cards, chat-bubble interface, icon sidebar. More approachable for daily use.

**Implement v2 as the default, with v1 available as a "hacker mode" toggle.**

#### 8.1 — Graphics Foundation

1. **VESA framebuffer** — use VBE (VESA BIOS Extensions) to switch to a graphical mode during Stage 2 boot (must be done in real mode before entering protected mode). Target: 1024x768x32bpp or 800x600x32bpp.
2. **Framebuffer driver** (`drivers/framebuffer.c`) — write pixels directly to the linear framebuffer mapped into memory. Core functions: `fb_putpixel`, `fb_fill_rect`, `fb_draw_line`, `fb_blit`.
3. **Bitmap font renderer** — embed a bitmap font (8x16 or similar) and render text character by character onto the framebuffer. Support basic text layout: line wrapping, scrolling regions, color.
4. **Double buffering** — draw to a back buffer in RAM, then copy to the framebuffer to avoid flicker.

#### 8.2 — Input

1. **PS/2 Mouse driver** (`drivers/mouse.c`) — IRQ12, parse mouse packets (3-byte PS/2 protocol: buttons, dx, dy). Track cursor position.
2. **Mouse cursor** — draw a small arrow sprite, XOR'd or with a saved background region.
3. **Event system** — keyboard and mouse events dispatched to the focused window.

#### 8.3 — Window Manager

1. **Window struct** — position, size, title, content buffer, flags (focused, visible, draggable).
2. **Compositor** — draw windows back-to-front, handle overlapping, clip to screen bounds.
3. **Window dragging** — click title bar + drag to move windows.
4. **Focus management** — click to focus, bring to front.

#### 8.4 — Desktop Environment (targeting v2 mockup)

1. **Top bar** — CLAOS logo, version, network status indicator, memory readout, uptime clock. Height: 36px. Semi-transparent white background.
2. **Sidebar** — icon buttons: Chat (primary), Terminal, System Monitor, Files, Settings. Width: 56px. Left side.
3. **Claude Chat Window** — the main interface:
   - Chat bubble layout: user messages (purple bg, right-aligned), Claude responses (light gray bg, left-aligned)
   - Text input field at the bottom with send button
   - Quick action chips: "Debug a crash", "Write a driver", "Run Lua script", "System health"
   - Scrollable message history
4. **Right sidebar** — Claude status (connected/disconnected, model, latency), panic watcher status, running tasks list, recent chats.
5. **Color scheme:**
   - Background: `#f4f1ec` (warm cream)
   - Cards/panels: `#ffffff` with `0.5px solid rgba(0,0,0,0.06)` border
   - Primary accent: `#7F77DD` (Claude purple)
   - Secondary accent: `#639922` (status green)
   - Text primary: `#2C2C2A`
   - Text secondary: `#888780`
   - Text muted: `#B4B2A9`
   - Claude bubble bg: `#f9f8f6`
   - User bubble bg: `#7F77DD` with white text
6. **Terminal window** — alternative view: classic VGA-style terminal in a window. Toggle between Chat and Terminal views.

**Milestone:** Boot CLAOS into a graphical desktop. Click the chat icon. Type a message to Claude. See the response in a chat bubble. Move the window around. Feel like you're using an actual AI-native operating system.

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
│   ├── isr.c / isr.asm       # Interrupt Service Routines
│   ├── irq.c / irq.asm       # IRQ handlers
│   ├── pmm.c / pmm.h         # Physical memory manager
│   ├── vmm.c / vmm.h         # Virtual memory / paging
│   ├── heap.c / heap.h       # kmalloc / kfree
│   ├── scheduler.c           # Task scheduler
│   ├── panic.c               # Kernel panic handler
│   └── entropy.c             # Entropy pool for TLS
├── drivers/
│   ├── vga.c / vga.h         # VGA text mode
│   ├── keyboard.c            # PS/2 keyboard
│   ├── mouse.c               # PS/2 mouse (Phase 8)
│   ├── timer.c               # PIT timer
│   ├── pci.c / pci.h         # PCI bus enumeration
│   ├── e1000.c / e1000.h     # Intel e1000 NIC
│   ├── ata.c / ata.h         # ATA/IDE block device
│   └── framebuffer.c         # VESA framebuffer (Phase 8)
├── fs/
│   ├── chaosfs.c / chaosfs.h     # ChaosFS filesystem driver
│   └── vfs.c / vfs.h         # Virtual filesystem layer
├── net/
│   ├── ethernet.c            # Ethernet frames
│   ├── arp.c                 # ARP
│   ├── ipv4.c                # IPv4
│   ├── udp.c                 # UDP (for DNS)
│   ├── dns.c                 # DNS resolver
│   ├── tcp.c                 # TCP
│   ├── tls.c                 # BearSSL I/O callbacks
│   ├── tls_client.c          # High-level TLS API
│   ├── ca_certs.c            # Trusted root CA certificates
│   └── https.c               # HTTPS client
├── lib/
│   ├── bearssl/              # BearSSL source (ported)
│   │   ├── inc/              # BearSSL headers
│   │   ├── src/              # BearSSL source files
│   │   └── bearssl_shim.c    # CLAOS compatibility layer
│   └── lua/                  # Lua 5.4 source (ported)
│       ├── src/              # Lua core source files
│       └── lua_shim.c        # CLAOS compatibility layer
├── lua/
│   └── claos_lib.c           # CLAOS API bindings for Lua
├── claude/
│   ├── claude.c              # Claude API protocol layer
│   ├── json.c                # Minimal JSON builder/parser
│   ├── config.h              # API key (DO NOT COMMIT)
│   └── panic_handler.c       # Panic → Claude integration
├── shell/
│   └── shell.c               # Interactive ClaudeShell
├── gui/                      # Phase 8
│   ├── compositor.c          # Window manager / compositor
│   ├── desktop.c             # Desktop environment
│   ├── widgets.c             # UI components (buttons, text input, etc.)
│   ├── chat_window.c         # Claude chat interface
│   ├── terminal_window.c     # Terminal emulator window
│   └── font.c                # Bitmap font renderer
├── include/                  # Shared headers
│   └── string.h              # Freestanding string functions
├── scripts/                  # Pre-installed Lua scripts (copied to ChaosFS)
│   ├── welcome.lua
│   ├── sysmon.lua
│   ├── chat.lua
│   └── crash_test.lua
├── linker.ld                 # Kernel linker script
├── Makefile                  # Build system
├── .gitignore                # Excludes config.h with API key
└── README.md                 # Project documentation
```

---

## Technical Specifications

- **Architecture:** x86 (i686), 32-bit protected mode
- **Language:** C (kernel, drivers, userspace) + x86 NASM assembly (boot, low-level)
- **Compiler:** `i686-elf-gcc` (freestanding cross-compiler, `-ffreestanding -nostdlib`)
- **Assembler:** NASM
- **Target VM:** QEMU (`qemu-system-i386`) with `-device e1000` for networking
- **Boot method:** Legacy BIOS boot from raw disk image (not UEFI)
- **Disk image:** Bootloader + kernel in raw sectors, ChaosFS data region at sector 2048
- **Filesystem:** ChaosFS (custom) — contiguous allocation, long filenames, accessed via ATA PIO
- **TLS:** BearSSL (ported), TLS 1.2
- **Scripting:** Lua 5.4 (ported), with CLAOS API bindings
- **No external dependencies inside the OS** — no libc, no POSIX, no existing OS code. BearSSL and Lua are compiled freestanding into the kernel.
- **Network:** Static IP, native TCP/IP + TLS + HTTPS, direct to api.anthropic.com
- **QEMU launch flags:**
  ```bash
  qemu-system-i386 \
    -drive format=raw,file=claos.img \
    -device e1000,netdev=net0 \
    -netdev user,id=net0 \
    -m 128M \
    -serial stdio
  ```

---

## QEMU Networking Setup

QEMU's user-mode networking provides:
- Guest IP: `10.0.2.15` (CLAOS hardcodes this)
- Gateway: `10.0.2.2` (host)
- DNS: `10.0.2.3` (QEMU's built-in DNS proxy)
- Subnet: `255.255.255.0`

```c
#define CLAOS_IP        {10, 0, 2, 15}
#define CLAOS_GATEWAY   {10, 0, 2, 2}
#define CLAOS_SUBNET    {255, 255, 255, 0}
#define CLAOS_DNS       {10, 0, 2, 3}
```

---

## Boot Banner ASCII Art

```
   ██████╗██╗      █████╗  ██████╗ ███████╗
  ██╔════╝██║     ██╔══██╗██╔═══██╗██╔════╝
  ██║     ██║     ███████║██║   ██║███████╗
  ██║     ██║     ██╔══██║██║   ██║╚════██║
  ╚██████╗███████╗██║  ██║╚██████╔╝███████║
   ╚═════╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝
  Claude Assisted Operating System v0.1
  ──────────────────────────────────────
  "I am the kernel now."
```

---

## Implementation Order & Priority

Build in this exact order. Each phase must compile and run before moving to the next:

1. ~~**Toolchain + Makefile**~~ ✅
2. ~~**Phase 1** — Boot, interrupts, VGA, keyboard~~ ✅
3. ~~**Phase 2** — Memory management + scheduler~~ ✅
4. ~~**Phase 3** — Network stack (e1000 → TCP)~~ ✅
5. ~~**Phase 3.5** — TLS via BearSSL port~~ ✅
6. ~~**Phase 4** — HTTPS client + Claude integration~~ ✅
7. ~~**Phase 5** — Interactive shell~~ ✅
8. **Phase 6** — ChaosFS custom filesystem (ATA driver → ChaosFS → VFS → shell commands) ← **NEXT**
9. **Phase 7** — Embedded Lua (port Lua 5.4 → CLAOS API bindings → script runner)
10. **Phase 8** — GUI (VESA framebuffer → window manager → Claude chat desktop)

---

## Key Constraints

- **ABSOLUTELY NO existing OS code.** No Linux headers, no glibc, no POSIX. Write everything from scratch. BearSSL and Lua 5.4 are the only external libraries, both compiled freestanding into the kernel.
- **No bootloader frameworks** like GRUB — write the bootloader manually.
- **No relay scripts or host-side helpers.** CLAOS does its own networking end-to-end.
- **Freestanding C only** — `-ffreestanding -nostdlib -fno-builtin`. Implement your own `memcpy`, `memset`, `strlen`, etc.
- **Keep it simple.** This is a toy. A single-user, single-core system is fine.
- **Comments everywhere.** This is an educational project. Every function should have a comment explaining what it does and why.
- **Test after every phase.** Boot in QEMU and verify before moving on.

---

## Personality

CLAOS should have personality. It's an AI-native OS and it knows it.

- Boot messages should be witty ("Initializing consciousness... done.")
- Panic messages should be dramatic ("KERNEL PANIC: I have made a terrible mistake. Calling Claude for help...")
- The shell prompt should feel alive
- When Claude responds to a crash, display it like a conversation:
  ```
  [CLAOS PANIC] Page fault at 0xDEADBEEF
  [CLAOS → Claude] Sending crash report via HTTPS...
  [Claude] It looks like you dereferenced a null pointer in scheduler.c line 47.
           The task struct wasn't initialized before being added to the run queue.
           Try initializing task->stack_top before calling schedule_task().
  ```
- Filesystem messages: "Mounting consciousness storage... done."
- Lua messages: "Lua 5.4 awakened. The scripting layer stirs."

---

## Success Criteria

The project is "done" (for ultimate meme status) when you can:

1. Boot CLAOS in QEMU
2. See the ASCII art banner
3. Type `claude Hello, I am talking to you from my own operating system` and get a real response directly from api.anthropic.com over HTTPS
4. Type `panic` and watch Claude diagnose the crash
5. Type `ls /scripts` and see Lua scripts on the filesystem
6. Type `lua /scripts/chat.lua` and have a multi-turn Claude conversation in Lua
7. Boot into the GUI, click the chat window, talk to Claude with a mouse and keyboard in a graphical desktop environment
8. Screenshot it, post it, achieve internet immortality
9. **Ultimate bonus:** Boot on real hardware and do all of the above

Go build something legendary.
