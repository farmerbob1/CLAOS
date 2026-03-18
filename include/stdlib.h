/*
 * CLAOS — Minimal stdlib.h stub for freestanding mode.
 * Only provides what BearSSL might need.
 */

#ifndef CLAOS_STDLIB_H
#define CLAOS_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7FFFFFFF

void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* ptr, size_t size);
void  free(void* ptr);
void  abort(void);
void  exit(int status);
int   abs(int x);
long  strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);
double strtod(const char* str, char** endptr);
int   system(const char* cmd);
char* getenv(const char* name);
char* tmpnam(char* s);

#endif /* CLAOS_STDLIB_H */
