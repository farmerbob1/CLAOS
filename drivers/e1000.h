/*
 * CLAOS — Claude Assisted Operating System
 * e1000.h — Intel e1000 NIC Driver Interface
 *
 * Drives the Intel 82540EM, the default NIC emulated by QEMU
 * when using `-device e1000`. Uses memory-mapped I/O (MMIO).
 */

#ifndef CLAOS_E1000_H
#define CLAOS_E1000_H

#include "types.h"

/* Intel vendor/device IDs */
#define E1000_VENDOR_ID  0x8086
#define E1000_DEVICE_ID  0x100E      /* 82540EM */

/* e1000 register offsets (from MMIO base) */
#define E1000_CTRL      0x0000       /* Device Control */
#define E1000_STATUS    0x0008       /* Device Status */
#define E1000_EERD      0x0014       /* EEPROM Read */
#define E1000_ICR       0x00C0       /* Interrupt Cause Read */
#define E1000_IMS       0x00D0       /* Interrupt Mask Set */
#define E1000_IMC       0x00D8       /* Interrupt Mask Clear */
#define E1000_RCTL      0x0100       /* Receive Control */
#define E1000_TCTL      0x0400       /* Transmit Control */
#define E1000_RDBAL     0x2800       /* RX Descriptor Base Low */
#define E1000_RDBAH     0x2804       /* RX Descriptor Base High */
#define E1000_RDLEN     0x2808       /* RX Descriptor Length */
#define E1000_RDH       0x2810       /* RX Descriptor Head */
#define E1000_RDT       0x2818       /* RX Descriptor Tail */
#define E1000_TDBAL     0x3800       /* TX Descriptor Base Low */
#define E1000_TDBAH     0x3804       /* TX Descriptor Base High */
#define E1000_TDLEN     0x3808       /* TX Descriptor Length */
#define E1000_TDH       0x3810       /* TX Descriptor Head */
#define E1000_TDT       0x3818       /* TX Descriptor Tail */
#define E1000_RAL       0x5400       /* Receive Address Low */
#define E1000_RAH       0x5404       /* Receive Address High */
#define E1000_MTA       0x5200       /* Multicast Table Array */

/* Control register bits */
#define E1000_CTRL_RST   (1 << 26)   /* Device Reset */
#define E1000_CTRL_SLU   (1 << 6)    /* Set Link Up */
#define E1000_CTRL_ASDE  (1 << 5)    /* Auto-Speed Detection Enable */

/* Receive control bits */
#define E1000_RCTL_EN    (1 << 1)    /* Receiver Enable */
#define E1000_RCTL_BAM   (1 << 15)   /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048 (0 << 16)
#define E1000_RCTL_SECRC (1 << 26)   /* Strip Ethernet CRC */

/* Transmit control bits */
#define E1000_TCTL_EN    (1 << 1)    /* Transmitter Enable */
#define E1000_TCTL_PSP   (1 << 3)    /* Pad Short Packets */
#define E1000_TCTL_CT    (0x10 << 4) /* Collision Threshold */
#define E1000_TCTL_COLD  (0x40 << 12)/* Collision Distance */

/* TX descriptor command bits */
#define E1000_TXD_CMD_EOP  (1 << 0)  /* End Of Packet */
#define E1000_TXD_CMD_IFCS (1 << 1)  /* Insert FCS (CRC) */
#define E1000_TXD_CMD_RS   (1 << 3)  /* Report Status */

/* TX descriptor status bits */
#define E1000_TXD_STAT_DD  (1 << 0)  /* Descriptor Done */

/* RX descriptor status bits */
#define E1000_RXD_STAT_DD  (1 << 0)  /* Descriptor Done */
#define E1000_RXD_STAT_EOP (1 << 1)  /* End Of Packet */

/* Number of descriptors in each ring */
#define E1000_NUM_RX_DESC  32
#define E1000_NUM_TX_DESC  32

/* RX buffer size */
#define E1000_RX_BUFFER_SIZE 2048

/* Initialize the e1000 NIC. Returns true on success. */
bool e1000_init(void);

/* Send a raw Ethernet frame. Returns true on success. */
bool e1000_send(const void* data, uint16_t length);

/* Receive a raw Ethernet frame. Returns length, or 0 if no packet.
 * buf must be at least MAX_PACKET_SIZE bytes. */
uint16_t e1000_receive(void* buf, uint16_t buf_size);

/* Get the NIC's MAC address */
void e1000_get_mac(uint8_t mac[6]);

/* Check if the link is up */
bool e1000_link_up(void);

#endif /* CLAOS_E1000_H */
