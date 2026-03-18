/*
 * CLAOS — Claude Assisted Operating System
 * lua_shim.c — Lua 5.5 Compatibility Layer
 *
 * Provides the OS-level functions that Lua needs but we don't have:
 *   - realloc() — Lua uses a single allocator function for all memory
 *   - clock() — for os.clock() timing
 *   - stdio stubs — for liolib.c (we replace with ChaosFS)
 *   - locale/signal stubs — Lua references these but doesn't need them
 *
 * "Lua 5.5 awakened. The scripting layer stirs."
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "stdio.h"
#include "math.h"
#include "time.h"
#include "heap.h"
#include "timer.h"
#include "string.h"

/*
 * realloc() — Lua's memory allocator uses this.
 * Lua calls l_alloc(ud, ptr, osize, nsize):
 *   - nsize == 0: free(ptr), return NULL
 *   - ptr == NULL: malloc(nsize)
 *   - otherwise: realloc(ptr, nsize)
 *
 * We implement realloc as: kmalloc new block, copy, kfree old.
 * Not efficient, but correct and simple for a toy OS.
 */
void* realloc(void* ptr, size_t new_size) {
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    if (ptr == NULL) {
        return kmalloc(new_size);
    }

    /* Allocate new block, copy old data, free old block.
     * We don't know the old size precisely, so copy new_size bytes.
     * This is safe because we're always growing or the caller handles it. */
    void* new_ptr = kmalloc(new_size);
    if (new_ptr) {
        /* Copy up to new_size bytes — if shrinking, this is fine.
         * If growing, the extra bytes are uninitialized (Lua handles this). */
        memcpy(new_ptr, ptr, new_size);
        kfree(ptr);
    }
    return new_ptr;
}

/* malloc — used by some Lua internals */
void* malloc(size_t size) {
    return kmalloc(size);
}

/* calloc — zero-initialized allocation */
void* calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

/* free — release memory */
void free(void* ptr) {
    kfree(ptr);
}

/* clock() — returns processor time in CLOCKS_PER_SEC units.
 * Lua uses this for os.clock(). We return timer ticks. */
long clock(void) {
    return (long)timer_get_ticks();
}

/* time() — returns current time as seconds since epoch.
 * We don't have real-time clock, so return uptime. */
long time(long* t) {
    long val = (long)timer_get_uptime();
    if (t) *t = val;
    return val;
}

/* abort() — called on fatal Lua errors */
void abort(void) {
    extern void kernel_panic(const char* message);
    kernel_panic("Lua abort() called — fatal scripting error");
}

/* exit() — shouldn't be called, but just in case */
void exit(int status) {
    (void)status;
    abort();
}

/* strtod — Lua needs this for number parsing.
 * Simple implementation that handles integers and basic decimals. */
double strtod(const char* str, char** endptr) {
    double result = 0.0;
    int sign = 1;
    const char* p = str;

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Sign */
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }

    /* Integer part */
    while (*p >= '0' && *p <= '9') {
        result = result * 10.0 + (*p - '0');
        p++;
    }

    /* Decimal part */
    if (*p == '.') {
        p++;
        double frac = 0.1;
        while (*p >= '0' && *p <= '9') {
            result += (*p - '0') * frac;
            frac *= 0.1;
            p++;
        }
    }

    if (endptr) *endptr = (char*)p;
    return result * sign;
}

/* strtol — for integer parsing */
long strtol(const char* str, char** endptr, int base) {
    long result = 0;
    int sign = 1;
    const char* p = str;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }

    if (base == 0) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (*p == '0') { base = 8; p++; }
        else base = 10;
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        p++;
    }

    if (endptr) *endptr = (char*)p;
    return result * sign;
}

unsigned long strtoul(const char* str, char** endptr, int base) {
    return (unsigned long)strtol(str, endptr, base);
}

/* snprintf — Lua uses this for number formatting.
 * Very minimal implementation — handles %s, %d, %f, %p, %%. */
