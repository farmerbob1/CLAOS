;
; CLAOS — Claude Assisted Operating System
; stage1.asm — Stage 1 MBR Bootloader (512 bytes)
;
; This is the very first code that runs when the BIOS loads our disk.
; It lives in the first 512 bytes (the Master Boot Record).
;
; Job:
;   1. Set up segment registers and stack
;   2. Load Stage 2 from disk sectors 2-17 (16 sectors = 8KB) using BIOS INT 13h
;   3. Jump to Stage 2
;
; BIOS loads us at 0x7C00 in real mode (16-bit).
;

[BITS 16]
[ORG 0x7C00]

STAGE2_ADDR     equ 0x7E00      ; Stage 2 loads right after us
STAGE2_SECTORS  equ 32          ; 32 sectors = 16KB for Stage 2 + kernel

start:
    ; Clear interrupts while we set up
    cli

    ; Set up segment registers — all pointing to 0
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; Stack grows downward from where we're loaded

    ; Re-enable interrupts
    sti

    ; Save boot drive number (BIOS passes it in DL)
    mov [boot_drive], dl

    ; Print a loading message
    mov si, msg_loading
    call print_string

    ; Load Stage 2 from disk
    ; We use INT 13h, AH=02h (Read sectors)
    mov ah, 0x02                ; BIOS read sectors function
    mov al, STAGE2_SECTORS      ; Number of sectors to read
    mov ch, 0                   ; Cylinder 0
    mov cl, 2                   ; Start from sector 2 (sector 1 is the MBR)
    mov dh, 0                   ; Head 0
    mov dl, [boot_drive]        ; Drive number
    mov bx, STAGE2_ADDR         ; ES:BX = destination buffer
    int 0x13
    jc disk_error               ; Carry flag set = error

    ; Jump to Stage 2!
    mov si, msg_ok
    call print_string
    jmp STAGE2_ADDR

disk_error:
    mov si, msg_disk_err
    call print_string
    jmp hang

hang:
    cli
    hlt
    jmp hang

; ──────────────────────────────────────
; print_string — Print null-terminated string at SI using BIOS teletype
; ──────────────────────────────────────
print_string:
    pusha
.loop:
    lodsb                       ; Load byte at [SI] into AL, increment SI
    or al, al                   ; Check for null terminator
    jz .done
    mov ah, 0x0E                ; BIOS teletype output
    mov bh, 0                   ; Page 0
    int 0x10
    jmp .loop
.done:
    popa
    ret

; ──────────────────────────────────────
; Data
; ──────────────────────────────────────
boot_drive:     db 0
msg_loading:    db "CLAOS: Loading Stage 2...", 0
msg_ok:         db " OK", 13, 10, 0
msg_disk_err:   db " DISK ERROR!", 13, 10, 0

; ──────────────────────────────────────
; Padding and boot signature
; MBR must be exactly 512 bytes, ending with 0xAA55
; ──────────────────────────────────────
times 510 - ($ - $$) db 0
dw 0xAA55
