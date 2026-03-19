/*
 * CLAOS — Claude Assisted Operating System
 * string.c — Freestanding string/memory utility implementations
 *
 * These replace libc functions we can't use in a freestanding kernel.
 * GCC may implicitly call memcpy/memset for struct copies and zero-init,
 * so these MUST exist even if we never call them directly.
 */

#include "string.h"

/* Copy n bytes from src to dest. Regions must not overlap.
 * Uses rep movsl for bulk 32-bit transfers, ~4x faster than byte loop. */
void* memcpy(void* dest, const void* src, size_t n) {
    void* ret = dest;
    size_t dwords = n / 4;
    size_t bytes = n % 4;

    /* Bulk copy 4 bytes at a time */
    if (dwords) {
        __asm__ volatile (
            "rep movsl"
            : "+D"(dest), "+S"(src), "+c"(dwords)
            :: "memory"
        );
    }
    /* Copy remaining bytes */
    if (bytes) {
        __asm__ volatile (
            "rep movsb"
            : "+D"(dest), "+S"(src), "+c"(bytes)
            :: "memory"
        );
    }
    return ret;
}

/* Fill n bytes of dest with the byte value val.
 * Uses rep stosl when filling with a uniform byte (common case). */
void* memset(void* dest, int val, size_t n) {
    void* ret = dest;
    uint8_t byte = (uint8_t)val;

    if (n >= 4) {
        /* Broadcast byte to all 4 positions in a dword */
        uint32_t fill = (uint32_t)byte | ((uint32_t)byte << 8) |
                        ((uint32_t)byte << 16) | ((uint32_t)byte << 24);
        size_t dwords = n / 4;
        size_t remain = n % 4;

        __asm__ volatile (
            "rep stosl"
            : "+D"(dest), "+c"(dwords)
            : "a"(fill)
            : "memory"
        );
        n = remain;
    }
    /* Fill remaining bytes */
    if (n) {
        __asm__ volatile (
            "rep stosb"
            : "+D"(dest), "+c"(n)
            : "a"(byte)
            : "memory"
        );
    }
    return ret;
}

/* Copy n bytes, handling overlapping regions correctly.
 * Forward copy if dest < src, backward copy otherwise. */
void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        /* Forward — safe to use optimized memcpy */
        memcpy(dest, src, n);
    } else if (d > s) {
        /* Backward copy — start from end of buffers */
        d += n - 1;
        s += n - 1;
        size_t count = n;
        __asm__ volatile (
            "std\n\t"
            "rep movsb\n\t"
            "cld"
            : "+S"(s), "+D"(d), "+c"(count)
            :: "memory"
        );
    }
    return dest;
}

/* Compare n bytes of memory. Returns 0 if equal. */
int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return pa[i] - pb[i];
    }
    return 0;
}

/* Return the length of a null-terminated string. */
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

/* Compare two null-terminated strings. */
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *(unsigned char*)a - *(unsigned char*)b;
}

/* Compare at most n characters of two strings. */
int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0')
            return 0;
    }
    return 0;
}

/* Copy a null-terminated string. */
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

/* Copy at most n characters, padding with nulls. */
char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

/* Append src to the end of dest. */
char* strcat(char* dest, const char* src) {
    char* d = dest + strlen(dest);
    while ((*d++ = *src++))
        ;
    return dest;
}

/* Find first occurrence of needle in haystack */
char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0)
            return (char*)haystack;
        haystack++;
    }
    return NULL;
}

/* Find first occurrence of character c in string s */
char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

/* Find last occurrence of character c in string s */
char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == 0) ? (char*)s : (char*)last;
}

/* Find first char in s that's in accept */
char* strpbrk(const char* s, const char* accept) {
    while (*s) {
        for (const char* a = accept; *a; a++)
            if (*s == *a) return (char*)s;
        s++;
    }
    return NULL;
}

/* Length of prefix of s consisting of chars in accept */
size_t strspn(const char* s, const char* accept) {
    size_t count = 0;
    while (*s) {
        const char* a = accept;
        while (*a && *a != *s) a++;
        if (!*a) break;
        count++; s++;
    }
    return count;
}

/* Length of prefix of s consisting of chars NOT in reject */
size_t strcspn(const char* s, const char* reject) {
    size_t count = 0;
    while (*s) {
        for (const char* r = reject; *r; r++)
            if (*s == *r) return count;
        count++; s++;
    }
    return count;
}

/* Return error string for errno value */
char* strerror(int errnum) {
    (void)errnum;
    return "error";
}

/* Find first occurrence of byte c in first n bytes of s */
void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void*)(p + i);
    }
    return NULL;
}

/* strcoll — locale-aware string comparison. We just use strcmp. */
int strcoll(const char* a, const char* b) {
    return strcmp(a, b);
}
