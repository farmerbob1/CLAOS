/*
 * CLAOS — Minimal stdio.h stub for freestanding mode.
 * Provides what Lua needs: FILE, fprintf, sprintf, snprintf, etc.
 * Actual implementations are in lib/lua/lua_shim.c.
 */

#ifndef CLAOS_STDIO_H
#define CLAOS_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* FILE type — opaque, we don't really use it */
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct { int dummy; } FILE;
#endif

/* Standard streams */
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

/* EOF */
#define EOF (-1)

/* Buffering modes (unused but Lua references them) */
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define BUFSIZ 512
#define L_tmpnam 20

/* Format output */
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int fprintf(FILE* f, const char* fmt, ...);

/* Stream operations — stubs */
int fflush(FILE* f);
int fclose(FILE* f);
FILE* fopen(const char* path, const char* mode);
size_t fread(void* buf, size_t size, size_t count, FILE* f);
size_t fwrite(const void* buf, size_t size, size_t count, FILE* f);
int feof(FILE* f);
int ferror(FILE* f);
int fseek(FILE* f, long offset, int whence);
long ftell(FILE* f);
void clearerr(FILE* f);
int setvbuf(FILE* f, char* buf, int mode, size_t size);
int ungetc(int c, FILE* f);
int getc(FILE* f);
char* fgets(char* buf, int size, FILE* f);
int fputs(const char* s, FILE* f);

/* Seek constants */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* tmpfile */
FILE* tmpfile(void);
FILE* freopen(const char* path, const char* mode, FILE* f);
int   rename(const char* old_name, const char* new_name);
int   remove(const char* path);

/* For Lua's l_inspectstat */
#define WIFEXITED(x)   1
#define WEXITSTATUS(x) (x)

#endif /* CLAOS_STDIO_H */
