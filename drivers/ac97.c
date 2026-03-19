/*
 * AC97 Audio Codec Driver for CLAOS
 *
 * Supports Intel 82801AA (QEMU's default AC97 emulation).
 * Provides tone generation via DMA ring buffer with a 256-entry
 * sine lookup table — no floating point needed.
 */

#include "ac97.h"
#include "pci.h"
#include "../kernel/idt.h"
#include "../include/io.h"
#include <string.h>

/* serial_print is provided by include/io.h as static inline */

/* ── State ───────────────────────────────────────────────────────── */

static uint16_t nam_base;       /* BAR0 I/O port base (mixer) */
static uint16_t nabm_base;      /* BAR1 I/O port base (bus master) */
static bool     ac97_present = false;
static volatile bool playing = false;
static int      volume_pct = 80;
static uint32_t sample_rate = 48000;

/* Tone generation */
static volatile uint32_t tone_freq;
static volatile uint32_t tone_phase;       /* 24.8 fixed-point index into sine table */
static volatile int      tone_bufs_left;   /* buffers remaining to generate */

/* DMA structures — statically allocated, identity-mapped */
static struct ac97_bdl_entry bdl[AC97_BDL_COUNT] __attribute__((aligned(32)));
static uint8_t audio_bufs[AC97_BDL_COUNT][AC97_BDL_BUF_SIZE] __attribute__((aligned(4096)));

/* ── Precomputed sine table (256 entries, one full cycle) ──────── */
/* Values: sin(2*pi*i/256) * 32767, computed offline */
static const int16_t sine_table[256] = {
         0,    804,   1608,   2410,   3212,   4011,   4808,   5602,
      6393,   7179,   7962,   8739,   9512,  10278,  11039,  11793,
     12539,  13279,  14010,  14732,  15446,  16151,  16846,  17530,
     18204,  18868,  19519,  20159,  20787,  21403,  22005,  22594,
     23170,  23731,  24279,  24811,  25329,  25832,  26319,  26790,
     27245,  27683,  28105,  28510,  28898,  29268,  29621,  29956,
     30273,  30571,  30852,  31113,  31356,  31580,  31785,  31971,
     32137,  32285,  32412,  32521,  32609,  32678,  32728,  32757,
     32767,  32757,  32728,  32678,  32609,  32521,  32412,  32285,
     32137,  31971,  31785,  31580,  31356,  31113,  30852,  30571,
     30273,  29956,  29621,  29268,  28898,  28510,  28105,  27683,
     27245,  26790,  26319,  25832,  25329,  24811,  24279,  23731,
     23170,  22594,  22005,  21403,  20787,  20159,  19519,  18868,
     18204,  17530,  16846,  16151,  15446,  14732,  14010,  13279,
     12539,  11793,  11039,  10278,   9512,   8739,   7962,   7179,
      6393,   5602,   4808,   4011,   3212,   2410,   1608,    804,
         0,   -804,  -1608,  -2410,  -3212,  -4011,  -4808,  -5602,
     -6393,  -7179,  -7962,  -8739,  -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530,
    -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971,
    -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285,
    -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868,
    -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278,  -9512,  -8739,  -7962,  -7179,
     -6393,  -5602,  -4808,  -4011,  -3212,  -2410,  -1608,   -804,
};

/* ── Helpers ─────────────────────────────────────────────────────── */

static inline void nam_writew(uint16_t reg, uint16_t val) {
    outw(nam_base + reg, val);
}

static inline uint16_t nam_readw(uint16_t reg) {
    return inw(nam_base + reg);
}

static inline void nabm_writeb(uint16_t reg, uint8_t val) {
    outb(nabm_base + reg, val);
}

static inline uint8_t nabm_readb(uint16_t reg) {
    return inb(nabm_base + reg);
}

static inline void nabm_writew(uint16_t reg, uint16_t val) {
    outw(nabm_base + reg, val);
}

static inline uint16_t nabm_readw(uint16_t reg) {
    return inw(nabm_base + reg);
}

static inline void nabm_writel(uint16_t reg, uint32_t val) {
    outl(nabm_base + reg, val);
}

/* ── Fill a buffer with sine wave tone data ──────────────────── */

static void fill_tone_buffer(int idx) {
    int16_t *samples = (int16_t *)audio_bufs[idx];
    uint32_t num_frames = AC97_BDL_BUF_SIZE / 4;  /* 4 bytes per stereo frame */

    /*
     * Phase step: how many 256ths of a cycle to advance per sample.
     * Using 24.8 fixed point: phase_step = freq * 256 * 256 / sample_rate
     * To avoid overflow for freq up to 20kHz: compute in two parts.
     */
    uint32_t freq = tone_freq;
    uint32_t step = (freq * 256) / (sample_rate / 256);

    uint32_t phase = tone_phase;

    for (uint32_t i = 0; i < num_frames; i++) {
        uint8_t table_idx = (phase >> 8) & 0xFF;
        int16_t val = sine_table[table_idx];
        samples[i * 2]     = val;   /* Left */
        samples[i * 2 + 1] = val;   /* Right */
        phase += step;
    }

    tone_phase = phase;
}

