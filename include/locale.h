/*
 * CLAOS — Minimal locale.h stub
 */
#ifndef CLAOS_LOCALE_H
#define CLAOS_LOCALE_H

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_NUMERIC  4
#define LC_MONETARY 5
#define LC_TIME     6

struct lconv {
    char* decimal_point;
};

struct lconv* localeconv(void);
char* setlocale(int category, const char* locale);

#endif
