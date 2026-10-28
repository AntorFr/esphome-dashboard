#pragma once
// Diagnostic build only: hunt for the blue frames of the reTerminal D1001.
// 1. DPI bridge underruns: the esp_lcd DPI driver logs "underrun happens" with ESP_DRAM_LOGE (ROM
//    printf) from its ISR. A second ROM putc channel sees every character of it and counts the word.
// 2. DSI host error flags (INT_ST0 / INT_ST1, clear-on-read in the Synopsys host) and PHY PLL
//    lock, polled every 2 ms. Flags that stayed latched instead would count ~500 per second.
// 3. Panel side: a NOP sent with ack request (esp_lcd enables it) makes the panel answer the bus
//    turnaround with its error report (INT_ST0 ack_with_err bits). A one-shot DCS read of 0x0A /
//    0x05 (power mode, count of corrupted packets received) needs command mode: the video stream
//    stops for a moment, so it is a button, not a poll.
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "soc/mipi_dsi_host_struct.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/mipi_dsi/mipi_dsi.h"

static const char *const DSI_DIAG_TAG = "dsi_diag";

// ---- 1. Bridge underruns ----------------------------------------------------------------------

static volatile uint32_t g_dsi_underruns = 0;
static volatile uint8_t g_dsi_pat_i = 0;
static DRAM_ATTR const char k_dsi_pat[] = "underrun";

static void IRAM_ATTR dsi_diag_putc(char c) {
  uint8_t i = g_dsi_pat_i;
  if (c == k_dsi_pat[i]) {
    i++;
    if (k_dsi_pat[i] == 0) {
      g_dsi_underruns = g_dsi_underruns + 1;
      i = 0;
    }
  } else {
    i = (c == k_dsi_pat[0]) ? 1 : 0;
  }
  g_dsi_pat_i = i;
}

inline void dsi_diag_install() { esp_rom_install_channel_putc(2, dsi_diag_putc); }

// ---- 2. Host error flags and PHY lock ---------------------------------------------------------

// INT_ST0[15:0]: the panel's acknowledge error report; [20:16]: D-PHY errors.
static const char *const k_dsi_st0[21] = {
    "panel: SoT error",        "panel: SoT sync error",     "panel: EoT sync error",
    "panel: escape entry",     "panel: LP transmit sync",   "panel: peripheral timeout",
    "panel: false control",    "panel: contention",         "panel: ECC single-bit",
    "panel: ECC multi-bit",    "panel: checksum",           "panel: unknown data type",
    "panel: invalid VC",       "panel: invalid length",     "panel: bit 14",
    "panel: protocol violation", "phy: ErrEsc",             "phy: ErrSyncEsc",
    "phy: ErrControl",         "phy: LP0 contention",       "phy: LP1 contention"};
// INT_ST1: host-side errors (TX/RX timeouts, received packets, FIFOs).
static const char *const k_dsi_st1[20] = {
    "host: HS TX timeout", "host: LP RX timeout",  "host: rx ECC single", "host: rx ECC multi",
    "host: rx CRC",        "host: rx packet size", "host: rx EoTp",       "host: DPI payload FIFO overflow",
    "host: gen cmd write", "host: gen payload write", "host: gen payload send", "host: gen payload read",
    "host: gen payload receive", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "host: DPI payload underflow"};

static volatile uint32_t g_dsi_cnt0[32];
static volatile uint32_t g_dsi_cnt1[32];
static volatile uint32_t g_dsi_link_err = 0;   // polls that caught a new host / PHY flag
static volatile uint32_t g_dsi_panel_err = 0;  // polls that caught a new panel error report
static volatile uint32_t g_dsi_unlock = 0;     // PHY PLL lock losses
static uint32_t g_dsi_pend0 = 0, g_dsi_pend1 = 0;  // flags seen since the last log
static bool g_dsi_locked = true;
static volatile bool g_dsi_poll_paused = false;
static esp_timer_handle_t g_dsi_timer = nullptr;

static void dsi_poll_cb(void *) {
  if (g_dsi_poll_paused)
    return;
  uint32_t s0 = MIPI_DSI_HOST.int_st0.val;
  uint32_t s1 = MIPI_DSI_HOST.int_st1.val;
  bool locked = MIPI_DSI_HOST.phy_status.phy_lock;
  if (!locked && g_dsi_locked)
    g_dsi_unlock = g_dsi_unlock + 1;
  g_dsi_locked = locked;
  uint32_t n0 = s0, n1 = s1;
  if ((n0 | n1) == 0)
    return;
  for (int b = 0; b < 32; b++) {
    if (n0 >> b & 1)
      g_dsi_cnt0[b] = g_dsi_cnt0[b] + 1;
    if (n1 >> b & 1)
      g_dsi_cnt1[b] = g_dsi_cnt1[b] + 1;
  }
  if (n0 & 0xFFFFu)
    g_dsi_panel_err = g_dsi_panel_err + 1;
  if ((n0 & ~0xFFFFu) | n1)
    g_dsi_link_err = g_dsi_link_err + 1;
  __atomic_fetch_or(&g_dsi_pend0, n0, __ATOMIC_RELAXED);
  __atomic_fetch_or(&g_dsi_pend1, n1, __ATOMIC_RELAXED);
}

