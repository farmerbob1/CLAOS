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
# -ffreestanding: Don't assume a hosted environment (no libc)
# -nostdlib:      Don't link standard libraries
# -fno-builtin:   Don't use GCC built-in function replacements
# -Wall:          All warnings (this is educational, catch mistakes)
# -Wextra:        Even more warnings
# -I include -I kernel -I drivers: Header search paths
CFLAGS  = -ffreestanding -nostdlib -fno-builtin -fno-pie \
          -Wall -Wextra -Wno-unused-parameter \
          -I include -I kernel -I drivers -O1 -g
LDFLAGS = -T linker.ld -nostdlib
ASFLAGS_BIN = -f bin
ASFLAGS_ELF = -f elf32

# ─── Source Files ────────────────────────────────────────────
# C source files
C_SOURCES = kernel/main.c \
            kernel/gdt.c \
            kernel/idt.c \
            kernel/panic.c \
            kernel/string.c \
            kernel/pmm.c \
            kernel/vmm.c \
            kernel/heap.c \
            kernel/scheduler.c \
            drivers/vga.c \
            drivers/keyboard.c \
            drivers/timer.c

# Assembly source files (ELF object files for linking with C)
ASM_SOURCES = kernel/entry.asm \
              kernel/isr.asm \
              kernel/irq.asm \
              kernel/gdt_flush.asm \
              kernel/scheduler_asm.asm

# ─── Object Files ────────────────────────────────────────────
C_OBJECTS   = $(C_SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
# entry.o MUST come first so _entry is at the start of the binary
OBJECTS     = kernel/entry.o $(filter-out kernel/entry.o,$(ASM_OBJECTS)) $(C_OBJECTS)

# ─── Build Targets ───────────────────────────────────────────

.PHONY: all clean run debug

all: claos.img

# Stage 1: MBR bootloader (raw binary, 512 bytes)
boot/stage1.bin: boot/stage1.asm
	$(AS) $(ASFLAGS_BIN) $< -o $@

# Stage 2: Protected mode setup (raw binary)
boot/stage2.bin: boot/stage2.asm
	$(AS) $(ASFLAGS_BIN) $< -o $@

# Compile C source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble NASM source files to ELF object files
%.o: %.asm
	$(AS) $(ASFLAGS_ELF) $< -o $@

# Link all kernel objects into a flat binary
kernel.bin: $(OBJECTS)
	$(LD) $(LDFLAGS) -o kernel.elf $(OBJECTS)
	$(OBJCOPY) -O binary kernel.elf kernel.bin

# Build the final disk image
# Layout: [Stage1 512B] [Stage2 + Kernel padded to fill sectors]
claos.img: boot/stage1.bin boot/stage2.bin kernel.bin
	@echo "=== Building CLAOS disk image ==="
	cat boot/stage1.bin boot/stage2.bin kernel.bin > claos.img
	@# Pad to at least 1MB (QEMU likes round numbers)
	truncate -s 1M claos.img || dd if=/dev/null of=claos.img bs=1 count=0 seek=1048576
	@echo "=== claos.img built successfully ==="
	@ls -la claos.img

# Run in QEMU
run: claos.img
	$(QEMU) \
		-drive format=raw,file=claos.img \
		-m 128M \
		-serial stdio \
		-no-reboot

# Run with debug output and serial console
debug: claos.img
	$(QEMU) \
		-drive format=raw,file=claos.img \
		-m 128M \
		-serial stdio \
		-no-reboot \
		-d int,cpu_reset -no-shutdown

# Clean build artifacts
clean:
	rm -f boot/stage1.bin boot/stage2.bin
	rm -f kernel.bin kernel.elf
	rm -f claos.img
	rm -f $(OBJECTS)
	@echo "=== Cleaned ==="
