/*
 * CLAOS — Minimal math.h stub
 * Lua's math library needs these. We provide basic implementations.
 */
#ifndef CLAOS_MATH_H
#define CLAOS_MATH_H

#define HUGE_VAL (__builtin_huge_val())
#define NAN      (__builtin_nanf(""))
#define INFINITY (__builtin_inff())

double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double pow(double base, double exp);
double sqrt(double x);
double fabs(double x);
double log(double x);
double log2(double x);
double log10(double x);
double exp(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double frexp(double x, int* exp);
double ldexp(double x, int exp);
double modf(double x, double* iptr);
int isnan(double x);
int isinf(double x);

#endif
