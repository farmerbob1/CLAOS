/*
 * CLAOS — Minimal time.h stub
 */
#ifndef CLAOS_TIME_H
#define CLAOS_TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 100  /* Our PIT runs at 100Hz */

struct tm {
    int tm_sec, tm_min, tm_hour;
    int tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

clock_t clock(void);
time_t time(time_t* t);
struct tm* localtime(const time_t* t);
struct tm* gmtime(const time_t* t);
time_t mktime(struct tm* tm);
size_t strftime(char* buf, size_t max, const char* fmt, const struct tm* tm);
double difftime(time_t t1, time_t t0);

#endif
