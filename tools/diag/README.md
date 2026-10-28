# D1001 display diagnostics

Test-only helpers used to hunt the blue flashes of the reTerminal D1001 (see
[lessons-learned §14](../../docs/lessons-learned.md)). Never ship them in a production config.

| File | What it gives |
|---|---|
| `diag_dsi.h` | Counters for the display path: DPI bridge underruns, DSI host error flags, PHY PLL lock losses, and a one-shot read of the panel's own state and DSI error count. |
| `diag_stress.h` | A dose knob for the redraw load: N ghosts / pumpkins / bats bouncing over every screen. |

## What each counter sees

- **Underruns** (`g_dsi_underruns`): the esp_lcd DPI driver logs `underrun happens` from its ISR
  with `ESP_DRAM_LOGE`. A second ROM putc channel counts the word. Self-test: an
  `esp_rom_printf` containing it must bump the counter.
- **Link errors** (`g_dsi_link_err`): DSI host `INT_ST0` / `INT_ST1` (clear-on-read), polled
  every 2 ms by an `esp_timer`. Named in the log once a second by `dsi_link_log()`.
- **PHY lock losses** (`g_dsi_unlock`): falling edges of `phy_status.phy_lock`.
- **Panel read** (`dsi_panel_read()`): DCS `0x0A` (power mode) and `0x05` (corrupted packets
  received). The esp_lcd read leaves video mode; restoring it mid-frame **shifts the picture**
  (right half on the left) until a reboot. Read at the end of a run, then restart.
- **Panel probe** (`dsi_panel_probe()`): a DCS NOP with ack request, so the panel returns its
  error report. No positive control: zero reports does not prove the panel answered.

## Wiring (device config)

Point the includes at a local clone; `d1001_display` and the decor fonts come from the board
and font packages.

```yaml
esphome:
  includes:
    - /path/to/esphome-dashboard/tools/diag/diag_dsi.h
    - /path/to/esphome-dashboard/tools/diag/diag_stress.h

interval:
  - interval: 5s          # install once, after boot
    then:
      - lambda: |-
          static bool done = false;
          if (!done) { dsi_diag_install(); dsi_link_install(); done = true; }
  - interval: 1s
    then:
      - lambda: dsi_link_log();

sensor:
  - platform: template
    name: "Manques DSI"
    accuracy_decimals: 0
    update_interval: 10s
    lambda: "return (float) g_dsi_underruns;"
  - platform: template
    name: "Erreurs liaison DSI"
    accuracy_decimals: 0
    update_interval: 10s
    lambda: "return (float) g_dsi_link_err;"
  - platform: template
    name: "Pertes verrou PHY"
    accuracy_decimals: 0
    update_interval: 10s
    lambda: "return (float) g_dsi_unlock;"

button:
  - platform: template
    name: "Test compteur DSI"
    on_press:
      - lambda: 'esp_rom_printf("diag: fake underrun for the counter self-test\n");'
  - platform: template
    name: "Lire la dalle (coupe l'image)"
    on_press:
      - lambda: dsi_panel_read(id(d1001_display));

number:
  - platform: template
    name: "Décor de test (objets)"
    optimistic: true
    min_value: 0
    max_value: 40
    step: 1
    initial_value: 0
    restore_value: false
    on_value:
      - lambda: dsi_stress_set((int) x, id(ha_font_decor).get_lv_font(), id(ha_font_decor_sm).get_lv_font());
```

`diag_dsi.h` reads `MipiDsi::io_handle_` through an explicit-instantiation access trick; it
tracks the ESPHome `mipi_dsi` component layout and may need an update when that changes.
