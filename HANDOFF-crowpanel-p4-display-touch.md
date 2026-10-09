# Handoff: CrowPanel Advanced 10.1" ESP32-P4 — display, touch & remaining work

**Date:** 2026-10-09
**Branch:** `feat/crowpanel-advance-p4-101` (SDK repo, based on `60a27a8`)
**Status:** Wi-Fi + audio (PDM mic, I2S speaker) **built, flashed, and verified on hardware**. Display and touch are **intentionally deferred** — that is the remaining work.

---

## 1. What is DONE and verified on the physical board

The hermes-gadget-sdk firmware builds for `esp32p4` and runs on the Elecrow
CrowPanel Advanced 10.1" (ESP32-P4 rev v1.3, ESP32-C6 Wi-Fi companion).

Verified boot log markers:
```
hg.pdmmic: pdm microphone ready (clk 24, data 26)
hg.audio: speaker ready
hg.diag: parts: display none, microphone pdm, speaker i2s, touch no, key no
hg: device hg-... ready (board crowpanel-advance-p4-10.1, fw 0.2.0)
```
Wi-Fi over the C6 (set creds via serial console):
```
set wifi_ssid <ssid>  → @ok wifi_ssid
set wifi_pass <pass>  → @ok wifi_pass
RPC_WRAP: ESP Event: Station mode: Connected
hg.wifi: connected, ip 192.168.8.160
```

### Files changed for the P4 port (all committed to the branch or in working tree)

Modified:
- `firmware/esp32/CMakeLists.txt` — derives `ESP_IDF_VERSION` from `version.txt`
  (critical: without it the C6 Kconfig choice never parses — see §5).
- `firmware/esp32/main/CMakeLists.txt` — adds `port_pdm_mic.cpp`.
- `firmware/esp32/main/Kconfig.projbuild` — adds `HG_BOARD_CROWPANEL_P4_101` choice.
- `firmware/esp32/main/board.cpp` — board name macro + `make()` block (audio pins).
- `firmware/esp32/main/board.hpp` — `I2sMicConfig::pdm` bool.
- `firmware/esp32/main/main.cpp` — `g_pdm_mic` instance, mic selection, diag `pdm`.
- `firmware/esp32/main/port.hpp` — `PdmMic` class declaration.
- `firmware/esp32/main/idf_component.yml` — `esp_hosted 2.7.0` + `esp_wifi_remote 0.16.1` (P4-only rules).

New:
- `firmware/esp32/main/port_pdm_mic.cpp` — PDM mic driver (`hg::AudioIn`).
- `firmware/esp32/boards/crowpanel-p4-101/sdkconfig.defaults`
- `firmware/esp32/boards/crowpanel-p4-101/board.json`

### Board pin map (from the V1.1 schematic, `Eagle_SCH&PCB/1.1/*.sch`)

Audio (wired & working):
| Function | GPIO |
|---|---|
| PDM mic clock | 24 |
| PDM mic data | 26 |
| Speaker BCLK | 22 |
| Speaker WS (LRCLK) | 21 |
| Speaker DATA | 23 |
| BOOT button (=TALK) | 35 |

Display / touch (NOT yet wired into the SDK — this is the remaining work):
| Function | Value |
|---|---|
| Panel | 1024×600 MIPI-DSI, controller **EK79007**, RGB565 |
| DSI data lanes | 2, lane bit-rate 900 Mbps |
| DSI DPI clock | 51 MHz |
| DSI PHY power | internal LDO **channel 3 @ 2500 mV** |
| DSI bus | ESP32-P4 **dedicated MIPI-DSI peripheral pins** (NOT GPIO-muxed — `esp_lcd_new_dsi_bus()` takes no pin numbers; the SoC fixes them) |
| LCD reset | NC (not bonded) |
| Backlight | GPIO31, active-high, not inverted |
| Touch | **GT911** on I2C port 0: SDA GPIO45, SCL GPIO46, INT GPIO42, RST GPIO40, addr 0x5D/0x14 |
| Camera SCCB (future) | I2C1 SCL GPIO13, SDA GPIO12 |

---

## 2. What REMAINS

1. **MIPI-DSI display driver** (`port_dsi.cpp` + board config). The SDK's `LcdBus`
   only supports SPI/I80/RGB — there is **no DSI bus type**. This is a new driver
   implementing `hg::Display`, not a config entry. See §3.
2. **GT911 touch** on I2C0 (SDA 45 / SCL 46 / INT 42 / RST 40). Feed `hg::TouchGestures`
   (hold=TALK, tap=yes, swipe-down=CANCEL) like `port_touch.cpp` does for other
   controllers. Touch shares I2C0 with nothing else critical; the display DSI bus
   is separate.
