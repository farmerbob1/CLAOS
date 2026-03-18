/*
 * CLAOS — Minimal signal.h stub
 */
#ifndef CLAOS_SIGNAL_H
#define CLAOS_SIGNAL_H

#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)
#define SIG_ERR ((void(*)(int))-1)
#define SIGINT   2
#define SIGABRT  6

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t handler);

#endif
