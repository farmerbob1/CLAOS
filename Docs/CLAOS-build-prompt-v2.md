# CLAOS — Claude Assisted Operating System

## Master Build Prompt for Claude Code (v2 — Path A: Native HTTPS)

\---

## Project Overview

Build **CLAOS** (pronounced "Chaos") — a toy x86 operating system written entirely from scratch with NO dependency on Linux, Windows, macOS, or any existing OS kernel. CLAOS is an "AI-native OS" where Claude (Anthropic's AI) is integrated at the kernel level as a core system service. Claude can be prompted interactively from the OS shell, receives crash/panic reports automatically, and can send back patches or commands.

This is a meme project and educational toy — not a production OS. Prioritize "cool and functional demo" over robustness.

**CRITICAL DESIGN DECISION: CLAOS connects to the Anthropic API directly via native HTTPS. No relay. No host-side scripts. The OS handles TCP/IP, TLS, and HTTPS entirely on its own using a ported BearSSL library. This means CLAOS can run on real bare-metal hardware with just an internet connection.**

\---

## Architecture

```
┌──────────────────────────────────────────────────┐
│                  CLAOS Stack                      │
├──────────────────────────────────────────────────┤
│  Layer 6: ClaudeShell (interactive AI shell)      │
│           + Lua scripting environment             │
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

This means CLAOS works on:

* QEMU with default user-mode networking (gateway 10.0.2.2 → host → internet)
* Real hardware with any e1000-compatible NIC and internet access
* No external helper scripts, no relay, nothing running on the host

\---

## Build Phases

### PHASE 1: Boot \& Kernel Foundation ✅ (COMPLETED)

**Goal:** Boot in QEMU, enter protected mode, print to screen, handle interrupts.

1. Cross-compiler toolchain (i686-elf-gcc, NASM, QEMU)
2. Stage 1 Bootloader (MBR, loads Stage 2)
3. Stage 2 Bootloader (GDT, A20, protected mode, jump to C kernel)
4. Kernel entry (boot banner, GDT, IDT, ISRs, IRQs)
5. VGA text mode driver
6. PS/2 Keyboard driver
7. PIT Timer

**Status: DONE — boots in QEMU, shows banner, keyboard works, panic screen works.**

\---

### PHASE 2: Memory Management \& Scheduler ✅ (COMPLETED)

1. **Physical memory manager** (`kernel/pmm.c`)

   * Bitmap-based page allocator (4KB pages)
   * Parse memory map from bootloader (BIOS INT 15h, E820 in Stage 2)
2. **Virtual memory / Paging** (`kernel/vmm.c`)

   * Set up page directory and page tables
   * Identity-map kernel space
   * Enable paging via CR0
3. **Heap allocator** (`kernel/heap.c`)

   * Simple `kmalloc` / `kfree` for kernel dynamic allocation
   * Can be a basic first-fit or buddy allocator
4. **Basic scheduler** (`kernel/scheduler.c`)

   * Cooperative or simple preemptive round-robin scheduler
   * Task struct with saved register state
   * Context switching via timer interrupt
   * At minimum: support running 2-3 kernel tasks concurrently

**Milestone:** Multiple kernel tasks running (e.g., one printing a counter, another blinking a cursor).

\---

### PHASE 3: Network Stack

The foundation for reaching Claude. Build and test each layer bottom-up.

1. **PCI bus enumeration** (`drivers/pci.c`)

   * Scan PCI bus to find devices by vendor/device ID
   * Read BARs (Base Address Registers) for MMIO addresses
   * Needed to locate the e1000 NIC
2. **e1000 NIC driver** (`drivers/e1000.c`)

   * Intel 82540EM (default QEMU NIC with `-device e1000`)
   * Map MMIO registers from PCI BAR0
   * Initialize TX/RX descriptor rings (16 descriptors each is fine)
   * Send and receive Ethernet frames
   * Handle NIC interrupts (or poll — polling is simpler)
3. **Ethernet** (`net/ethernet.c`)

   * Frame construction and parsing
   * EtherType dispatch (0x0800 = IPv4, 0x0806 = ARP)
4. **ARP** (`net/arp.c`)

   * ARP request/reply for MAC address resolution
   * Simple static ARP cache table (8-16 entries)
   * Need this to resolve the gateway MAC
5. **IPv4** (`net/ipv4.c`)

   * IP packet construction/parsing
   * Static IP configuration (hardcode for now)
   * Basic checksum calculation
6. **UDP** (`net/udp.c`)

   * Simple UDP send/receive
   * Needed for DNS lookups
7. **DNS resolver** (`net/dns.c`)

   * Minimal DNS client over UDP
   * Resolve `api.anthropic.com` to an IP address
   * Query format: standard A record lookup
   * Parse response to extract IP
   * Use QEMU's built-in DNS at 10.0.2.3, or Google's 8.8.8.8
   * Cache the result (only need to resolve once)
8. **TCP** (`net/tcp.c`)

   * TCP state machine (SYN → SYN-ACK → ACK → data → FIN)
   * Sequence/acknowledgment number tracking
   * Basic retransmission (simple timeout, no fancy algorithms needed)
   * Receive window management
   * This doesn't need to be RFC-perfect. "Good enough to hold a connection to Anthropic's servers" is the bar.

**Testing strategy for Phase 3:**

* Test e1000 + Ethernet: send raw frames, verify in QEMU packet capture (`-object filter-dump,id=f1,netdev=net0,file=dump.pcap`)
* Test ARP: resolve gateway MAC (10.0.2.2)
* Test IP + UDP: send a DNS query to 10.0.2.3, get a response
* Test TCP: connect to a simple TCP echo server on the host
* Each layer independently before stacking

**Milestone:** CLAOS can resolve `api.anthropic.com` via DNS and establish a TCP connection to the resulting IP on port 443.

\---

### PHASE 3.5: TLS via BearSSL Port

This is the critical bridge between "we have TCP" and "we can talk to Claude." We port BearSSL — a minimal, portable TLS library designed for embedded systems.

**Why BearSSL:**

* Written in pure C with zero dependencies (no malloc required, no libc needed)
* Designed for constrained systems — small code size (\~200KB), low RAM usage
* Supports TLS 1.2 with modern cipher suites
* No dynamic memory allocation by default (uses caller-provided buffers)
* Well-documented and has a clean API

**Steps:**

1. **Download and integrate BearSSL source** (`lib/bearssl/`)

   * Get the source from https://www.bearssl.org/ or the Git repo
   * Copy the `src/` and `inc/` directories into the CLAOS tree
   * BearSSL is \~100 source files but they're small
2. **Create CLAOS compatibility shim** (`lib/bearssl\\\_shim.c`)

   * BearSSL needs very little from the OS, but it does need:

     * `memcpy`, `memmove`, `memset`, `memcmp`, `strlen` — we already have these in our freestanding libc
     * A source of entropy/randomness for key generation — use a combination of PIT timer ticks, TSC (RDTSC instruction), and keyboard timing as a basic entropy pool
     * Buffer memory — provide static buffers (BearSSL needs \~25KB for I/O buffers)
   * It does NOT need: malloc, file I/O, threads, or anything complex
3. **Implement the BearSSL I/O callbacks** (`net/tls.c`)

   * BearSSL uses a "low-level" API where you provide read/write callbacks
   * These callbacks map directly to our TCP send/receive functions:

```c
     // BearSSL calls these to send/receive encrypted data
     static int tls\\\_sock\\\_read(void \\\*ctx, unsigned char \\\*buf, size\\\_t len);
     static int tls\\\_sock\\\_write(void \\\*ctx, const unsigned char \\\*buf, size\\\_t len);
     ```

   \* Wire these to `tcp\\\_send()` and `tcp\\\_recv()` from Phase 3
4. \*\*Certificate trust store\*\* (`net/ca\\\_certs.c`)

   \* BearSSL needs trusted root CA certificates to validate the server
   \* Anthropic's API uses certificates signed by standard CAs
   \* \*\*Pragmatic approach:\*\* Extract and hardcode the root CA certificates needed for `api.anthropic.com` (likely Amazon Root CA 1 or similar)
   \* Alternatively, embed a minimal set of widely-used root CAs (5-10 certificates, \\\~5KB)
   \* BearSSL includes tools to convert PEM certificates to C arrays
5. \*\*TLS client wrapper\*\* (`net/tls\\\_client.c`)

   \* High-level API for the rest of CLAOS:

```c
     tls\\\_connection\\\_t\\\* tls\\\_connect(const char\\\* hostname, uint16\\\_t port);
     int tls\\\_send(tls\\\_connection\\\_t\\\* conn, const void\\\* data, size\\\_t len);
     int tls\\\_recv(tls\\\_connection\\\_t\\\* conn, void\\\* buf, size\\\_t max\\\_len);
     void tls\\\_close(tls\\\_connection\\\_t\\\* conn);
     ```

   \* Internally: creates TCP connection → initializes BearSSL client context → performs TLS handshake → provides encrypted I/O
   \* SNI (Server Name Indication) — must send the hostname during handshake. BearSSL supports this natively.
6. \*\*Entropy pool\*\* (`kernel/entropy.c`)

   \* TLS needs randomness for key generation
   \* Collect entropy from: PIT timer interrupts (low bits of tick count), RDTSC instruction (CPU cycle counter), keyboard interrupt timing, e1000 interrupt timing
   \* Feed into a simple PRNG (or use BearSSL's built-in HMAC-DRBG)
   \* Doesn't need to be cryptographically perfect for a toy OS, but needs to be non-deterministic

\*\*Build considerations:\*\*

\* BearSSL compiles with `-ffreestanding` — it's designed for this
\* You may need to stub out a few things or provide `#define` overrides
\* Compile BearSSL objects separately and link them into the kernel
\* Add BearSSL's `inc/` to the include path

\*\*Testing:\*\*

\* First test: TLS handshake with `api.anthropic.com:443` — if the handshake completes, you're golden
\* Use QEMU packet capture to verify encrypted traffic is flowing
\* If handshake fails, check: certificate validation, SNI hostname, cipher suite support

\*\*Milestone:\*\* CLAOS can establish a TLS 1.2 connection to `api.anthropic.com:443` and send/receive encrypted data.

\\---

### PHASE 4: HTTPS Client \\\& Claude Integration

With TLS working, building HTTPS and the Claude protocol layer is straightforward.

1. \*\*HTTPS Client\*\* (`net/https.c`)

   \* HTTP/1.1 over TLS (use the `tls\\\_send`/`tls\\\_recv` from Phase 3.5)
   \* Construct HTTP requests:

```

&#x20;    POST /v1/messages HTTP/1.1
     Host: api.anthropic.com
     Content-Type: application/json
     x-api-key: <API\\\_KEY>
     anthropic-version: 2023-06-01
     Content-Length: <len>
     Connection: close

     <JSON body>
     ```


* Parse HTTP responses: status line, headers (especially Content-Length), body
* Handle chunked transfer encoding (Anthropic's API may use it)
* `Connection: close` simplifies things — one request per TCP+TLS connection
2. **Claude Protocol Layer** (`claude/claude.c`)

   * Format Anthropic Messages API requests as JSON:

```json
     {
       "model": "claude-sonnet-4-20250514",
       "max\\\_tokens": 1024,
       "messages": \\\[{"role": "user", "content": "..."}]
     }
     ```

   \* Send via HTTPS directly to `api.anthropic.com`
   \* Parse JSON response (minimal JSON parser — just extract `content\\\[0].text`)
   \* Provide kernel API: `claude\\\_ask(const char\\\* prompt, char\\\* response\\\_buf, int buf\\\_size)`
3. \*\*Minimal JSON parser\*\* (`claude/json.c`)

   \* Only needs to handle the specific structure of Anthropic API responses
   \* Find `"content"` array → first object → `"text"` field → extract string value
   \* Also build JSON request bodies (simpler — just string concatenation with escaping)
4. \*\*API key storage\*\* (`claude/config.c`)

   \* Hardcode the API key at compile time via a `#define` or a config header:

```c
     #define ANTHROPIC\\\_API\\\_KEY "sk-ant-..."
     ```

   \* The key gets baked into the kernel binary
   \* Add `claude/config.h` to `.gitignore` so it doesn't get committed
5. \*\*Panic-to-Claude Handler\*\* (`claude/panic\\\_handler.c`)

   \* Hook into the kernel's exception handlers
   \* On panic/fault: capture fault type, error code, register dump (EAX-ESP, EIP, EFLAGS), stack trace (walk EBP chain), and last N lines of kernel log
   \* Format all of this into a prompt:

```

&#x20;    "CLAOS kernel panic. Fault: Page Fault at 0xDEADBEEF.
      Error code: 0x00000002. Registers: EAX=... EBX=...
      Stack trace: \\\[addresses]. Diagnose this crash and suggest a fix."
     ```


* Send to Claude via the normal HTTPS path
* Display Claude's response on screen
* **NOTE:** The network stack and TLS must still be functional when the panic handler fires. If the panic is in the network stack itself, display a fallback message: "Panic in network subsystem — cannot reach Claude."

**Milestone:** From the CLAOS kernel, call `claude\\\_ask("Hello from CLAOS!", buf, sizeof(buf))` and get a real response from Claude displayed on screen. No relay. No host scripts. Pure CLAOS.

\---

### PHASE 5: Interactive Shell

1. **ClaudeShell** (`shell/shell.c`)

   * Command-line interface displayed after boot
   * Prompt: `claos> `
   * Built-in commands:

     * `claude <message>` — send a prompt to Claude and display the response
     * `sysinfo` — show memory usage, uptime, task count, network status
     * `clear` — clear screen
     * `panic` — deliberately trigger a kernel panic (to demo the panic→Claude flow)
     * `reboot` — triple-fault reboot
     * `help` — list commands
     * `tasks` — show running tasks
     * `net` — show network configuration (IP, gateway, DNS, NIC status)
     * `ping <ip>` — send ICMP echo request (nice to have)
   * Any unrecognized command is sent to Claude with context: "The user typed this command in CLAOS shell. Interpret it and respond helpfully."
2. **Embedded Lua** (stretch goal for Phase 5)

   * Port Lua 5.4 interpreter (ANSI C, very portable)
   * Expose kernel APIs as Lua functions:

     * `claos.ask(prompt)` — call Claude
     * `claos.print(text)` — write to screen
     * `claos.uptime()` — get system uptime
     * `claos.mem\\\_free()` — get free memory
   * `run <script.lua>` shell command to execute Lua scripts

**Milestone:** Full interactive experience — boot CLAOS, type `claude What is the meaning of life?` and get a response directly from Anthropic's API. Type `panic` and watch Claude analyze the crash in real time.

\---

### PHASE 6: GUI (Stretch Goal)

Reference the CLAOS GUI mockup for the target design. Key elements:

1. **VESA framebuffer** — switch to graphical mode via VBE during boot
2. **Basic drawing primitives** — pixels, rectangles, lines, bitmap font rendering
3. **Mouse driver** — PS/2 mouse via IRQ12
4. **Window compositor** — dark theme, purple accent (#7F77DD), terminal-native aesthetic
5. **Claude Chat Window** — main window with scrolling text, purple left-border for Claude responses
6. **System sidebar** — task monitor, memory, Claude connection status, panic watcher
7. **Top bar** — network status, memory, uptime
8. **Bottom taskbar** — CLAOS logo, window buttons, motto: "I am the kernel now."

Design philosophy: Dark, minimal, terminal-native. Not trying to be Windows/macOS. Monospace font everywhere. Purple (#7F77DD / #AFA9EC / #CECBF6) is the accent colour. Teal/green (#5DCAA5) for healthy/connected status.

\---

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
│   ├── panic.c             # Kernel panic handler
│   └── entropy.c           # Entropy pool for TLS
├── drivers/
│   ├── vga.c / vga.h       # VGA text mode
│   ├── keyboard.c          # PS/2 keyboard
│   ├── timer.c             # PIT timer
│   ├── pci.c / pci.h       # PCI bus enumeration
│   └── e1000.c / e1000.h   # Intel e1000 NIC
├── net/
│   ├── ethernet.c          # Ethernet frames
│   ├── arp.c               # ARP
│   ├── ipv4.c              # IPv4
│   ├── udp.c               # UDP (for DNS)
│   ├── dns.c               # DNS resolver
│   ├── tcp.c               # TCP
│   ├── tls.c               # BearSSL I/O callbacks
│   ├── tls\\\_client.c        # High-level TLS API
│   ├── ca\\\_certs.c          # Trusted root CA certificates
│   └── https.c             # HTTPS client
├── lib/
│   └── bearssl/            # BearSSL source (ported)
│       ├── inc/            # BearSSL headers
│       ├── src/            # BearSSL source files
│       └── bearssl\\\_shim.c  # CLAOS compatibility layer
├── claude/
│   ├── claude.c            # Claude API protocol layer
│   ├── json.c              # Minimal JSON builder/parser
│   ├── config.h            # API key (DO NOT COMMIT)
│   └── panic\\\_handler.c     # Panic → Claude integration
├── shell/
│   └── shell.c             # Interactive ClaudeShell
├── include/                # Shared headers
│   └── string.h            # Freestanding string functions
├── linker.ld               # Kernel linker script
├── Makefile                # Build system
├── .gitignore              # Excludes config.h with API key
└── README.md               # Project documentation
```

\---

## Technical Specifications

* **Architecture:** x86 (i686), 32-bit protected mode
* **Language:** C (kernel, drivers, userspace) + x86 NASM assembly (boot, low-level)
* **Compiler:** `i686-elf-gcc` (freestanding cross-compiler, `-ffreestanding -nostdlib`)
* **Assembler:** NASM
* **Target VM:** QEMU (`qemu-system-i386`) with `-device e1000` for networking
* **Boot method:** Legacy BIOS boot from raw disk image (not UEFI)
* **Disk image:** Flat binary, dd'd into a raw `.img` file
* **TLS:** BearSSL (ported), TLS 1.2
* **No external dependencies inside the OS** — no libc, no POSIX, no existing OS code. BearSSL is the one exception as an embedded library, compiled into the kernel.
* **Network:** Static IP, native TCP/IP + TLS + HTTPS, direct to api.anthropic.com
* **QEMU launch flags:**

```bash
  qemu-system-i386 \\\\
    -drive format=raw,file=claos.img \\\\
    -device e1000,netdev=net0 \\\\
    -netdev user,id=net0 \\\\
    -m 128M \\\\
    -serial stdio
  ```

\---

## QEMU Networking Setup

QEMU's user-mode networking provides:

* Guest IP: `10.0.2.15` (CLAOS hardcodes this)
* Gateway: `10.0.2.2` (host)
* DNS: `10.0.2.3` (QEMU's built-in DNS proxy)
* Subnet: `255.255.255.0`
* The gateway NATs to the host's real network, so CLAOS can reach the internet

CLAOS network config (hardcoded):

```c
#define CLAOS\\\_IP        {10, 0, 2, 15}
#define CLAOS\\\_GATEWAY   {10, 0, 2, 2}
#define CLAOS\\\_SUBNET    {255, 255, 255, 0}
#define CLAOS\\\_DNS       {10, 0, 2, 3}
```

\---

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

\---

## Implementation Order \& Priority

Build in this exact order. Each phase must compile and run before moving to the next:

1. <b>~~Toolchain + Makefile~~</b> ✅
2. <b>~~Phase 1~~</b> ~~— Boot to a blinking cursor with keyboard input~~ ✅
3. **Phase 2** — Memory + scheduler ← IN PROGRESS
4. **Phase 3** — Network stack (e1000, Ethernet, ARP, IP, UDP, DNS, TCP)
5. **Phase 3.5** — TLS via BearSSL port (the big one)
6. **Phase 4** — HTTPS client + Claude integration
7. **Phase 5** — Interactive shell
8. **Phase 6** — GUI (if we get here, we've already won)

\---

## Key Constraints

* **ABSOLUTELY NO existing OS code.** No Linux headers, no glibc, no POSIX. Write everything from scratch. BearSSL is the sole external library, ported and compiled into the kernel.
* **No bootloader frameworks** like GRUB — write the bootloader manually.
* **No relay scripts or host-side helpers.** CLAOS does its own networking end-to-end.
* **Freestanding C only** — `-ffreestanding -nostdlib -fno-builtin`. Implement your own `memcpy`, `memset`, `strlen`, etc.
* **Keep it simple.** This is a toy. A single-user, single-core, non-preemptive (or simply preemptive) system is fine.
* **Comments everywhere.** This is an educational project. Every function should have a comment explaining what it does and why.
* **Test after every phase.** Boot in QEMU and verify before moving on.

\---

## Personality

CLAOS should have personality. It's an AI-native OS and it knows it.

* Boot messages should be witty ("Initializing consciousness... done.")
* Panic messages should be dramatic ("KERNEL PANIC: I have made a terrible mistake. Calling Claude for help...")
* The shell prompt should feel alive
* When Claude responds to a crash, display it like a conversation:

```
  \\\[CLAOS PANIC] Page fault at 0xDEADBEEF
  \\\[CLAOS → Claude] Sending crash report via HTTPS...
  \\\[Claude] It looks like you dereferenced a null pointer in scheduler.c line 47.
           The task struct wasn't initialized before being added to the run queue.
           Try initializing task->stack\\\_top before calling schedule\\\_task().
  ```

\---

## Success Criteria

The project is "done" (for meme purposes) when you can:

1. Boot CLAOS in QEMU
2. See the ASCII art banner
3. Type `claude Hello, I am talking to you from my own operating system` and get a real response **directly from api.anthropic.com over HTTPS** — no relay, no middleware, no cheating
4. Type `panic` and watch Claude diagnose the crash
5. Screenshot it, post it, collect internet points
6. **Bonus:** Boot it on real hardware with an Intel NIC and do the same thing

Go build something legendary.

