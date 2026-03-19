/*
 * CLAOS — Claude Assisted Operating System
 * pci.c — PCI Bus Enumeration
 *
 * PCI devices are accessed via two I/O ports:
 *   0xCF8 (CONFIG_ADDRESS): selects the bus/device/function/register
 *   0xCFC (CONFIG_DATA): reads/writes the selected register
 *
 * We scan all 256 buses, 32 devices per bus, 8 functions per device
 * to find hardware by vendor/device ID.
 */

#include "pci.h"
#include "io.h"
#include "vga.h"

/* Build the 32-bit address for PCI configuration space access */
static uint32_t pci_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(
        ((uint32_t)1 << 31) |           /* Enable bit */
        ((uint32_t)bus << 16) |
        ((uint32_t)(slot & 0x1F) << 11) |
        ((uint32_t)(func & 0x07) << 8) |
        ((uint32_t)(offset & 0xFC))      /* Align to 4 bytes */
    );
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read32(bus, slot, func, offset & 0xFC);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDR, pci_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* dev) {
    /* Scan PCI buses — skip empty buses early for speed */
    for (uint16_t bus = 0; bus < 256; bus++) {
        /* Quick check: if slot 0, func 0 has no device, skip entire bus */
        uint16_t bus_check = pci_read16(bus, 0, 0, PCI_VENDOR_ID);
        if (bus_check == 0xFFFF)
            continue;

        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vid = pci_read16(bus, slot, func, PCI_VENDOR_ID);
                if (vid == 0xFFFF) {
                    if (func == 0) break;  /* Empty slot — skip all functions */
                    continue;
                }

                uint16_t did = pci_read16(bus, slot, func, PCI_DEVICE_ID);

                if (vid == vendor_id && did == device_id) {
                    dev->bus = bus;
                    dev->slot = slot;
                    dev->func = func;
                    dev->vendor_id = vid;
                    dev->device_id = did;
                    dev->class_code = (uint8_t)(pci_read32(bus, slot, func, 0x08) >> 24);
                    dev->subclass = pci_read16(bus, slot, func, PCI_SUBCLASS) & 0xFF;
                    dev->irq = pci_read16(bus, slot, func, PCI_INTERRUPT_LINE) & 0xFF;

                    /* Read all 6 BARs */
                    for (int i = 0; i < 6; i++) {
                        dev->bar[i] = pci_read32(bus, slot, func, PCI_BAR0 + i * 4);
                    }

                    return true;
                }

                /* If this is not a multi-function device, skip other functions */
                if (func == 0) {
                    uint8_t header_type = pci_read16(bus, slot, func, PCI_HEADER_TYPE) & 0xFF;
                    if (!(header_type & 0x80))
                        break;
                }
            }
        }
    }
    return false;
}

void pci_enable_bus_master(struct pci_device* dev) {
    uint32_t cmd = pci_read32(dev->bus, dev->slot, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER | PCI_CMD_MEMORY_SPACE | PCI_CMD_IO_SPACE;
    pci_write32(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}