inline void dsi_link_install() {
  // Drop whatever setup left behind (init sequence acks, etc.).
  uint32_t s0 = MIPI_DSI_HOST.int_st0.val;
  uint32_t s1 = MIPI_DSI_HOST.int_st1.val;
  g_dsi_locked = MIPI_DSI_HOST.phy_status.phy_lock;
  esp_timer_create_args_t args = {};
  args.callback = dsi_poll_cb;
  args.name = "dsi_diag";
  esp_timer_create(&args, &g_dsi_timer);
  esp_timer_start_periodic(g_dsi_timer, 2000);
  ESP_LOGI(DSI_DIAG_TAG, "link poller on (st0 0x%08X, st1 0x%08X, phy_lock %d)", (unsigned) s0, (unsigned) s1,
           (int) g_dsi_locked);
}

// Main loop, every second: name the flags caught since the last call.
inline void dsi_link_log() {
  uint32_t p0 = __atomic_exchange_n(&g_dsi_pend0, 0, __ATOMIC_RELAXED);
  uint32_t p1 = __atomic_exchange_n(&g_dsi_pend1, 0, __ATOMIC_RELAXED);
  for (int b = 0; b < 32; b++) {
    if (p0 >> b & 1) {
      if (b < 21)
        ESP_LOGW(DSI_DIAG_TAG, "%s (total %u)", k_dsi_st0[b], (unsigned) g_dsi_cnt0[b]);
      else
        ESP_LOGW(DSI_DIAG_TAG, "INT_ST0 bit %d (total %u)", b, (unsigned) g_dsi_cnt0[b]);
    }
    if (p1 >> b & 1) {
      if (b < 20 && k_dsi_st1[b] != nullptr)
        ESP_LOGW(DSI_DIAG_TAG, "%s (total %u)", k_dsi_st1[b], (unsigned) g_dsi_cnt1[b]);
      else
        ESP_LOGW(DSI_DIAG_TAG, "INT_ST1 bit %d (total %u)", b, (unsigned) g_dsi_cnt1[b]);
    }
  }
  static uint32_t last_unlock = 0;
  if (g_dsi_unlock != last_unlock) {
    last_unlock = g_dsi_unlock;
    ESP_LOGW(DSI_DIAG_TAG, "PHY PLL lock lost (total %u)", (unsigned) last_unlock);
  }
}

// ---- 3. Panel side ----------------------------------------------------------------------------

// MipiDsi keeps its DBI io handle protected and the class is final. An explicit instantiation
// may name a non-public member, which gives this diagnostic read access to it.
struct DsiIoTag {
  using type = esp_lcd_panel_io_handle_t esphome::mipi_dsi::MipiDsi::*;
  friend type dsi_io_member(DsiIoTag);
};
template<typename Tag, typename Tag::type M> struct DsiIoAccess {
  friend typename Tag::type dsi_io_member(Tag) { return M; }
};
template struct DsiIoAccess<DsiIoTag, &esphome::mipi_dsi::MipiDsi::io_handle_>;

inline esp_lcd_panel_io_handle_t dsi_io(esphome::mipi_dsi::MipiDsi *d) { return d->*dsi_io_member(DsiIoTag{}); }

static volatile uint32_t g_dsi_probes = 0, g_dsi_probe_fail = 0;

// NOP with ack request: queued only (the write path does not wait for the ack); the panel's
// error report, if any, shows up in the poller as a "panel:" flag.
inline void dsi_panel_probe(esphome::mipi_dsi::MipiDsi *d) {
  g_dsi_probes = g_dsi_probes + 1;
  if (esp_lcd_panel_io_tx_param(dsi_io(d), 0x00, nullptr, 0) != ESP_OK)
    g_dsi_probe_fail = g_dsi_probe_fail + 1;
}

// DCS reads: the esp_lcd read switches the host to command mode and leaves it there, so video
// mode is restored right after (the panel misses a frame or so). The poller is paused meanwhile:
// the read waits on the LP RX timeout flag, which a concurrent INT_ST1 read could clear. The flags
// raised around the read are logged with it, apart from the passive counts.
inline void dsi_panel_read(esphome::mipi_dsi::MipiDsi *d) {
  uint8_t pm = 0, cnt = 0;
  g_dsi_poll_paused = true;
  esphome::delay(5);
  esp_err_t e1 = esp_lcd_panel_io_rx_param(dsi_io(d), 0x0A, &pm, 1);
  esp_err_t e2 = esp_lcd_panel_io_rx_param(dsi_io(d), 0x05, &cnt, 1);
  MIPI_DSI_HOST.mode_cfg.cmd_video_mode = 0;
  uint32_t s0 = MIPI_DSI_HOST.int_st0.val;
  uint32_t s1 = MIPI_DSI_HOST.int_st1.val;
  g_dsi_poll_paused = false;
  ESP_LOGW(DSI_DIAG_TAG, "panel read: power mode 0x%02X (%s), corrupted packets %u%s (%s), st0 0x%08X st1 0x%08X", pm,
           esp_err_to_name(e1), cnt & 0x7F, (cnt & 0x80) ? "+ (overflow)" : "", esp_err_to_name(e2), (unsigned) s0,
           (unsigned) s1);
}