3. **Enable display + touch in the board `make()`** once drivers land (currently
   `b.lcd` disabled, `b.touch` disabled; only `b.i2c = {45,46,400000}` is set).
4. **Docs:** add a CrowPanel Advanced P4 section to `docs/hardware.md` and note
   the DSI/touch bring-up in `docs/porting.md`. `board.json` already points at
   `docs/hardware.md#elecrow-crowpanel-advanced-101-inch-esp32-p4`.
5. **Simulator profile** (optional): add a 1024×600 board to
   `python/hermes_gadget/sim/runner.py` `BOARDS` for desktop UI work.
6. **CI:** the SDK builds every board via PlatformIO, which **does not support
   ESP32-P4**. A P4 target needs either raw `idf.py` CI or a separate P4 pipeline.
   Coordinate with SDK maintainers; do not silently add a broken PlatformIO env.
7. **Camera (optional, later):** MIPI-CSI + V4L2 driver exists in the vendor tree
   (`bsp_camera.c`) but the SDK has **no camera abstraction** in `hg/hal.hpp` at
   all. This needs a core interface + protocol design first — large effort.
8. **Battery: DEFERRED / no viable path.** `ADC_VBAT` is read by a separate
   STC8H1K08 power-MCU, not the P4, and it only exposes UART on the J8 debug
   header. No documented protocol. Do not spend time here without a new plan.

---

## 3. Display driver: the `hg::Display` contract

Implement this interface (`firmware/core/include/hg/hal.hpp`):

```cpp
struct DisplayInfo {
  uint16_t width = 0, height = 0;
  bool swap_bytes = false;   // RGB565 big-endian if the panel wants it
  bool has_backlight = false;
  bool round = false;        // circular panel — keep UI in inscribed square
  uint8_t corner_inset = 0;  // top-bar padding for rounded-corner panels
};
class Display {
  virtual ~Display() = default;
  virtual DisplayInfo info() const = 0;
  virtual uint16_t* framebuffer() = 0;          // width*height RGB565 you own (PSRAM)
  virtual void flush(uint16_t y0, uint16_t y1) = 0;  // push rows [y0,y1)
  virtual void set_backlight(uint8_t percent) {}     // optional
};
```

The core owns a full `width×height` RGB565 framebuffer and calls `flush(y0,y1)`
for dirty row bands. `SpiDisplay` (`port_display.cpp`) is the reference: it stages
rows through a DMA bounce buffer (`hg::BandFlush`, `firmware/drivers/band_flush.hpp`)
and never refills the buffer while a transfer may still read it. For DSI, the panel
takes a framebuffer directly (DPI mode), so `flush` may become a no-op or a cache
flush — but you still must satisfy the contract and prove pixels change.

### Vendor DSI reference (authoritative — adapt, don't invent)

`example/V1.2/AI_Conversation/7inch_9inch_10inch_P4_HMI_AI/main/boards/elecrow-p4-board/elecrow_board.cc`
has a complete, working bring-up. Key sequence:
1. Acquire internal **MIPI-DSI LDO channel 3 @ 2500 mV** (`esp_ldo_acquire_channel`).
2. `esp_lcd_new_dsi_bus()` with `num_data_lanes = 2`, `lane_bit_rate_mbps = 900`.
3. `esp_lcd_new_panel_io_dbi()` for the DBI command interface.
4. `esp_lcd_dpi_panel_config_t`: `pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565`,
   `dpi_clock_freq_mhz = 51`, `num_fbs = 1`.
5. `esp_lcd_new_panel_ek79007()` → `esp_lcd_panel_reset()` → `esp_lcd_panel_init()`.
6. Backlight GPIO31 active-high.

Add a `Dsi` variant to `LcdBus` (or a dedicated `DsiConfig`) in `board.hpp`, wire it
in `main.cpp` alongside the RGB/I80 branches, and add a diag `parts.display` name.

**Display gotchas (from the embedded-device-bringup skill):**
- A `display initialized` log line proves init ran, NOT that pixels changed. Prove
  the render path with an unmistakable test pattern and ask the user what the panel
  shows.
- Orientation/mirror/offset cannot be verified from a log — confirm with the user.
- DSI on P4 needs PSRAM for the framebuffer (32 MB present, confirmed).

---

## 4. Touch: GT911

- Controller GT911, I2C0, SDA 45 / SCL 46 / INT 42 / RST 40, 7-bit addr 0x5D or
  0x14 (set by INT level at reset). `esp_lcd_touch_gt911` is already an SDK
  component dependency (`idf_component.yml`).
- Follow `port_touch.cpp`: read samples, post `EventType::Touch`, feed
  `hg::TouchGestures`. In the board `make()` set `b.touch.enabled = true`,
  `b.touch.addr`, `b.touch.rst`, width/height 1024×600, and add GT911 to the
  `TouchController` enum if not present.
