;
; CLAOS — Claude Assisted Operating System
; stage1.asm — Stage 1 MBR Bootloader (512 bytes)
;
; Loads Stage 2 + kernel from disk using INT 13h Extended Read (LBA).
; This supports loading much larger payloads than the old CHS method.
;
; We load in 32KB chunks to different memory segments, covering up to
; 512KB of data (more than enough for kernel + BearSSL).
;
; Memory layout after loading:
;   0x7C00 - 0x7DFF : Stage 1 (this file, 512 bytes)
;   0x7E00 - 0xFFFF : Stage 2 + start of kernel (~32KB)
;   0x10000+        : Rest of kernel (loaded by Stage 2)
;

[BITS 16]
[ORG 0x7C00]

STAGE2_ADDR     equ 0x7E00      ; Stage 2 loads right after us
STAGE2_SECTORS  equ 64          ; 64 sectors = 32KB initial load

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Save boot drive number (BIOS passes it in DL)
    mov [boot_drive], dl

    mov si, msg_loading
    call print_string

    ; Load Stage 2 + kernel start using INT 13h Extended Read
    ; This avoids CHS geometry limits and supports LBA addressing
    mov si, dap             ; DS:SI = pointer to Disk Address Packet
    mov ah, 0x42            ; Extended read
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

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

print_string:
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
; Disk Address Packet for INT 13h AH=42h
; ──────────────────────────────────────
dap:
    db 0x10             ; Size of DAP (16 bytes)
    db 0                ; Reserved
    dw STAGE2_SECTORS   ; Number of sectors to read
    dw STAGE2_ADDR      ; Offset (destination buffer)
    dw 0x0000           ; Segment (0x0000:STAGE2_ADDR)
    dd 1                ; LBA start (sector 1 = second sector)
    dd 0                ; LBA high 32 bits (always 0 for us)

boot_drive:     db 0
msg_loading:    db "CLAOS: Loading...", 0
msg_ok:         db " OK", 13, 10, 0
msg_disk_err:   db " DISK ERR!", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
