;
; CLAOS — Claude Assisted Operating System
; scheduler_asm.asm — Context switch routine
;
; task_switch(uint32_t* old_esp, uint32_t new_esp)
;
; Saves the current task's callee-save registers and ESP,
; then loads the new task's ESP and registers.
;
; We only save/restore EBX, ESI, EDI, EBP (callee-save per cdecl).
; The C compiler handles the rest, and EAX/ECX/EDX are caller-save
; so the calling code already saved them if needed.
;

[BITS 32]

global task_switch

task_switch:
    ; Arguments on stack: [ESP+4] = old_esp ptr, [ESP+8] = new_esp
    mov eax, [esp + 4]      ; EAX = pointer to old task's ESP storage
    mov edx, [esp + 8]      ; EDX = new task's ESP value

    ; Save callee-save registers onto the OLD task's stack
    push ebx
    push esi
    push edi
    push ebp

    ; Save the old task's stack pointer
    mov [eax], esp

    ; Load the new task's stack pointer
    mov esp, edx

    ; Restore callee-save registers from the NEW task's stack
    pop ebp
    pop edi
    pop esi
    pop ebx

    ; Return — this pops the NEW task's return address (EIP),
    ; effectively resuming execution where it was last switched out.
    ret