/* ── Fill a buffer with silence ──────────────────────────────── */

static void fill_silence(int idx) {
    memset(audio_bufs[idx], 0, AC97_BDL_BUF_SIZE);
}

/* ── IRQ handler ─────────────────────────────────────────────── */

static void ac97_irq_handler(struct registers *regs) {
    (void)regs;

    uint16_t sr = nabm_readw(AC97_PO_SR);
    if (!(sr & 0x1C)) return;  /* Not our interrupt */

    if (sr & AC97_SR_BCIS) {
        /* Buffer completed — refill the next one if tone is active */
        if (tone_bufs_left > 0) {
            uint8_t civ = nabm_readb(AC97_PO_CIV);
            int fill_idx = (civ + AC97_BDL_COUNT - 1) & (AC97_BDL_COUNT - 1);
            fill_tone_buffer(fill_idx);
            tone_bufs_left--;

            /* Advance LVI to keep DMA running */
            uint8_t new_lvi = (civ + AC97_BDL_COUNT - 2) & (AC97_BDL_COUNT - 1);
            nabm_writeb(AC97_PO_LVI, new_lvi);
        }
    }

    if (sr & AC97_SR_LVBCI) {
        /* Last valid buffer reached — playback finished */
        playing = false;
        /* Stop DMA and disable interrupts to prevent IRQ storm */
        nabm_writeb(AC97_PO_CR, 0);
    }

    /* Acknowledge all interrupt bits (write-1-to-clear) */
    nabm_writew(AC97_PO_SR, 0x1C);
}

/* ── Public API ──────────────────────────────────────────────── */

bool ac97_init(void) {
    struct pci_device dev;

    if (!pci_find_device(AC97_VENDOR_ID, AC97_DEVICE_ID, &dev)) {
        serial_print("[AC97] PCI device not found\n");
        return false;
    }

    serial_print("[AC97] Found on PCI bus\n");

    /* Enable I/O space + bus mastering */
    pci_enable_bus_master(&dev);

    /* Extract I/O port bases (bit 0 indicates I/O space — mask it off) */
    nam_base  = (uint16_t)(dev.bar[0] & ~1);
    nabm_base = (uint16_t)(dev.bar[1] & ~1);

    serial_print("[AC97] I/O bases configured\n");

    /* Register IRQ handler */
    if (dev.irq > 0 && dev.irq < 16) {
        register_interrupt_handler(32 + dev.irq, ac97_irq_handler);
        serial_print("[AC97] IRQ handler registered\n");
    }

    /* Cold reset release — then poll for codec ready */
    nabm_writel(AC97_GLOB_CNT, AC97_GC_CR);
    {
        bool ready = false;
        for (int i = 0; i < 1000; i++) {
            uint32_t sta = inl(nabm_base + AC97_GLOB_STA);
            if (sta & (1 << 8)) {  /* Bit 8 = Primary Codec Ready */
                serial_print("[AC97] Codec ready\n");
                ready = true;
                break;
            }
            io_wait();
        }
        if (!ready) {
            serial_print("[AC97] Codec ready timeout (proceeding anyway)\n");
        }
    }

    /* Reset mixer */
    nam_writew(AC97_NAM_RESET, 0x0000);
    io_wait();

    /* Set master + PCM volume to maximum (0x0000 = no attenuation) */
    nam_writew(AC97_NAM_MASTER_VOL, 0x0000);
    nam_writew(AC97_NAM_PCM_VOL, 0x0808);   /* moderate default */

    /* Check for variable rate audio support */
    uint16_t ext_id = nam_readw(AC97_NAM_EXT_AUDIO_ID);
    if (ext_id & 0x0001) {
        /* Enable variable rate */
        uint16_t ext_ctrl = nam_readw(AC97_NAM_EXT_AUDIO_CTRL);
        nam_writew(AC97_NAM_EXT_AUDIO_CTRL, ext_ctrl | 0x0001);
        /* Set sample rate */
        nam_writew(AC97_NAM_PCM_RATE, 48000);
        sample_rate = 48000;
        serial_print("[AC97] Variable rate audio enabled, 48000 Hz\n");
    } else {
        sample_rate = 48000;  /* AC97 default */
        serial_print("[AC97] Fixed rate 48000 Hz\n");
    }

    /* Initialize BDL entries — each points to its corresponding buffer */
    for (int i = 0; i < AC97_BDL_COUNT; i++) {
        bdl[i].addr    = (uint32_t)audio_bufs[i];  /* identity-mapped phys addr */
        bdl[i].samples = AC97_BDL_BUF_SIZE / 2;    /* bytes / 2 = sample count (16-bit) */
        bdl[i].flags   = AC97_BDL_IOC;             /* interrupt on each buffer */
        fill_silence(i);
    }

    /* Reset PCM out channel — wait for reset completion */
    nabm_writeb(AC97_PO_CR, AC97_CR_RR);
    for (int i = 0; i < 10000; i++) {
        uint8_t cr = nabm_readb(AC97_PO_CR);
        if (!(cr & AC97_CR_RR)) break;
        io_wait();
    }
    for (int i = 0; i < 10000; i++) {
        uint16_t sr = nabm_readw(AC97_PO_SR);
        if (sr & AC97_SR_DCH) break;
        io_wait();
    }

    /* Write BDL physical address */
    nabm_writel(AC97_PO_BDBAR, (uint32_t)bdl);

    /* Apply default volume */
    ac97_set_volume(volume_pct);

    /* Enable global interrupts */
    nabm_writel(AC97_GLOB_CNT, AC97_GC_CR | AC97_GC_GIE);

    ac97_present = true;
    serial_print("[AC97] Initialization complete\n");
    return true;
}

