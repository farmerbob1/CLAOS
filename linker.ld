/*
 * CLAOS — Claude Assisted Operating System
 * linker.ld — Kernel linker script
 *
 * This tells the linker how to arrange kernel sections in memory.
 * The kernel is loaded starting at 0x8200 (right after the bootloader
 * stages in the disk image: Stage 1 at 0x7C00 = 512 bytes,
 * Stage 2 at 0x7E00 = loaded from sector 2).
 *
 * Since Stage 2 is loaded to 0x7E00 and occupies some space, we place
 * the kernel code right after Stage 2 in the binary image. Stage 2
 * calls kernel_main at this address.
 */

ENTRY(kernel_main)

SECTIONS
{
    /* Kernel is loaded to 1MB (0x100000) by Stage 2.
     * This gives us plenty of space for a large kernel with BearSSL. */
    . = 0x100000;

    .text ALIGN(16) : {
        *(.text.entry)    /* Entry stub MUST be first */
        *(.text)
    }

    .rodata ALIGN(16) : {
        *(.rodata*)
    }

    .data ALIGN(16) : {
        *(.data)
    }

    .bss ALIGN(16) : {
        *(COMMON)
        *(.bss)
    }

    /* Remember the end of the kernel for memory management later */
    _kernel_end = .;
}
