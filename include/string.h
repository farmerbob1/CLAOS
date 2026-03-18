/*
 * CLAOS — Claude Assisted Operating System
 * string.h — Freestanding string/memory utility declarations
 *
 * We implement our own since we have no libc.
 */

#ifndef CLAOS_STRING_H
#define CLAOS_STRING_H

#include "types.h"

/* Memory operations */
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int val, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);

/* String operations */
size_t strlen(const char* str);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strcpy(char* dest, const char* src);
char*  strncpy(char* dest, const char* src, size_t n);
char*  strcat(char* dest, const char* src);

#endif /* CLAOS_STRING_H */
