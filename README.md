# CLAOS — Claude Assisted Operating System

```
   ####  ##        ##     ####    ####
  ##     ##       ####   ##  ##  ##
  ##     ##      ##  ##  ##  ##   ####
  ##     ##      ######  ##  ##      ##
   ####  ######  ##  ##   ####   ####

  "I am the kernel now."
```

**CLAOS** (pronounced "Chaos") is a toy x86 operating system written entirely from scratch with **zero dependency on any existing OS kernel**. No Linux, no Windows, no macOS, no GRUB, no libc — just raw metal and vibes.

CLAOS is an **AI-native OS** where Claude (Anthropic's AI) is integrated at the kernel level as a core system service. Claude can be prompted interactively from the OS shell, receives crash/panic reports automatically, and can send back diagnoses in real time.

> This is a meme project and educational toy — not a production OS. Built with Claude Code.

### Boot Screen
![CLAOS Boot Screen](Screenshots/Screenshot%202026-03-18%20161927.png)

### Kernel Panic
![CLAOS Kernel Panic](Screenshots/Screenshot%202026-03-18%20162359.png)

---

## Features

### Phase 1 — Boot & Kernel Foundation
- **Custom 2-stage bootloader** — MBR → Protected Mode, no GRUB
- **32-bit protected mode kernel** written in C and x86 assembly
- **VGA text mode driver** — 80x25 color text with scrolling
- **PS/2 keyboard driver** — US QWERTY with shift support and line editing
- **PIT timer** — 100 Hz system tick with uptime tracking
- **Full interrupt infrastructure** — GDT, IDT, PIC remapping, ISR/IRQ handlers
- **Kernel panic handler** — dramatic red screen of death with register dump, press any key to reboot

### Phase 2 — Memory Management & Scheduler
- **Physical memory manager** — bitmap-based page allocator (4KB pages), E820 memory map parsing
- **Virtual memory (paging)** — page directory/tables, full 4GB identity-mapped via PSE (4MB pages)
- **Kernel heap allocator** — first-fit free list with block splitting and coalescing (`kmalloc`/`kfree`)
- **Preemptive round-robin scheduler** — context switching via timer IRQ, task sleep/yield, up to 16 concurrent tasks
- **Background tasks** — spinning status indicator and live uptime counter running alongside the shell

### Phase 3 — Network Stack
- **PCI bus enumeration** — scan for devices by vendor/device ID, read BARs
- **Intel e1000 NIC driver** — MMIO register access, DMA TX/RX descriptor rings, MAC address, link status
- **Ethernet layer** — frame construction/parsing, EtherType dispatch
- **ARP** — request/reply, static cache, gateway MAC resolution at boot
- **IPv4** — packet construction/parsing, header checksum, gateway routing
- **UDP** — send/receive with port binding (used by DNS)
- **DNS resolver** — A record queries over UDP, hostname-to-IP resolution
- **TCP** — full state machine (SYN/SYN-ACK/ACK/FIN), sequence tracking, receive buffering

### Phase 3.5 — TLS via BearSSL
- **BearSSL ported** — 294 source files compiled for freestanding i686, x86 intrinsics disabled
- **Entropy pool** — RDTSC + timer ticks + xorshift PRNG for TLS key generation
- **TLS client wrapper** — connects BearSSL's SSL engine to our TCP stack via I/O callbacks
- **Certificate handling** — custom X.509 engine extracts server public keys from leaf certificates
- **Successful TLS 1.2 handshake** with `api.anthropic.com:443` — no relay, no host scripts
- **Bootloader rewritten** — loads 292KB kernel to 1MB using INT 13h extended reads + protected mode copy

### Phase 4 — HTTPS Client & Claude Integration (Current)
- **HTTPS client** — HTTP/1.1 over TLS with chunked transfer encoding support
- **Claude protocol layer** — formats Anthropic Messages API requests, parses JSON responses
- **Minimal JSON parser** — builds request bodies, extracts `content[0].text` from responses
- **Runtime configuration** — `config` command to enter API key and model interactively (no recompilation needed)
- **Panic-to-Claude** — kernel panics automatically send crash reports to Claude for AI diagnosis
- **`claude <message>`** — talk to Claude directly from the CLAOS shell over native HTTPS
- **Commands**: `help`, `clear`, `uptime`, `sysinfo`, `tasks`, `net`, `dns`, `tls`, `config`, `claude <msg>`, `panic`, `reboot`

## Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| 1 | **Done** | Boot & Kernel Foundation — boot, interrupts, VGA, keyboard, timer |
| 2 | **Done** | Memory Management & Scheduler — PMM, paging, heap, multitasking |
| 3 | **Done** | Network Stack — PCI, e1000 NIC, Ethernet, ARP, IPv4, UDP, DNS, TCP |
| 3.5 | **Done** | TLS via BearSSL — native TLS 1.2 handshake, no relay needed |
| 4 | **Done** | HTTPS Client & Claude Integration — talk directly to api.anthropic.com |
| 5 | Planned | Interactive Shell — full ClaudeShell with AI-powered commands |
| 6 | Stretch | GUI — framebuffer graphics, windows, Claude chat window |

---

## Building

### Prerequisites

- **i686-elf-gcc** — freestanding cross-compiler targeting 32-bit x86 ELF
- **NASM** — x86 assembler
- **QEMU** — `qemu-system-i386` for testing
- **GNU Make**

On Windows with MSYS2:
```bash
# Install MSYS2 from https://www.msys2.org/ then:
pacman -S nasm make

# Download i686-elf cross-compiler from:
# https://github.com/lordmilko/i686-elf-tools/releases
# Extract to C:\i686-elf-tools\

# Install QEMU:
pacman -S mingw-w64-x86_64-qemu
```

See `tools/setup_toolchain.sh` for automated setup on Linux/macOS.

### Build & Run

```bash
make          # Build claos.img
make run      # Build and launch in QEMU
make clean    # Remove build artifacts
```

On Windows, you can also double-click **`run.bat`** to launch CLAOS in QEMU.

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

### Host-Side Relay

CLAOS talks to Claude via a small Python relay (`tools/relay.py`) running on your LAN. CLAOS sends plain HTTP; the relay forwards to the Anthropic API over HTTPS.

```
CLAOS (VM)                          Host / LAN
┌──────────────┐                   ┌─────────────────────┐
│ HTTP POST ────┼──── LAN ────────▶│ relay.py             │
│ (plain)       │                  │ HTTP → HTTPS         │──▶ api.anthropic.com
└──────────────┘                   └─────────────────────┘
```

---

## Project Structure

```
claos/
├── boot/
│   ├── stage1.asm          # MBR bootloader (512 bytes)
│   └── stage2.asm          # GDT, A20, protected mode switch
├── kernel/
│   ├── entry.asm           # Kernel entry stub
│   ├── main.c              # Kernel entry point
│   ├── gdt.c / gdt.h       # Global Descriptor Table
│   ├── idt.c / idt.h       # Interrupt Descriptor Table
│   ├── isr.asm             # CPU exception stubs
│   ├── irq.asm             # Hardware IRQ stubs
│   ├── gdt_flush.asm       # GDT/IDT register loading
│   ├── panic.c / panic.h   # Kernel panic handler
│   ├── pmm.c / pmm.h       # Physical memory manager (bitmap)
│   ├── vmm.c / vmm.h       # Virtual memory / paging
│   ├── heap.c / heap.h     # Kernel heap (kmalloc/kfree)
│   ├── scheduler.c / .h    # Preemptive round-robin scheduler
│   ├── scheduler_asm.asm   # Context switch routine
│   └── string.c            # memcpy, memset, strlen, etc.
├── drivers/
│   ├── vga.c / vga.h       # VGA text mode (80x25)
│   ├── keyboard.c / .h     # PS/2 keyboard
│   ├── timer.c / timer.h   # PIT timer
│   ├── pci.c / pci.h       # PCI bus enumeration
│   └── e1000.c / e1000.h   # Intel e1000 NIC driver
├── net/
│   ├── net.h               # Network config & byte-order helpers
│   ├── ethernet.c / .h     # Ethernet frames
│   ├── arp.c / arp.h       # ARP (address resolution)
│   ├── ipv4.c / ipv4.h     # IPv4 packets
│   ├── udp.c / udp.h       # UDP (for DNS)
│   ├── dns.c / dns.h       # DNS resolver
│   ├── tcp.c / tcp.h       # TCP (connection-oriented transport)
│   ├── tls_client.c / .h   # TLS client (BearSSL wrapper)
│   ├── ca_certs.c          # Trusted root CA certificates
│   └── https.c / https.h   # HTTPS client (HTTP/1.1 over TLS)
├── claude/
│   ├── claude.c / claude.h # Claude API protocol layer
│   ├── json.c / json.h     # Minimal JSON builder/parser
│   ├── panic_handler.c / .h # Panic → Claude crash diagnosis
│   ├── config.h            # API key & settings (NOT committed)
│   └── config.h.example    # Template for config.h
├── lib/
│   └── bearssl/            # BearSSL TLS library (ported)
│       ├── inc/            # BearSSL public headers
│       ├── src/            # BearSSL source (~294 files)
│       └── bearssl_shim.c  # CLAOS compatibility layer
├── include/
│   ├── types.h             # Fixed-width types
│   ├── string.h            # String function declarations
│   └── io.h                # Port I/O + serial debug
├── tools/
│   ├── relay.py            # Host-side HTTP→HTTPS relay
│   └── setup_toolchain.sh  # Toolchain installer
├── Docs/
│   └── CLAOS-build-prompt.md  # Full build specification
├── linker.ld               # Kernel linker script
├── Makefile                # Build system
├── run.bat                 # Windows launcher
└── README.md
```

---

## Key Constraints

- **No existing OS code** — no Linux headers, no glibc, no POSIX
- **No bootloader frameworks** — no GRUB, hand-written bootloader
- **Freestanding C** — `-ffreestanding -nostdlib -fno-builtin`
- **Everything from scratch** — custom `memcpy`, `strlen`, interrupt handlers, the works

---

## License

This is a meme project. Do whatever you want with it. Have fun.

---

*Built with [Claude Code](https://claude.ai) — because every OS deserves an AI copilot.*
