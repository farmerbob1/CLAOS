#
# CLAOS — Claude Assisted Operating System
# Makefile — Builds the entire OS from source and produces a bootable disk image
#
# Usage:
#   make          — Build claos.img
#   make run      — Build and run in QEMU
#   make clean    — Remove build artifacts
#   make debug    — Build and run with QEMU debug output
#

# ─── Toolchain ───────────────────────────────────────────────
# Cross-compiler targeting bare-metal i686 (no OS, no libc)
# Adjust TOOLCHAIN_PREFIX if your tools are installed elsewhere
TOOLCHAIN_PREFIX = /c/i686-elf-tools/bin/
CC      = $(TOOLCHAIN_PREFIX)i686-elf-gcc
LD      = $(TOOLCHAIN_PREFIX)i686-elf-ld
OBJCOPY = $(TOOLCHAIN_PREFIX)i686-elf-objcopy
AS      = /c/msys64/usr/bin/nasm
QEMU    = /c/msys64/mingw64/bin/qemu-system-i386

# ─── Compiler Flags ──────────────────────────────────────────
CFLAGS  = -ffreestanding -nostdlib -fno-builtin -fno-pie \
          -Wall -Wextra -Wno-unused-parameter \
          -I include -I kernel -I drivers -I net -I claude -I shell -I fs -I lua -I gui \
          -I lib/bearssl/inc -I lib/bearssl/src \
          -I lib/lua/src \
          -O2 -g

# BearSSL gets compiled with relaxed warnings (it's third-party code)
BEARSSL_CFLAGS = -ffreestanding -nostdlib -fno-builtin -fno-pie \
                 -isystem include -I lib/bearssl/inc -I lib/bearssl/src \
                 -D__CLAOS__=1 -w -O2

LDFLAGS = -T linker.ld -nostdlib
ASFLAGS_BIN = -f bin
ASFLAGS_ELF = -f elf32

# ─── Source Files ────────────────────────────────────────────
# CLAOS C source files
C_SOURCES = kernel/main.c \
            kernel/gdt.c \
            kernel/idt.c \
            kernel/panic.c \
            kernel/string.c \
            kernel/pmm.c \
            kernel/vmm.c \
            kernel/heap.c \
            kernel/scheduler.c \
            kernel/entropy.c \
            drivers/vga.c \
            drivers/keyboard.c \
            drivers/timer.c \
            drivers/pci.c \
            drivers/e1000.c \
            drivers/ata.c \
            drivers/mouse.c \
            fs/chaosfs.c \
            net/ethernet.c \
            net/arp.c \
            net/ipv4.c \
            net/udp.c \
            net/dns.c \
            net/tcp.c \
            net/tls_client.c \
            net/ca_certs.c \
            net/https.c \
            claude/claude.c \
            claude/json.c \
            claude/panic_handler.c \
            shell/shell.c \
            lua/claos_lib.c \
            gui/fb.c \
            gui/font.c \
            gui/console.c \
            gui/input.c \
            lib/bearssl/bearssl_shim.c \
            lib/lua/lua_shim.c

# BearSSL source files (found automatically, excluding sysrng.c which we replace)
BEARSSL_SOURCES = $(filter-out lib/bearssl/src/rand/sysrng.c, \
                    $(shell find lib/bearssl/src -name '*.c'))

# Lua 5.5 source files (exclude lua.c and luac.c — standalone tools we don't need)
LUA_SOURCES = $(filter-out lib/lua/src/lua.c lib/lua/src/luac.c, \
                $(shell find lib/lua/src -name '*.c'))

# Lua gets compiled with relaxed warnings and our shim defines
LUA_CFLAGS = -ffreestanding -nostdlib -fno-builtin -fno-pie \
             -isystem include -I lib/lua/src -I kernel -I drivers -I fs \
             -DLUA_USE_C89 -w -O2

# Assembly source files (ELF object files for linking with C)
ASM_SOURCES = kernel/entry.asm \
              kernel/isr.asm \
              kernel/irq.asm \
              kernel/gdt_flush.asm \
              kernel/scheduler_asm.asm \
              kernel/setjmp.asm

# ─── Object Files ────────────────────────────────────────────
C_OBJECTS       = $(C_SOURCES:.c=.o)
BEARSSL_OBJECTS = $(BEARSSL_SOURCES:.c=.o)
LUA_OBJECTS     = $(LUA_SOURCES:.c=.o)
ASM_OBJECTS     = $(ASM_SOURCES:.asm=.o)
# entry.o MUST come first so _entry is at the start of the binary
OBJECTS = kernel/entry.o $(filter-out kernel/entry.o,$(ASM_OBJECTS)) \
          $(C_OBJECTS) $(BEARSSL_OBJECTS) $(LUA_OBJECTS)

# ─── Build Targets ───────────────────────────────────────────

.PHONY: all clean run debug

all: claos.img

