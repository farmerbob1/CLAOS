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
          -I include -I kernel -I drivers -I net \
          -I lib/bearssl/inc -I lib/bearssl/src \
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
            net/ethernet.c \
            net/arp.c \
            net/ipv4.c \
            net/udp.c \
            net/dns.c \
            net/tcp.c \
            net/tls_client.c \
            net/ca_certs.c \
            lib/bearssl/bearssl_shim.c

# BearSSL source files (found automatically, excluding sysrng.c which we replace)
BEARSSL_SOURCES = $(filter-out lib/bearssl/src/rand/sysrng.c, \
                    $(shell find lib/bearssl/src -name '*.c'))

# Assembly source files (ELF object files for linking with C)
ASM_SOURCES = kernel/entry.asm \
              kernel/isr.asm \
              kernel/irq.asm \
              kernel/gdt_flush.asm \
              kernel/scheduler_asm.asm

# ─── Object Files ────────────────────────────────────────────
C_OBJECTS       = $(C_SOURCES:.c=.o)
BEARSSL_OBJECTS = $(BEARSSL_SOURCES:.c=.o)
ASM_OBJECTS     = $(ASM_SOURCES:.asm=.o)
# entry.o MUST come first so _entry is at the start of the binary
OBJECTS = kernel/entry.o $(filter-out kernel/entry.o,$(ASM_OBJECTS)) \
          $(C_OBJECTS) $(BEARSSL_OBJECTS)

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
claos.img: boot/stage1.bin boot/stage2.bin kernel.bin
	@echo "=== Building CLAOS disk image ==="
	cat boot/stage1.bin boot/stage2.bin kernel.bin > claos.img
	@# Pad to at least 2MB for larger kernel with BearSSL
	truncate -s 2M claos.img || dd if=/dev/null of=claos.img bs=1 count=0 seek=2097152
	@echo "=== claos.img built successfully ==="
	@ls -la claos.img

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
clean:
	rm -f boot/stage1.bin boot/stage2.bin
	rm -f kernel.bin kernel.elf
	rm -f claos.img
	rm -f $(C_OBJECTS) $(ASM_OBJECTS)
	find lib/bearssl/src -name '*.o' -delete 2>/dev/null || true
	@echo "=== Cleaned ==="
