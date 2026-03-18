/*
 * CLAOS — Claude Assisted Operating System
 * dns.h — DNS Resolver
 */

#ifndef CLAOS_DNS_H
#define CLAOS_DNS_H

#include "types.h"

/* Initialize the DNS subsystem */
void dns_init(void);

/* Resolve a hostname to an IPv4 address.
 * Returns the IP in host byte order, or 0 on failure.
 * Blocks until resolved or timeout. */
uint32_t dns_resolve(const char* hostname);

#endif /* CLAOS_DNS_H */