# Stage 1: MBR bootloader (raw binary, 512 bytes)
boot/stage1.bin: boot/stage1.asm
	$(AS) $(ASFLAGS_BIN) $< -o $@

# Stage 2: Protected mode setup (raw binary)
boot/stage2.bin: boot/stage2.asm
	$(AS) $(ASFLAGS_BIN) $< -o $@

# Compile CLAOS C source files
$(C_OBJECTS): %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile BearSSL source files (with relaxed warnings)
$(BEARSSL_OBJECTS): %.o: %.c
	$(CC) $(BEARSSL_CFLAGS) -c $< -o $@

# Compile Lua source files
$(LUA_OBJECTS): %.o: %.c
	$(CC) $(LUA_CFLAGS) -c $< -o $@

# Assemble NASM source files to ELF object files
%.o: %.asm
	$(AS) $(ASFLAGS_ELF) $< -o $@

# Link all kernel + BearSSL objects into a flat binary
kernel.bin: $(OBJECTS)
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJECTS)
	$(OBJCOPY) -O binary kernel.elf kernel.bin
	@echo "Kernel size: $$(stat -c%s kernel.bin) bytes"

# Build the final disk image
# Layout: [Stage1 512B] [Stage2 + Kernel padded to fill sectors]
# Build/update the disk image.
# If claos.img doesn't exist, create it fresh with ChaosFS formatted.
# If it already exists, only overwrite the boot/kernel sectors — ChaosFS data is preserved.
claos.img: boot/stage1.bin boot/stage2.bin kernel.bin
	@echo "=== Building CLAOS disk image ==="
	@if [ ! -f claos.img ]; then \
		echo "  Creating new 64MB disk image..."; \
		dd if=/dev/null of=claos.img bs=1 count=0 seek=67108864 2>/dev/null; \
		/c/msys64/usr/bin/python3 tools/mkchaosfs.py claos.img --format; \
		MSYS_NO_PATHCONV=1 /c/msys64/usr/bin/python3 tools/mkchaosfs.py claos.img --add /welcome.txt "Welcome to CLAOS! Type help for commands, or just talk to Claude."; \
		MSYS_NO_PATHCONV=1 /c/msys64/usr/bin/python3 tools/mkchaosfs.py claos.img --mkdir /scripts; \
		MSYS_NO_PATHCONV=1 /c/msys64/usr/bin/python3 tools/mkchaosfs.py claos.img --mkdir /logs; \
		MSYS_NO_PATHCONV=1 /c/msys64/usr/bin/python3 tools/mkchaosfs.py claos.img --add /scripts/hello.lua "print('Hello from Lua on CLAOS!')\nprint('Uptime: ' .. claos.uptime() .. 's')"; \
		MSYS_NO_PATHCONV=1 /c/msys64/usr/bin/python3 tools/mkchaosfs.py claos.img --add /scripts/chat.lua "print('CLAOS Lua Chat — type quit to exit')\nwhile true do\n  io.write('You: ')\n  local msg = claos.input('You: ')\n  if msg == 'quit' then break end\n  local resp = claos.ask(msg)\n  if resp then print('Claude: ' .. resp) end\nend"; \
	fi
	@# Write bootloader + kernel to the start of the disk (preserves ChaosFS at sector 2048+)
	dd if=boot/stage1.bin of=claos.img bs=512 conv=notrunc 2>/dev/null
	dd if=boot/stage2.bin of=claos.img bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=kernel.bin of=claos.img bs=512 seek=10 conv=notrunc 2>/dev/null
	@echo "=== claos.img updated (ChaosFS preserved) ==="
	@ls -la claos.img

# Force recreate the disk image from scratch (destroys ChaosFS data)
.PHONY: newdisk
newdisk:
	rm -f claos.img
	$(MAKE) claos.img

# Run in QEMU with e1000 NIC
run: claos.img
	$(QEMU) \
		-drive format=raw,file=claos.img \
		-device e1000,netdev=net0 \
		-netdev user,id=net0 \
		-m 128M \
		-serial stdio

# Run with debug output
debug: claos.img
	$(QEMU) \
		-drive format=raw,file=claos.img \
		-device e1000,netdev=net0 \
		-netdev user,id=net0 \
		-m 128M \
		-serial stdio \
		-no-reboot \
		-d int,cpu_reset -no-shutdown

# Clean build artifacts
# Clean build artifacts but KEEP claos.img (preserves ChaosFS data)
clean:
	rm -f boot/stage1.bin boot/stage2.bin
	rm -f kernel.bin kernel.elf
	rm -f $(C_OBJECTS) $(ASM_OBJECTS)
	find lib/bearssl/src -name '*.o' -delete 2>/dev/null || true
	find lib/lua/src -name '*.o' -delete 2>/dev/null || true
	@echo "=== Cleaned (claos.img preserved) ==="

# Full clean including disk image
fullclean: clean
	rm -f claos.img
	@echo "=== Full clean (disk image deleted) ==="
