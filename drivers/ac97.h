#ifndef AC97_H
#define AC97_H

#include <stdint.h>
#include <stdbool.h>

/* ── PCI identification ──────────────────────────────────────────── */
#define AC97_VENDOR_ID      0x8086
#define AC97_DEVICE_ID      0x2415  /* Intel 82801AA (QEMU AC97) */

/* ── NAM (Native Audio Mixer) register offsets from BAR0 ─────── */
#define AC97_NAM_RESET          0x00  /* Write any value to reset codec */
#define AC97_NAM_MASTER_VOL     0x02  /* Master output volume */
#define AC97_NAM_AUX_VOL        0x04  /* Aux/headphone volume */
#define AC97_NAM_PCM_VOL        0x18  /* PCM output volume */
#define AC97_NAM_EXT_AUDIO_ID   0x28  /* Extended audio ID (read-only) */
#define AC97_NAM_EXT_AUDIO_CTRL 0x2A  /* Extended audio control */
#define AC97_NAM_PCM_RATE       0x2C  /* PCM front DAC sample rate */

/* ── NABM (Native Audio Bus Master) register offsets from BAR1 ── */

/* PCM OUT channel registers (base 0x10) */
#define AC97_PO_BDBAR       0x10  /* BDL base address (dword) */
#define AC97_PO_CIV         0x14  /* Current index value (byte) */
#define AC97_PO_LVI         0x15  /* Last valid index (byte) */
#define AC97_PO_SR          0x16  /* Status register (word) */
#define AC97_PO_PICB        0x18  /* Position in current buffer (word) */
#define AC97_PO_PIV         0x1A  /* Prefetched index value (byte) */
#define AC97_PO_CR          0x1B  /* Control register (byte) */

/* Global registers */
#define AC97_GLOB_CNT       0x2C  /* Global control (dword) */
#define AC97_GLOB_STA       0x30  /* Global status (dword) */

/* ── Status register bits (AC97_PO_SR) ──────────────────────────── */
#define AC97_SR_DCH         (1 << 0)  /* DMA controller halted */
#define AC97_SR_CELV        (1 << 1)  /* Current equals last valid */
#define AC97_SR_LVBCI       (1 << 2)  /* Last valid buffer completion int */
#define AC97_SR_BCIS        (1 << 3)  /* Buffer completion interrupt status */
#define AC97_SR_FIFOE       (1 << 4)  /* FIFO error */

/* ── Control register bits (AC97_PO_CR) ─────────────────────────── */
#define AC97_CR_RPBM        (1 << 0)  /* Run/pause bus master */
#define AC97_CR_RR          (1 << 1)  /* Reset registers */
#define AC97_CR_LVBIE       (1 << 2)  /* Last valid buffer int enable */
#define AC97_CR_FEIE        (1 << 3)  /* FIFO error int enable */
#define AC97_CR_IOCE        (1 << 4)  /* Interrupt on completion enable */

/* ── Global control bits (AC97_GLOB_CNT) ────────────────────────── */
#define AC97_GC_GIE         (1 << 0)  /* Global interrupt enable */
#define AC97_GC_CR          (1 << 1)  /* Cold reset (0=reset, 1=resume) */

/* ── Buffer Descriptor List ─────────────────────────────────────── */
#define AC97_BDL_COUNT      32
#define AC97_BDL_BUF_SIZE   4096      /* 4KB per buffer */

#define AC97_BDL_IOC        (1 << 15) /* Interrupt on completion */
#define AC97_BDL_BUP        (1 << 14) /* Buffer underrun policy (last) */

struct ac97_bdl_entry {
    uint32_t addr;      /* Physical address of PCM data */
    uint16_t samples;   /* Number of samples in buffer */
    uint16_t flags;     /* BDL_IOC, BDL_BUP */
} __attribute__((packed));

/* ── Public API ─────────────────────────────────────────────────── */
bool ac97_init(void);
void ac97_play_tone(uint32_t freq_hz, uint32_t duration_ms);
void ac97_stop(void);
void ac97_set_volume(int level);    /* 0-100 */
int  ac97_get_volume(void);         /* returns 0-100 */
bool ac97_is_playing(void);

#endif /* AC97_H */
