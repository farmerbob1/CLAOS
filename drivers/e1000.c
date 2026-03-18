/*
 * CLAOS — Claude Assisted Operating System
 * e1000.c — Intel e1000 NIC Driver
 *
 * Drives the Intel 82540EM NIC (QEMU's default with -device e1000).
 * Uses memory-mapped I/O for register access and DMA descriptor rings
 * for sending/receiving Ethernet frames.
 *
 * The e1000 uses ring buffers (circular arrays) of descriptors.
 * Each descriptor points to a data buffer. The NIC reads TX descriptors
 * to find packets to send, and writes received packets into RX buffers.
 */

#include "e1000.h"
#include "pci.h"
#include "io.h"
#include "string.h"
#include "vga.h"
#include "net.h"

/* TX descriptor (legacy format) */
struct e1000_tx_desc {
    uint64_t addr;       /* Buffer physical address */
    uint16_t length;     /* Data length */
    uint8_t  cso;        /* Checksum offset */
    uint8_t  cmd;        /* Command field */
    uint8_t  status;     /* Status (DD bit) */
    uint8_t  css;        /* Checksum start */
    uint16_t special;
} __attribute__((packed));

/* RX descriptor (legacy format) */
struct e1000_rx_desc {
    uint64_t addr;       /* Buffer physical address */
    uint16_t length;     /* Received data length */
    uint16_t checksum;
    uint8_t  status;     /* Status (DD, EOP bits) */
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

/* MMIO base address */
static volatile uint32_t* mmio_base = NULL;

/* Descriptor rings — aligned to 16 bytes as required by hardware */
static struct e1000_tx_desc tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));

/* Packet buffers for RX descriptors */
static uint8_t rx_buffers[E1000_NUM_RX_DESC][E1000_RX_BUFFER_SIZE] __attribute__((aligned(16)));

/* Packet buffer for TX */
static uint8_t tx_buffer[MAX_PACKET_SIZE] __attribute__((aligned(16)));

/* Current descriptor indices */
static uint32_t tx_cur = 0;
static uint32_t rx_cur = 0;

/* Our MAC address */
static uint8_t mac_addr[6];

/* Read an e1000 register via MMIO */
static uint32_t e1000_read(uint32_t reg) {
    return mmio_base[reg / 4];
}

/* Write an e1000 register via MMIO */
static void e1000_write(uint32_t reg, uint32_t value) {
    mmio_base[reg / 4] = value;
}

/* Read the MAC address from the NIC's EEPROM */
static void e1000_read_mac(void) {
    /* Try reading from RAL/RAH first (already set by QEMU) */
    uint32_t ral = e1000_read(E1000_RAL);
    uint32_t rah = e1000_read(E1000_RAH);

    mac_addr[0] = ral & 0xFF;
    mac_addr[1] = (ral >> 8) & 0xFF;
    mac_addr[2] = (ral >> 16) & 0xFF;
    mac_addr[3] = (ral >> 24) & 0xFF;
    mac_addr[4] = rah & 0xFF;
    mac_addr[5] = (rah >> 8) & 0xFF;
}

/* Set up the receive descriptor ring */
static void e1000_init_rx(void) {
    /* Point each RX descriptor to its buffer */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].addr = (uint64_t)(uint32_t)&rx_buffers[i];
        rx_descs[i].status = 0;
    }

    /* Tell the NIC where the RX ring is */
    e1000_write(E1000_RDBAL, (uint32_t)&rx_descs);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_NUM_RX_DESC - 1);

    /* Enable receiver: accept broadcast, 2048-byte buffers, strip CRC */
    e1000_write(E1000_RCTL,
        E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC);

    rx_cur = 0;
}

/* Set up the transmit descriptor ring */
static void e1000_init_tx(void) {
    memset(tx_descs, 0, sizeof(tx_descs));

    e1000_write(E1000_TDBAL, (uint32_t)&tx_descs);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);

    /* Enable transmitter: pad short packets, set collision parameters */
    e1000_write(E1000_TCTL,
        E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD);

    tx_cur = 0;
}