- With touch enabled, `main.cpp` auto-sets `profile.touch_screen = true` and adds
  the `touch_cancel` setting.

---

## 5. Build environment (already set up on the laptop `yogaman`)

The ESP32-P4 board is on `yogaman` (Lenovo laptop) at **`/dev/ttyUSB0`** (CH340
bridge; user in `uucp`). A full ESP-IDF toolchain is installed there.

- **Toolchain:** `~/crowpanel-p4-build/` — venv `.venv-idf` (Python 3.11),
  `esp-idf` (v5.5.4), `tools/` (riscv32-esp-elf 14.2.0, cmake, ninja),
  managed python env `tools/python_env/idf5.5_py3.11_env`.
- **SDK checkout on laptop:** `~/crowpanel-p4-build/hg-esp32/` (app),
  `~/crowpanel-p4-build/core/` (portable core), `~/crowpanel-p4-build/drivers/`.
  These are rsynced from hermesvm01 `~/hermes-gadget-sdk/firmware/{esp32,core,drivers}`.
- **Sync script:** `~/.hermes/cache/scratch/crowpanel-p4/sync_sdk.sh` (run on
  hermesvm01; pushes esp32+core+drivers to the laptop, excludes build artifacts
  and `managed_components`).
- **Build script:** `~/crowpanel-p4-build/build_sdk2.sh` (on laptop) — sets
  IDF env, wipes `build/`+`sdkconfig`, `set-target esp32p4`, `build`. Uses
  `SDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/crowpanel-p4-101/sdkconfig.defaults"`.
- **Flash/monitor:** `idf.py -p /dev/ttyUSB0 flash monitor`.

### Critical build facts (DO NOT rediscover — read the skill)

Load the **`esp32-p4-hosted-wifi`** skill before building. Summary:
1. **`ESP_IDF_VERSION` must be exported as major.minor (e.g. `5.5`)** before
   `idf.py` configure, or `esp_wifi_remote`'s `orsource ./idf_v$ESP_IDF_VERSION/...`
   never resolves and the build dies with `#error "Unknown Slave Target"`. The SDK
   `CMakeLists.txt` now derives it from `version.txt`; a shallow IDF clone lacks
   `version.txt` (create `echo v5.5.4 > $IDF_PATH/version.txt`).
2. **`idf.py` is not on PATH** in non-interactive SSH shells — invoke explicitly:
   `"$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" ...`.
3. **P4 rev v1.x** needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` +
   `CONFIG_ESP32P4_REV_MIN_100=y` + `CONFIG_ESP32P4_REV_MAX_FULL=199`, else the
   bootloader targets rev v3.x and esptool refuses to flash.
4. **P4 PSRAM is `SPIRAM_MODE_HEX`, not `OCT`.**
5. **`idf.py monitor` does NOT forward stdin** — to drive the serial console, write
   to `/dev/ttyUSB0` directly with pyserial (`port.write(b"cmd\r\n")`), then read.
6. `check_config.py` fails the build if any `sdkconfig.defaults` line didn't reach
   the generated config. Choice options (e.g. FLASHSIZE) need an explicit
   `# CONFIG_<other> is not set` in the board file.

---

## 6. Open questions / things to confirm with the user

- **`esp_hosted` version mismatch** (benign): host `2.7.0` < C6 coprocessor
  `2.12.0`. Connectivity works; reconcile versions before a production release.
- **ES8311/ES7210 codec:** the V1.1 schematic populates an ES8311 DAC + ES7210
  4-mic ADC, but the vendor factory firmware does NOT use them (raw I2S + PDM).
  The current port uses PDM+I2S. If the codec parts are actually soldered on a
  given unit, a codec path (via `esp_codec_dev`, already a dependency) is a
  possible upgrade — but PDM/I2S is proven, so don't switch without reason.
- **Camera & battery:** see §2 — camera needs a core interface; battery has no
  viable path. Confirm priorities before investing.

## 7. Source of truth references

- SDK repo: `~/hermes-gadget-sdk` (hermesvm01), branch `feat/crowpanel-advance-p4-101`.
- Vendor repo: `~/crowpanel-p4-101` (hermesvm01 clone of Elecrow-RD).
  - Schematic (pins): `Eagle_SCH&PCB/1.1/ESP32-P4 Display 10.1 inch V1.1.sch`
  - Working DSI+touch+audio reference:
    `example/V1.2/AI_Conversation/7inch_9inch_10inch_P4_HMI_AI/main/boards/elecrow-p4-board/`
  - Camera driver: `example/V1.2/idf-code/Lesson13-Camera_Real-Time/peripheral/bsp_camera/`
- Skill: `esp32-p4-hosted-wifi` (load it).
