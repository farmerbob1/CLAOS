;
; CLAOS — Claude Assisted Operating System
; stage2.asm — Stage 2 Bootloader
;
; Simple approach: Load kernel to low memory (0x10000-0x90000 = 512KB)
; in real mode, then copy it to 0x100000 after entering protected mode.
;

[BITS 16]
[ORG 0x7E00]

KERNEL_PHYS     equ 0x100000    ; Final kernel location
LOAD_BASE       equ 0x10000     ; Temp load area (64KB-576KB)
LOAD_SEG_START  equ 0x1000      ; Segment for 0x10000

stage2_start:
    mov si, msg_stage2
    call print_string_rm

    ; Detect and set VESA mode (VGA BIOS is ready after print_string_rm used INT 10h)
    call detect_vbe

    call detect_memory

    call enable_a20
    mov si, msg_a20
    call print_string_rm

    ; ── Load kernel from disk to low memory ──
    mov si, msg_loading
    call print_string_rm

    ; Load in chunks of 64 sectors (32KB) to different segments
    mov word [cur_seg], LOAD_SEG_START
    mov dword [cur_lba], 10         ; Kernel starts at LBA 10
    mov word [chunks_left], 20      ; 20 chunks × 32KB = 640KB max

.load_loop:
    cmp word [chunks_left], 0
    je .load_done

    ; Set up DAP for extended read
    mov byte [dap], 0x10
    mov byte [dap+1], 0
    mov word [dap+2], 64            ; 64 sectors = 32KB
    mov word [dap+4], 0x0000        ; Offset = 0
    mov ax, [cur_seg]
    mov word [dap+6], ax            ; Segment
    mov eax, [cur_lba]
    mov dword [dap+8], eax          ; LBA
    mov dword [dap+12], 0

    mov si, dap
    mov ah, 0x42
    mov dl, 0x80
    int 0x13
    jc .load_done                   ; If read fails, assume we've read everything

    ; Advance: next segment += 0x800 (32KB / 16 = 0x800 paragraphs)
    add word [cur_seg], 0x800
    add dword [cur_lba], 64
    dec word [chunks_left]

    ; Don't load past 0xA000:0 = 0xA0000 (conventional memory limit)
    cmp word [cur_seg], 0xA000
    jae .load_done

    mov al, '.'
    mov ah, 0x0E
    int 0x10
    jmp .load_loop

.load_done:
    mov si, msg_done
    call print_string_rm

    ; Calculate how many bytes we loaded
    mov ax, [cur_seg]
    sub ax, LOAD_SEG_START
    shl eax, 4                      ; Segment × 16 = bytes
    mov [kernel_size], eax

    ; ── Enter protected mode ──
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

; Variables
cur_seg:        dw 0
cur_lba:        dd 0
chunks_left:    dw 0
kernel_size:    dd 0
dap:            times 16 db 0

; ──────────────────────────────────────────────────────────────
detect_memory:
    pusha
    mov di, 0x8004
    xor ebx, ebx
    xor si, si
.e820_loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc si
    add di, 24
    test ebx, ebx
    jnz .e820_loop
.e820_done:
    mov [0x8000], si
    popa
    ret

enable_a20:
    mov ax, 0x2401
    int 0x15
    call .a20_kb
    ret
.a20_kb:
    call .wi
    mov al, 0xAD
    out 0x64, al
    call .wi
    mov al, 0xD0
    out 0x64, al
    call .wo
    in al, 0x60
    push ax
    call .wi
    mov al, 0xD1
    out 0x64, al
    call .wi
    pop ax
    or al, 2
    out 0x60, al
    call .wi
    mov al, 0xAE
    out 0x64, al
    call .wi
    ret
.wi:
    in al, 0x64
    test al, 2
    jnz .wi
    ret
.wo:
    in al, 0x64
    test al, 1
    jz .wo
    ret

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

msg_stage2:  db "CLAOS: Stage 2.", 13, 10, 0
msg_a20:     db "CLAOS: A20 OK.", 13, 10, 0
msg_loading: db "CLAOS: Loading kernel", 0
msg_done:    db " OK.", 13, 10, 0

; ══════════════════════════════════════════════════════════════
; GDT
gdt_start:
    dd 0, 0                                             ; Null
    dw 0xFFFF, 0x0000                                   ; Code: 0x08
    db 0x00, 10011010b, 11001111b, 0x00
    dw 0xFFFF, 0x0000                                   ; Data: 0x10
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ══════════════════════════════════════════════════════════════
[BITS 32]

pm_entry:
    ; Set up data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Copy kernel from 0x10000 to 0x100000
    ; We loaded up to 512KB (0x80000 bytes). Just copy the max —
    ; copying extra zeros is harmless and avoids variable address issues.
    mov esi, LOAD_BASE              ; Source: where we loaded in real mode
    mov edi, KERNEL_PHYS            ; Destination: 1MB
    mov ecx, 0x90000                ; Copy 576KB (enough for kernel + BearSSL + Lua)
    rep movsb                       ; Copy!

    ; Jump to the kernel at 1MB
    call KERNEL_PHYS

    cli
    hlt
    jmp $

; ──────────────────────────────────────────────────────────────
; VBE Detection Subroutine (placed after 32-bit section to avoid
; shifting GDT/pm_entry addresses which causes cross-section
; relocation issues in NASM flat binaries)
[BITS 16]
detect_vbe:
    pusha
    mov byte [0x9000], 0

    push es
    xor ax, ax
    mov es, ax
    mov ax, 0x4F00
    mov di, 0x9010
    mov dword [es:di], 'VBE2'
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .vdone

    push es
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, 0x118
    mov di, 0x9200
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .vtry2

    ; Accept 32bpp or 24bpp
    cmp byte [0x9200 + 25], 32
    je .vmode_ok
    cmp byte [0x9200 + 25], 24
    je .vmode_ok
    jmp .vtry2

.vmode_ok:
    mov bx, 0x4118
    jmp .vset

.vtry2:
    push es
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, 0x115
    mov di, 0x9200
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .vdone
    cmp byte [0x9200 + 25], 32
    je .vfb_ok
    cmp byte [0x9200 + 25], 24
    je .vfb_ok
    jmp .vdone
.vfb_ok:
    mov bx, 0x4115

.vset:
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .vdone

    mov byte [0x9000], 1
    mov eax, [0x9200 + 40]
    mov [0x9004], eax
    mov ax, [0x9200 + 18]
    mov [0x9008], ax
    mov ax, [0x9200 + 20]
    mov [0x900A], ax
    mov ax, [0x9200 + 16]
    mov [0x900C], ax
    mov al, [0x9200 + 25]
    mov [0x900E], al

.vdone:
    popa
    ret

; Pad to 9 sectors (0x1200 bytes)
times 0x1200 - ($ - $$) db 0
