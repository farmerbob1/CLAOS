;
; CLAOS — Claude Assisted Operating System
; stage2.asm — Stage 2 Bootloader
;
; This runs after Stage 1 loads us into memory at 0x7E00.
;
; Job:
;   1. Detect available memory via BIOS INT 15h, E820
;   2. Enable the A20 line (so we can address above 1MB)
;   3. Set up the Global Descriptor Table (GDT) for protected mode
;   4. Switch to 32-bit protected mode
;   5. Jump to the C kernel entry point
;

[BITS 16]
[ORG 0x7E00]

KERNEL_ADDR     equ 0x100000    ; Kernel loaded at 1MB (we copy it there after paging)
KERNEL_LOAD     equ 0x10000     ; Temporarily load kernel at 64KB in real mode

stage2_start:
    mov si, msg_stage2
    call print_string_rm

    ; ──────────────────────────────────────
    ; Step 1: Detect memory map using BIOS INT 15h, E820
    ; Store entries at 0x8000, count at 0x7FF0
    ; ──────────────────────────────────────
    call detect_memory

    ; ──────────────────────────────────────
    ; Step 2: Enable the A20 line
    ; The A20 line is a legacy hack. With it disabled, the 21st address
    ; bit is forced to 0, which means we can't address above 1MB.
    ; We try multiple methods because hardware is weird.
    ; ──────────────────────────────────────
    call enable_a20

    mov si, msg_a20
    call print_string_rm

    ; ──────────────────────────────────────
    ; Step 3: Load the GDT register
    ; The GDT defines memory segments for protected mode.
    ; We set up a flat memory model (base=0, limit=4GB) for both
    ; code and data segments.
    ; ──────────────────────────────────────
    cli                         ; Disable interrupts for mode switch
    lgdt [gdt_descriptor]       ; Load GDT

    ; ──────────────────────────────────────
    ; Step 4: Enter protected mode
    ; Set bit 0 of CR0 (Protection Enable)
    ; ──────────────────────────────────────
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to flush the CPU pipeline and load CS with the
    ; code segment selector (0x08) from our GDT
    jmp 0x08:protected_mode_entry

; ──────────────────────────────────────────────────────────────
; detect_memory — Use BIOS INT 15h, E820 to get the memory map
; Stores entries at 0x8000, entry count at 0x7FF0
; ──────────────────────────────────────────────────────────────
detect_memory:
    pusha
    mov di, 0x8004              ; Start storing entries at 0x8004 (leave room for count)
    xor ebx, ebx               ; Continuation value (0 = start)
    xor si, si                  ; Entry counter

.e820_loop:
    mov eax, 0xE820             ; Function number
    mov ecx, 24                 ; Size of one entry (24 bytes)
    mov edx, 0x534D4150         ; 'SMAP' magic number
    int 0x15
    jc .e820_done               ; Carry set = error or end

    cmp eax, 0x534D4150         ; BIOS should return 'SMAP' in EAX
    jne .e820_done

    inc si                      ; Count this entry
    add di, 24                  ; Move to next entry slot

    test ebx, ebx              ; If EBX=0, we're done
    jnz .e820_loop

.e820_done:
    mov [0x8000], si            ; Store entry count at 0x8000
    popa
    ret

; ──────────────────────────────────────────────────────────────
; enable_a20 — Try multiple methods to enable the A20 gate
; ──────────────────────────────────────────────────────────────
enable_a20:
    ; Method 1: BIOS function
    mov ax, 0x2401
    int 0x15

    ; Method 2: Keyboard controller
    call .a20_keyboard
    ret

.a20_keyboard:
    ; Wait for keyboard controller to be ready
    call .a20_wait_input
    mov al, 0xAD               ; Disable keyboard
    out 0x64, al

    call .a20_wait_input
    mov al, 0xD0               ; Read output port
    out 0x64, al

    call .a20_wait_output
    in al, 0x60                ; Read output port data
    push ax

    call .a20_wait_input
    mov al, 0xD1               ; Write output port
    out 0x64, al

    call .a20_wait_input
    pop ax
    or al, 2                   ; Set A20 bit
    out 0x60, al

    call .a20_wait_input
    mov al, 0xAE               ; Re-enable keyboard
    out 0x64, al

    call .a20_wait_input
    ret

.a20_wait_input:
    in al, 0x64
    test al, 2
    jnz .a20_wait_input
    ret

.a20_wait_output:
    in al, 0x64
    test al, 1
    jz .a20_wait_output
    ret

; ──────────────────────────────────────────────────────────────
; print_string_rm — Print string in real mode (same as Stage 1)
; ──────────────────────────────────────────────────────────────
print_string_rm:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .loop
.done:
    popa
    ret

; ──────────────────────────────────────
; Messages (real mode)
; ──────────────────────────────────────
msg_stage2:     db "CLAOS: Stage 2 loaded. Entering protected mode...", 13, 10, 0
msg_a20:        db "CLAOS: A20 line enabled.", 13, 10, 0

; ══════════════════════════════════════════════════════════════
; Global Descriptor Table (GDT)
;
; We use a flat memory model: both code and data segments span
; the entire 4GB address space. This is the simplest setup and
; lets us use paging for real memory protection later.
;
; Entry format (8 bytes each):
;   - Limit (20 bits), Base (32 bits), Access byte, Flags
; ══════════════════════════════════════════════════════════════

gdt_start:

gdt_null:                       ; Null descriptor (required, index 0)
    dd 0
    dd 0

gdt_code:                       ; Code segment: selector 0x08
    dw 0xFFFF                   ; Limit bits 0-15
    dw 0x0000                   ; Base bits 0-15
    db 0x00                     ; Base bits 16-23
    db 10011010b                ; Access: present, ring 0, code, readable
    db 11001111b                ; Flags: 4KB granularity, 32-bit + Limit 16-19
    db 0x00                     ; Base bits 24-31

gdt_data:                       ; Data segment: selector 0x10
    dw 0xFFFF                   ; Limit bits 0-15
    dw 0x0000                   ; Base bits 0-15
    db 0x00                     ; Base bits 16-23
    db 10010010b                ; Access: present, ring 0, data, writable
    db 11001111b                ; Flags: 4KB granularity, 32-bit + Limit 16-19
    db 0x00                     ; Base bits 24-31

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT size (minus 1)
    dd gdt_start                 ; GDT base address

; ══════════════════════════════════════════════════════════════
; 32-bit Protected Mode
; ══════════════════════════════════════════════════════════════

[BITS 32]

protected_mode_entry:
    ; Set up segment registers with data segment selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up a stack at 0x90000 (plenty of room)
    mov esp, 0x90000

    ; Jump to our C kernel!
    ; The kernel is linked at 0x9000 (see linker.ld).
    ; Stage 1 loaded us + kernel as a contiguous block starting at 0x7E00,
    ; so the kernel code sits at 0x9000 in physical memory.
    call 0x9000

    ; If the kernel ever returns, hang
    cli
    hlt
    jmp $

; Pad Stage 2 to exactly fill the space between 0x7E00 and 0x9000
; so the kernel binary lands at the right address in the disk image.
; 0x9000 - 0x7E00 = 0x1200 = 4608 bytes
times 0x1200 - ($ - $$) db 0
