/*
 * CLAOS — Claude Assisted Operating System
 * pci.h — PCI Bus Enumeration
 *
 * The PCI bus connects hardware devices. We scan it to find the e1000
 * NIC by its vendor/device ID and read its Base Address Registers (BARs)
 * to find the memory-mapped I/O regions.
 */

#ifndef CLAOS_PCI_H
#define CLAOS_PCI_H

#include "types.h"

/* PCI configuration space I/O ports */
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

/* PCI configuration register offsets */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_CLASS           0x0B
#define PCI_SUBCLASS        0x0A
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C

/* PCI command register bits */
#define PCI_CMD_IO_SPACE     0x0001
#define PCI_CMD_MEMORY_SPACE 0x0002
#define PCI_CMD_BUS_MASTER   0x0004

/* PCI device info */
struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  irq;
    uint32_t bar[6];
};

/* Read a 32-bit value from PCI configuration space */
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/* Read a 16-bit value from PCI configuration space */
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/* Write a 32-bit value to PCI configuration space */
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/* Find a PCI device by vendor and device ID. Returns true if found. */
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* dev);

/* Enable bus mastering for a PCI device (required for DMA) */
void pci_enable_bus_master(struct pci_device* dev);

#endif /* CLAOS_PCI_H */