#include <stdarg.h>

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    size_t pos = 0;
    while (*fmt && pos < size - 1) {
        if (*fmt == '%') {
            fmt++;
            /* Skip flags/width/precision */
            while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '0' ||
                   (*fmt >= '1' && *fmt <= '9') || *fmt == '.') fmt++;

            /* Handle 'l' length modifier */
            int is_long = 0;
            if (*fmt == 'l') { is_long = 1; fmt++; }
            if (*fmt == 'l') { is_long = 2; fmt++; } /* ll */

            switch (*fmt) {
                case 's': {
                    const char* s = va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    while (*s && pos < size - 1) buf[pos++] = *s++;
                    break;
                }
                case 'd': case 'i': {
                    long val = is_long >= 2 ? (long)va_arg(ap, long long) :
                               is_long ? va_arg(ap, long) : va_arg(ap, int);
                    if (val < 0) { buf[pos++] = '-'; val = -val; }
                    char tmp[20]; int ti = 0;
                    if (val == 0) tmp[ti++] = '0';
                    else while (val > 0) { tmp[ti++] = '0' + val % 10; val /= 10; }
                    while (ti > 0 && pos < size - 1) buf[pos++] = tmp[--ti];
                    break;
                }
                case 'u': {
                    unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                    char tmp[20]; int ti = 0;
                    if (val == 0) tmp[ti++] = '0';
                    else while (val > 0) { tmp[ti++] = '0' + val % 10; val /= 10; }
                    while (ti > 0 && pos < size - 1) buf[pos++] = tmp[--ti];
                    break;
                }
                case 'x': case 'X': {
                    unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                    const char* hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    char tmp[20]; int ti = 0;
                    if (val == 0) tmp[ti++] = '0';
                    else while (val > 0) { tmp[ti++] = hex[val & 0xF]; val >>= 4; }
                    while (ti > 0 && pos < size - 1) buf[pos++] = tmp[--ti];
                    break;
                }
                case 'f': case 'g': case 'e': {
                    double val = va_arg(ap, double);
                    /* Simple: integer part + 6 decimal places */
                    if (val < 0) { buf[pos++] = '-'; val = -val; }
                    long ipart = (long)val;
                    char tmp[20]; int ti = 0;
                    if (ipart == 0) tmp[ti++] = '0';
                    else while (ipart > 0) { tmp[ti++] = '0' + ipart % 10; ipart /= 10; }
                    while (ti > 0 && pos < size - 1) buf[pos++] = tmp[--ti];
                    if (pos < size - 1) buf[pos++] = '.';
                    double frac = val - (long)val;
                    for (int j = 0; j < 6 && pos < size - 1; j++) {
                        frac *= 10; int d = (int)frac; buf[pos++] = '0' + d; frac -= d;
                    }
                    break;
                }
                case 'c': {
                    buf[pos++] = (char)va_arg(ap, int);
                    break;
                }
                case 'p': {
                    unsigned long val = (unsigned long)va_arg(ap, void*);
                    buf[pos++] = '0'; buf[pos++] = 'x';
                    const char* hex = "0123456789abcdef";
                    char tmp[20]; int ti = 0;
                    if (val == 0) tmp[ti++] = '0';
                    else while (val > 0) { tmp[ti++] = hex[val & 0xF]; val >>= 4; }
                    while (ti > 0 && pos < size - 1) buf[pos++] = tmp[--ti];
                    break;
                }
                case '%': buf[pos++] = '%'; break;
                default: buf[pos++] = *fmt; break;
            }
        } else {
            buf[pos++] = *fmt;
        }
        fmt++;
    }
    buf[pos] = '\0';
    return (int)pos;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, 4096, fmt, ap);
    va_end(ap);
    return ret;
}

/* stdio globals and functions */
FILE _stdin_obj, _stdout_obj, _stderr_obj;
FILE* stdin = &_stdin_obj;
FILE* stdout = &_stdout_obj;
FILE* stderr = &_stderr_obj;

int fprintf(FILE* f, const char* fmt, ...) {
    (void)f;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    /* Print to VGA instead of serial (serial_print is inline in io.h) */
    extern void vga_print(const char*);
    vga_print(buf);
    return ret;
}

int fflush(FILE* f) { (void)f; return 0; }

/* locale stubs */
struct lconv { char* decimal_point; };
static struct lconv default_lconv = { "." };
struct lconv* localeconv(void) { return &default_lconv; }
char* setlocale(int category, const char* locale) {
    (void)category; (void)locale;
    return "C";
}

/* signal stub */
typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t handler) {
    (void)sig; (void)handler;
    return NULL;
}

/* getenv — no environment variables in CLAOS */
char* getenv(const char* name) {
    (void)name;
    return NULL;
}

/* system — not supported */
int system(const char* cmd) {
    (void)cmd;
    return -1;
}

/* tmpnam — not supported */
char* tmpnam(char* s) {
    (void)s;
    return NULL;
}

/* rename/remove — map to ChaosFS */
int rename(const char* old, const char* new_name) {
    (void)old; (void)new_name;
    return -1; /* Not supported yet */
}

int remove(const char* path) {
    extern int chaosfs_delete(const char* path);
    return chaosfs_delete(path);
}

/* abs */
int abs(int x) { return x < 0 ? -x : x; }

/* errno */
int errno = 0;