void ac97_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (!ac97_present || freq_hz == 0 || duration_ms == 0) return;

    /* Stop any current playback */
    ac97_stop();

    /* Calculate how many buffers we need */
    uint32_t total_frames = (sample_rate * duration_ms) / 1000;
    uint32_t frames_per_buf = AC97_BDL_BUF_SIZE / 4;  /* stereo 16-bit = 4 bytes/frame */
    uint32_t bufs_needed = (total_frames + frames_per_buf - 1) / frames_per_buf;
    if (bufs_needed < 2) bufs_needed = 2;

    serial_print("[AC97] Playing tone\n");

    /* Set up tone state */
    tone_freq = freq_hz;
    tone_phase = 0;
    tone_bufs_left = (int)bufs_needed;

    /* Pre-fill all BDL buffers (or as many as needed) */
    int prefill = bufs_needed < AC97_BDL_COUNT ? (int)bufs_needed : AC97_BDL_COUNT;
    for (int i = 0; i < prefill; i++) {
        fill_tone_buffer(i);
        tone_bufs_left--;
    }

    /* Fill remaining slots with silence (in case ring wraps) */
    for (int i = prefill; i < AC97_BDL_COUNT; i++) {
        fill_silence(i);
    }

    /* Reset PCM out channel — wait for reset completion */
    nabm_writeb(AC97_PO_CR, AC97_CR_RR);
    for (int i = 0; i < 10000; i++) {
        uint8_t cr = nabm_readb(AC97_PO_CR);
        if (!(cr & AC97_CR_RR)) break;
        io_wait();
    }
    for (int i = 0; i < 10000; i++) {
        uint16_t sr = nabm_readw(AC97_PO_SR);
        if (sr & AC97_SR_DCH) break;
        io_wait();
    }

    /* Write BDL address */
    nabm_writel(AC97_PO_BDBAR, (uint32_t)bdl);

    /* Set last valid index */
    uint8_t lvi = (prefill < AC97_BDL_COUNT ? prefill : AC97_BDL_COUNT) - 1;
    nabm_writeb(AC97_PO_LVI, lvi);

    /* Start DMA with interrupts enabled */
    playing = true;
    nabm_writeb(AC97_PO_CR, AC97_CR_RPBM | AC97_CR_IOCE | AC97_CR_LVBIE);
}

void ac97_stop(void) {
    if (!ac97_present) return;

    /* Pause DMA */
    nabm_writeb(AC97_PO_CR, 0);
    /* Reset channel — wait for completion */
    nabm_writeb(AC97_PO_CR, AC97_CR_RR);
    for (int i = 0; i < 10000; i++) {
        uint8_t cr = nabm_readb(AC97_PO_CR);
        if (!(cr & AC97_CR_RR)) break;
        io_wait();
    }
    for (int i = 0; i < 10000; i++) {
        uint16_t sr = nabm_readw(AC97_PO_SR);
        if (sr & AC97_SR_DCH) break;
        io_wait();
    }

    playing = false;
    tone_bufs_left = 0;
    tone_phase = 0;
}

void ac97_set_volume(int level) {
    if (!ac97_present) return;

    if (level < 0)   level = 0;
    if (level > 100) level = 100;
    volume_pct = level;

    /*
     * AC97 volume: 0 = loudest, 63 = quietest (each step = ~1.5 dB)
     * Bits 5:0 = right, bits 13:8 = left, bit 15 = mute
     */
    if (level == 0) {
        nam_writew(AC97_NAM_MASTER_VOL, 0x8000);  /* Mute */
        nam_writew(AC97_NAM_PCM_VOL, 0x8000);
    } else {
        uint16_t atten = (uint16_t)(63 - (level * 63 / 100));
        uint16_t vol_reg = (atten << 8) | atten;
        nam_writew(AC97_NAM_MASTER_VOL, vol_reg);
        nam_writew(AC97_NAM_PCM_VOL, vol_reg);
    }
}

int ac97_get_volume(void) {
    return volume_pct;
}

bool ac97_is_playing(void) {
    return playing;
}
