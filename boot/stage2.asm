;
; CLAOS — Claude Assisted Operating System
; stage2.asm — Stage 2 Bootloader
;
; Simple approach: Load kernel to low memory (0x8000-0x9FFFF = 608KB)
; in real mode, then copy it to 0x100000 after entering protected mode.
; Stage2 code is at 0x7E00 and finishes well before 0x8000, so the kernel
; can safely be loaded starting at 0x8000.
;

[BITS 16]
[ORG 0x7E00]

KERNEL_PHYS     equ 0x100000    ; Final kernel location
LOAD_BASE       equ 0x8000      ; Temp load area (0x8000 - 0x9FFFF = 608KB)
LOAD_SEG_START  equ 0x0800      ; Segment for 0x8000

stage2_start:
    mov si, msg_stage2
    call print_string_rm

    ; Probe VBE capabilities (does NOT switch mode — stays in text mode)
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
    mov di, 0x704               ; E820 entries start at 0x704
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
    mov [0x700], si             ; E820 entry count at 0x700
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

    ; Copy kernel from LOAD_BASE (0x8000) to 0x100000
    ; We loaded up to 608KB. Copy the max — extra zeros are harmless.
    mov esi, LOAD_BASE              ; Source: where we loaded in real mode
    mov edi, KERNEL_PHYS            ; Destination: 1MB
    mov ecx, 0x98000                ; Copy 608KB (enough for kernel + 3D engine)
    rep movsb                       ; Copy!

    ; Jump to the kernel at 1MB
    call KERNEL_PHYS

    cli
    hlt
    jmp $

; ──────────────────────────────────────────────────────────────
; VBE Probe Subroutine — detect-only, does NOT switch video mode.
; Stores VBE capabilities at 0x2000 for the kernel to use later
; when the GUI is launched. Boot stays in text mode.
;
; Memory layout at 0x2000 (BIOS-free zone):
;   0x2000 (1B): 0 = no VBE, 1 = VBE mode found (not yet active)
;   0x2002 (2B): VBE mode number (with 0x4000 linear FB bit)
;   0x2004 (4B): framebuffer physical address
;   0x2008 (2B): X resolution
;   0x200A (2B): Y resolution
;   0x200C (2B): pitch (bytes per scanline)
;   0x200E (1B): bits per pixel
[BITS 16]
detect_vbe:
    pusha
    mov byte [0x2000], 0

    ; Get VBE Controller Info
    push es
    xor ax, ax
    mov es, ax
    mov ax, 0x4F00
    mov di, 0x2010
    mov dword [es:di], 'VBE2'
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .vdone

    ; Try mode 0x118 (1024x768)
    push es
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, 0x118
    mov di, 0x2200
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .vtry2
    cmp byte [0x2200 + 25], 32
    je .vfound_118
    cmp byte [0x2200 + 25], 24
    je .vfound_118
    jmp .vtry2

.vfound_118:
    mov word [0x2002], 0x4118
    jmp .vstore

.vtry2:
    ; Try mode 0x115 (800x600)
    push es
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, 0x115
    mov di, 0x2200
    int 0x10
    pop es
    cmp ax, 0x004F
    jne .vdone
    cmp byte [0x2200 + 25], 32
    je .vfound_115
    cmp byte [0x2200 + 25], 24
    je .vfound_115
    jmp .vdone

.vfound_115:
    mov word [0x2002], 0x4115

.vstore:
    ; Store mode info for kernel (but do NOT set the mode)
    mov byte [0x2000], 1
    mov eax, [0x2200 + 40]
    mov [0x2004], eax
    mov ax, [0x2200 + 18]
    mov [0x2008], ax
    mov ax, [0x2200 + 20]
    mov [0x200A], ax
    mov ax, [0x2200 + 16]
    mov [0x200C], ax
    mov al, [0x2200 + 25]
    mov [0x200E], al

.vdone:
    popa
    ret

; Pad to 9 sectors (0x1200 bytes)
times 0x1200 - ($ - $$) db 0