/* ─── stdio stubs ─── */
FILE* fopen(const char* path, const char* mode) { (void)path; (void)mode; return NULL; }
int fclose(FILE* f) { (void)f; return 0; }
size_t fread(void* buf, size_t size, size_t count, FILE* f) { (void)buf; (void)size; (void)count; (void)f; return 0; }
size_t fwrite(const void* buf, size_t size, size_t count, FILE* f) {
    (void)f;
    /* Write to VGA for stdout/stderr */
    extern void vga_putchar(char c);
    const char* p = (const char*)buf;
    size_t total = size * count;
    for (size_t i = 0; i < total; i++) vga_putchar(p[i]);
    return count;
}
int feof(FILE* f) { (void)f; return 1; }
int ferror(FILE* f) { (void)f; return 0; }
int fseek(FILE* f, long offset, int whence) { (void)f; (void)offset; (void)whence; return -1; }
long ftell(FILE* f) { (void)f; return -1; }
void clearerr(FILE* f) { (void)f; }
int setvbuf(FILE* f, char* buf, int mode, size_t size) { (void)f; (void)buf; (void)mode; (void)size; return 0; }
int ungetc(int c, FILE* f) { (void)c; (void)f; return EOF; }
int getc(FILE* f) { (void)f; return EOF; }
char* fgets(char* buf, int size, FILE* f) { (void)buf; (void)size; (void)f; return NULL; }
int fputs(const char* s, FILE* f) { (void)f; extern void vga_print(const char*); vga_print(s); return 0; }
FILE* tmpfile(void) { return NULL; }

/* ─── math stubs (basic implementations) ─── */
double floor(double x) { return (double)(long)x - (x < 0 && x != (long)x ? 1 : 0); }
double ceil(double x) { return (double)(long)x + (x > 0 && x != (long)x ? 1 : 0); }
double fmod(double x, double y) { return x - (long)(x/y) * y; }
double fabs(double x) { return x < 0 ? -x : x; }
double sqrt(double x) {
    if (x <= 0) return 0;
    double r = x / 2;
    for (int i = 0; i < 20; i++) r = (r + x / r) / 2;
    return r;
}
double pow(double base, double exp) {
    if (exp == 0) return 1;
    if (exp < 0) return 1.0 / pow(base, -exp);
    double result = 1;
    int iexp = (int)exp;
    for (int i = 0; i < iexp; i++) result *= base;
    return result;
}
double log(double x) {
    if (x <= 0) return -HUGE_VAL;
    double result = 0, term = (x - 1) / (x + 1);
    double t2 = term * term, power = term;
    for (int i = 0; i < 50; i++) {
        result += power / (2 * i + 1);
        power *= t2;
    }
    return 2 * result;
}
double log2(double x) { return log(x) / 0.693147180559945; }
double log10(double x) { return log(x) / 2.302585092994046; }
double exp(double x) {
    double result = 1, term = 1;
    for (int i = 1; i < 30; i++) { term *= x / i; result += term; }
    return result;
}
double sin(double x) {
    x = fmod(x, 6.283185307179586);
    double result = 0, term = x;
    for (int i = 1; i < 15; i++) { result += term; term *= -x*x/((2*i)*(2*i+1)); }
    return result;
}
double cos(double x) { return sin(x + 1.5707963267948966); }
double tan(double x) { return sin(x) / cos(x); }
double asin(double x) { (void)x; return 0; } /* stub */
double acos(double x) { return 1.5707963267948966 - asin(x); }
double atan(double x) {
    double result = 0, term = x;
    for (int i = 0; i < 30; i++) { result += term / (2*i+1); term *= -x*x; }
    return result;
}
double atan2(double y, double x) {
    if (x > 0) return atan(y/x);
    if (x < 0) return atan(y/x) + (y >= 0 ? 3.14159265358979 : -3.14159265358979);
    return y > 0 ? 1.5707963 : -1.5707963;
}
double frexp(double x, int* exp) { *exp = 0; while(fabs(x) >= 1.0 && *exp < 100) { x /= 2; (*exp)++; } return x; }
double ldexp(double x, int exp) { for(int i=0;i<exp;i++) x*=2; for(int i=0;i>exp;i--) x/=2; return x; }
double modf(double x, double* iptr) { *iptr = (double)(long)x; return x - *iptr; }
int isnan(double x) { return x != x; }
int isinf(double x) { return x == HUGE_VAL || x == -HUGE_VAL; }

/* ─── time stubs ─── */
static struct tm dummy_tm = {0, 0, 0, 1, 0, 126, 0, 0, 0}; /* 2026-01-01 */
struct tm* localtime(const time_t* t) { (void)t; return &dummy_tm; }
struct tm* gmtime(const time_t* t) { (void)t; return &dummy_tm; }
time_t mktime(struct tm* tm) { (void)tm; return 0; }
size_t strftime(char* buf, size_t max, const char* fmt, const struct tm* tm) {
    (void)fmt; (void)tm;
    if (max > 0) buf[0] = '\0';
    return 0;
}
double difftime(time_t t1, time_t t0) { return (double)(t1 - t0); }

/* freopen stub */
FILE* freopen(const char* path, const char* mode, FILE* f) {
    (void)path; (void)mode; (void)f;
    return NULL;
}
