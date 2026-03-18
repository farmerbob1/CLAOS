# CLAOS — Claude Assisted Operating System
## Master Build Prompt for Claude Code

---

## Project Overview

Build **CLAOS** (pronounced "Chaos") — a toy x86 operating system written entirely from scratch with NO dependency on Linux, Windows, macOS, or any existing OS kernel. CLAOS is an "AI-native OS" where Claude (Anthropic's AI) is integrated at the kernel level as a core system service. Claude can be prompted interactively from the OS shell, receives crash/panic reports automatically, and can send back patches or commands.

This is a meme project and educational toy — not a production OS. Prioritize "cool and functional demo" over robustness.

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│                  CLAOS Stack                      │
├──────────────────────────────────────────────────┤
│  Layer 5: ClaudeShell (interactive AI shell)      │
│           + Lua scripting environment             │
├──────────────────────────────────────────────────┤
│  Layer 4: Claude Protocol Layer                   │
│           JSON request/response over TCP          │
├──────────────────────────────────────────────────┤
│  Layer 3: Network Stack                           │
│           TCP/IP → HTTP client                    │
│           (talks to LAN relay for HTTPS)          │
├──────────────────────────────────────────────────┤
│  Layer 2: Drivers                                 │
│           VGA text/framebuffer, PS/2 keyboard,    │
│           PIT timer, e1000 NIC                    │
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

### Host-Side Relay (runs on LAN, not part of the OS)

A small Python script that accepts plain HTTP from CLAOS on the local network and proxies requests to the Anthropic API over HTTPS. This means CLAOS only needs TCP/IP + HTTP (no TLS), while still talking to the real Claude API. This relay could run on the host machine, a Raspberry Pi, or anything on the same network.

```
CLAOS (bare metal / VM)                    Host / LAN device
┌─────────────────────┐                   ┌─────────────────────────┐
│ HTTP GET/POST ───────┼──── LAN ────────▶│ Python relay            │
│ (plain, no TLS)      │                  │ HTTP in → HTTPS out     │──▶ api.anthropic.com
└─────────────────────┘                   │ (+ inserts API key)     │
                                          └─────────────────────────┘
```

---

## Build Phases

### PHASE 1: Boot & Kernel Foundation
**Goal:** Boot in QEMU, enter protected mode, print to screen, handle interrupts.

1. **Cross-compiler toolchain**
   - Build or install `i686-elf-gcc` and `i686-elf-ld` (freestanding cross-compiler targeting 32-bit x86 ELF)
   - Install NASM for assembly
   - Install QEMU (`qemu-system-i386`) for testing
   - Create a Makefile that builds the entire project and produces a bootable disk image

2. **Stage 1 Bootloader** (`boot/stage1.asm`)
   - 512-byte MBR bootloader in x86 real mode assembly
   - Loads Stage 2 from disk using BIOS INT 13h
   - Ends with `0xAA55` boot signature

3. **Stage 2 Bootloader** (`boot/stage2.asm`)
   - Sets up GDT (Global Descriptor Table) with code and data segments
   - Enables A20 line
   - Switches CPU to 32-bit protected mode
   - Jumps to the C kernel entry point

4. **Kernel entry** (`kernel/main.c`)
   - Clear screen, print CLAOS boot banner with ASCII art
   - Initialize GDT and IDT (Interrupt Descriptor Table)
   - Set up ISRs (Interrupt Service Routines) for CPU exceptions (divide by zero, page fault, GPF, etc.)
   - Set up IRQ handlers (remap PIC, handle timer and keyboard)
   - Print "CLAOS v0.1 — Claude Assisted Operating System" to screen

5. **VGA text mode driver** (`drivers/vga.c`)
   - Write characters to `0xB8000` VGA text buffer
   - Support: `print_char`, `print_string`, `print_hex`, `clear_screen`, `set_color`
   - Scrolling support

6. **PS/2 Keyboard driver** (`drivers/keyboard.c`)
   - IRQ1 handler, scancode-to-ASCII translation
   - Basic line-buffered input

7. **PIT Timer** (`drivers/timer.c`)
   - IRQ0 handler, configurable tick rate
   - System uptime counter

**Milestone:** CLAOS boots in QEMU, shows a banner, and you can type on a keyboard with characters appearing on screen.

---

### PHASE 2: Memory Management & Scheduler

1. **Physical memory manager** (`kernel/pmm.c`)
   - Bitmap-based page allocator (4KB pages)
   - Parse memory map from bootloader (use BIOS INT 15h, E820 in Stage 2)

2. **Virtual memory / Paging** (`kernel/vmm.c`)
   - Set up page directory and page tables
   - Identity-map kernel space
   - Enable paging via CR0

3. **Heap allocator** (`kernel/heap.c`)
   - Simple `kmalloc` / `kfree` for kernel dynamic allocation
   - Can be a basic first-fit or buddy allocator

4. **Basic scheduler** (`kernel/scheduler.c`)
   - Cooperative or simple preemptive round-robin scheduler
   - Task struct with saved register state
   - Context switching via timer interrupt
   - At minimum: support running 2-3 kernel tasks concurrently

**Milestone:** Multiple kernel tasks running (e.g., one printing a counter, another blinking a cursor).

---

### PHASE 3: Network Stack

This is the critical path to reaching Claude.

1. **e1000 NIC driver** (`drivers/e1000.c`)
   - Intel 82540EM (default QEMU NIC with `-device e1000`)
   - PCI bus enumeration to find the device
   - Initialize TX/RX descriptor rings
   - Send and receive Ethernet frames
   - Handle NIC interrupts

2. **Ethernet** (`net/ethernet.c`)
   - Frame construction and parsing
   - EtherType dispatch

3. **ARP** (`net/arp.c`)
   - ARP request/reply for MAC address resolution
   - Simple ARP cache table

4. **IPv4** (`net/ipv4.c`)
   - IP packet construction/parsing
   - Static IP configuration (no DHCP needed — hardcode IP for now)

5. **TCP** (`net/tcp.c`)
   - TCP state machine (SYN, SYN-ACK, ACK, data transfer, FIN)
   - Sequence numbers, basic retransmission
   - This doesn't need to be RFC-perfect. "Good enough to hold a connection" is fine.

6. **HTTP client** (`net/http.c`)
   - Construct HTTP/1.1 POST requests with JSON body
   - Parse HTTP responses (at minimum: status code, Content-Length, body)
   - This talks to the LAN relay, not directly to Anthropic

**Milestone:** CLAOS can send an HTTP request to the relay and receive a response. Test with a simple echo endpoint first.

---

### PHASE 4: Claude Integration

1. **Claude Protocol Layer** (`claude/claude.c`)
   - Format Anthropic Messages API requests as JSON:
     ```json
     {
       "model": "claude-sonnet-4-20250514",
       "max_tokens": 1024,
       "messages": [{"role": "user", "content": "..."}]
     }
     ```
   - Send via HTTP to the LAN relay
   - Parse JSON response (minimal JSON parser — just extract `content[0].text`)
   - Provide kernel API: `claude_ask(const char* prompt, char* response_buf, int buf_size)`

2. **Panic-to-Claude Handler** (`claude/panic_handler.c`)
   - Hook into the kernel's exception handlers
   - On panic/fault: capture fault type, error code, register dump (EAX-ESP, EIP, EFLAGS), stack trace (walk EBP chain), and last N lines of kernel log
   - Format all of this into a prompt and send to Claude
   - Display Claude's response on screen
   - If Claude sends back a fix suggestion, display it (live patching is stretch goal)

3. **LAN Relay** (`tools/relay.py`) — runs on host, NOT inside CLAOS
   - Python script using `flask` or `http.server`
   - Listens on `http://0.0.0.0:8080`
   - Receives POST requests from CLAOS
   - Forwards to `https://api.anthropic.com/v1/messages` with API key from env var
   - Returns response to CLAOS
   - ~30-50 lines of code

**Milestone:** From the CLAOS kernel, you can call `claude_ask("Hello from CLAOS!", buf, sizeof(buf))` and get a real response from Claude displayed on screen.

---

### PHASE 5: Interactive Shell

1. **ClaudeShell** (`shell/shell.c`)
   - Command-line interface displayed after boot
   - Prompt: `claos> `
   - Built-in commands:
     - `claude <message>` — send a prompt to Claude and display the response
     - `sysinfo` — show memory usage, uptime, task count
     - `clear` — clear screen
     - `panic` — deliberately trigger a kernel panic (to demo the panic→Claude flow)
     - `reboot` — triple-fault reboot
     - `help` — list commands
     - `tasks` — show running tasks
   - Any unrecognized command is sent to Claude as a prompt with context: "The user typed this command in CLAOS shell. Interpret it and respond helpfully."

2. **Embedded Lua** (stretch goal for Phase 5)
   - Port Lua 5.4 interpreter (it's ANSI C, very portable)
   - Expose kernel APIs as Lua functions:
     - `claos.ask(prompt)` — call Claude
     - `claos.print(text)` — write to screen
     - `claos.uptime()` — get system uptime
     - `claos.mem_free()` — get free memory
   - `run <script.lua>` shell command to execute Lua scripts
   - This becomes the "ClaudeScript" layer

**Milestone:** Full interactive experience — boot CLAOS, type `claude What is the meaning of life?` and get a response. Type `panic` and watch Claude analyze the crash in real time.

---

### PHASE 6: GUI (Stretch Goal)

1. **VESA framebuffer** — switch to graphical mode via VBE during boot
2. **Basic drawing primitives** — pixels, rectangles, lines, bitmap font rendering
3. **Mouse driver** — PS/2 mouse via IRQ12
4. **Window compositor** — simple flat windows, title bars, dragging
5. **Claude Chat Window** — a GUI window with scrolling text where you chat with Claude
6. **CLAOS Desktop** — wallpaper with the CLAOS logo, a taskbar, and the Claude chat window

---

## Project Structure

```
claos/
├── boot/
│   ├── stage1.asm          # MBR bootloader
│   └── stage2.asm          # Protected mode setup, loads kernel
├── kernel/
│   ├── main.c              # Kernel entry point
│   ├── gdt.c / gdt.h       # Global Descriptor Table
│   ├── idt.c / idt.h       # Interrupt Descriptor Table
│   ├── isr.c / isr.asm     # Interrupt Service Routines
│   ├── irq.c / irq.asm     # IRQ handlers
│   ├── pmm.c / pmm.h       # Physical memory manager
│   ├── vmm.c / vmm.h       # Virtual memory / paging
│   ├── heap.c / heap.h     # kmalloc / kfree
│   ├── scheduler.c         # Task scheduler
│   └── panic.c             # Kernel panic handler
├── drivers/
│   ├── vga.c / vga.h       # VGA text mode
│   ├── keyboard.c          # PS/2 keyboard
│   ├── timer.c             # PIT timer
│   └── e1000.c / e1000.h   # Intel e1000 NIC
├── net/
│   ├── ethernet.c          # Ethernet frames
│   ├── arp.c               # ARP
│   ├── ipv4.c              # IPv4
│   ├── tcp.c               # TCP
│   └── http.c              # HTTP client
├── claude/
│   ├── claude.c            # Claude API protocol layer
│   ├── json.c              # Minimal JSON builder/parser
│   └── panic_handler.c     # Panic → Claude integration
├── shell/
│   └── shell.c             # Interactive ClaudeShell
├── tools/
│   └── relay.py            # Host-side HTTP→HTTPS relay
├── include/                # Shared headers
├── linker.ld               # Kernel linker script
├── Makefile                # Build system
└── README.md               # Project documentation
```

---

## Technical Specifications

- **Architecture:** x86 (i686), 32-bit protected mode
- **Language:** C (kernel, drivers, userspace) + x86 NASM assembly (boot, low-level)
- **Compiler:** `i686-elf-gcc` (freestanding cross-compiler, `-ffreestanding -nostdlib`)
- **Assembler:** NASM
- **Target VM:** QEMU (`qemu-system-i386`) with `-device e1000` for networking
- **Boot method:** Legacy BIOS boot from raw disk image (not UEFI)
- **Disk image:** Flat binary, dd'd into a raw `.img` file
- **No external dependencies inside the OS** — no libc, no POSIX, no existing OS code. Everything from scratch.
- **Network:** Static IP, plain HTTP to LAN relay, relay handles HTTPS to Anthropic API
- **QEMU launch flags:**
  ```bash
  qemu-system-i386 \
    -drive format=raw,file=claos.img \
    -device e1000,netdev=net0 \
    -netdev user,id=net0,hostfwd=tcp::8080-:80 \
    -m 128M \
    -serial stdio
  ```

---

## QEMU Networking Setup

For CLAOS to reach the relay:
- QEMU's default user-mode networking gives the guest IP `10.0.2.15`
- The gateway (host) is `10.0.2.2`
- CLAOS should hardcode: IP=`10.0.2.15`, Gateway=`10.0.2.2`, Subnet=`255.255.255.0`
- The relay runs on the host at port `8080`
- CLAOS sends HTTP requests to `10.0.2.2:8080`

---

## Boot Banner ASCII Art

Display this on boot (or create something similar):

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

1. **Toolchain + Makefile** (get the cross-compiler working)
2. **Phase 1** — Boot to a blinking cursor with keyboard input
3. **Phase 2** — Memory + scheduler
4. **Phase 3** — Network stack (hardest phase, take your time)
5. **Phase 4** — Claude integration
6. **Phase 5** — Shell
7. **Phase 6** — GUI (if we get here, we've already won)

---

## Key Constraints

- **ABSOLUTELY NO existing OS code.** No Linux headers, no glibc, no POSIX. Write everything from scratch.
- **No bootloader frameworks** like GRUB — write the bootloader manually.
- **Freestanding C only** — `-ffreestanding -nostdlib -fno-builtin`. Implement your own `memcpy`, `memset`, `strlen`, etc.
- **Keep it simple.** This is a toy. A single-user, single-core, non-preemptive (or simply preemptive) system is fine.
- **Comments everywhere.** This is an educational project. Every function should have a comment explaining what it does and why.
- **Test after every phase.** Boot in QEMU and verify before moving on.

---

## The Relay Script (tools/relay.py)

This is the simplest component. Here's roughly what it should do:

```python
"""
CLAOS LAN Relay — Bridges HTTP from CLAOS to Anthropic's HTTPS API.
Run on any machine on the same network as the CLAOS machine/VM.

Usage:
  export ANTHROPIC_API_KEY=sk-ant-...
  python relay.py
"""
# - Listen on 0.0.0.0:8080 for POST /v1/messages
# - Forward the request body to https://api.anthropic.com/v1/messages
# - Add headers: x-api-key, anthropic-version, content-type
# - Return the API response back to CLAOS
# - That's it. That's the whole script.
```

---

## Personality

CLAOS should have personality. It's an AI-native OS and it knows it.

- Boot messages should be witty ("Initializing consciousness... done.")
- Panic messages should be dramatic ("KERNEL PANIC: I have made a terrible mistake. Calling Claude for help...")
- The shell prompt should feel alive
- When Claude responds to a crash, display it like a conversation:
  ```
  [CLAOS PANIC] Page fault at 0xDEADBEEF
  [CLAOS → Claude] Sending crash report...
  [Claude] It looks like you dereferenced a null pointer in scheduler.c line 47.
           The task struct wasn't initialized before being added to the run queue.
           Try initializing task->stack_top before calling schedule_task().
  ```

---

## Success Criteria

The project is "done" (for meme purposes) when you can:

1. Boot CLAOS in QEMU
2. See the ASCII art banner
3. Type `claude Hello, I am talking to you from my own operating system` and get a real response
4. Type `panic` and watch Claude diagnose the crash
5. Screenshot it, post it, collect internet points

Go build something legendary.
