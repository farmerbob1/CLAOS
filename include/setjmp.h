/*
 * CLAOS — Minimal setjmp.h
 * Lua uses setjmp/longjmp for error handling.
 * GCC provides __builtin_setjmp/__builtin_longjmp but they're tricky.
 * Use the simple struct + inline asm approach for i686.
 */
#ifndef CLAOS_SETJMP_H
#define CLAOS_SETJMP_H

/* Save EBX, ESI, EDI, EBP, ESP, EIP — 6 registers */
typedef int jmp_buf[6];

int setjmp(jmp_buf buf);
void longjmp(jmp_buf buf, int val) __attribute__((noreturn));

#endif