bool e1000_init(void) {
    /* Find the e1000 on the PCI bus */
    struct pci_device dev;
    if (!pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID, &dev)) {
        serial_print("[E1000] NIC not found on PCI bus!\n");
        return false;
    }

    serial_print("[E1000] Found NIC on PCI bus\n");

    /* Enable bus mastering (required for DMA) */
    pci_enable_bus_master(&dev);

    /* Get MMIO base from BAR0 (mask off the lower bits) */
    mmio_base = (volatile uint32_t*)(dev.bar[0] & 0xFFFFFFF0);

    /* Reset the NIC */
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | E1000_CTRL_RST);

    /* Wait for reset to complete (busy wait ~10ms) */
    for (volatile int i = 0; i < 100000; i++);

    /* Set link up */
    e1000_write(E1000_CTRL,
        e1000_read(E1000_CTRL) | E1000_CTRL_SLU | E1000_CTRL_ASDE);

    /* Disable all interrupts (we use polling) */
    e1000_write(E1000_IMC, 0xFFFFFFFF);

    /* Clear any pending interrupts */
    e1000_read(E1000_ICR);

    /* Clear the multicast table */
    for (int i = 0; i < 128; i++) {
        e1000_write(E1000_MTA + i * 4, 0);
    }

    /* Read MAC address */
    e1000_read_mac();

    /* Initialize descriptor rings */
    e1000_init_rx();
    e1000_init_tx();

    serial_print("[E1000] Initialized, MAC=");
    /* Print MAC to serial for debugging */
    for (int i = 0; i < 6; i++) {
        if (i > 0) serial_putchar(':');
        char hex[] = "0123456789ABCDEF";
        serial_putchar(hex[mac_addr[i] >> 4]);
        serial_putchar(hex[mac_addr[i] & 0xF]);
    }
    serial_putchar('\n');

    return true;
}

bool e1000_send(const void* data, uint16_t length) {
    if (length > MAX_PACKET_SIZE) return false;

    /* Copy data to TX buffer */
    memcpy(tx_buffer, data, length);

    /* Set up the TX descriptor */
    tx_descs[tx_cur].addr = (uint64_t)(uint32_t)tx_buffer;
    tx_descs[tx_cur].length = length;
    tx_descs[tx_cur].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_descs[tx_cur].status = 0;

    /* Tell the NIC there's a new packet to send */
    uint32_t old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write(E1000_TDT, tx_cur);

    /* Wait for the NIC to finish sending (poll the DD bit) */
    int timeout = 100000;
    while (!(tx_descs[old_cur].status & E1000_TXD_STAT_DD) && timeout > 0) {
        timeout--;
    }

    return timeout > 0;
}

uint16_t e1000_receive(void* buf, uint16_t buf_size) {
    /* Check if the current RX descriptor has a packet (DD bit set) */
    if (!(rx_descs[rx_cur].status & E1000_RXD_STAT_DD))
        return 0;   /* No packet available */

    /* Get the packet length */
    uint16_t length = rx_descs[rx_cur].length;
    if (length > buf_size)
        length = buf_size;

    /* Copy the packet data */
    memcpy(buf, rx_buffers[rx_cur], length);

    /* Reset this descriptor and give it back to the NIC */
    rx_descs[rx_cur].status = 0;
    uint32_t old_cur = rx_cur;
    rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;

    /* Advance the tail pointer so the NIC can reuse this descriptor */
    e1000_write(E1000_RDT, old_cur);

    return length;
}

void e1000_get_mac(uint8_t mac[6]) {
    memcpy(mac, mac_addr, 6);
}

bool e1000_link_up(void) {
    return (e1000_read(E1000_STATUS) & 2) != 0;
}
