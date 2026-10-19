# Hardware capabilities and verification

The Raspberry Pi 4/5 Linux port is experimental. ARM64 CI covers the native
core, service, package installation and updates. USB audio, GPIO, and display
tests use software drivers or test doubles. No physical Pi report is recorded.

Firmware builds and simulator tests check software behavior. A physical verification report records what worked on a particular board revision, wiring, and firmware commit. A passing build alone does not establish that a microphone, power circuit, or display works on a device.

## Current hardware

| Model | Display and input | Audio | Power support | Verification |
|---|---|---|---|---|
| ESP32-S3-DevKitC-1 N8R8 breadboard | Wired ST7789, TALK and CANCEL buttons | Wired I2S microphone; optional MAX98357A speaker | External power | CI build; physical report not recorded |
| Waveshare ESP32-S3-LCD-1.54, SKUs 33866/33867 | ST7789 240×240; BOOT and PLUS | ES7210 microphones and ES8311 speaker | Calibrated voltage, charging signal, battery latch and screen timeout; USB bypasses shutdown | CI build; physical report not recorded |
| Waveshare ESP32-S3-Touch-AMOLED-1.75 | CO5300 466×466; CST9217 touch, BOOT and PWR | ES7210 microphones and ES8311 speaker output | AXP2101 readings and local power-off; optional screen timeout | CI build; physical report not recorded |
| Waveshare ESP32-S3-Touch-AMOLED-1.75C, SKUs 33691/33692 | CO5300 466×466; CST9217 touch and BOOT | ES7210 microphones and ES8311 onboard speaker | AXP2101 readings, audio supply and local power-off; screen timeout | Experimental; physical report not recorded |
| Waveshare ESP32-S3-Touch-AMOLED-1.8, V2 only | CO5300 368×448; CST820 touch and BOOT | ES8311 analog microphone and speaker output | AXP2101 readings and local power-off; display/touch reset through a TCA9554 expander | Experimental; physical smoke check only (boot, display, I2C devices present, codecs and the touch, speaker and microphone tasks start); touch, microphone capture, speaker playback and battery not verified; full checklist not completed |
| Waveshare ESP32-S3-Touch-LCD-1.85C V2 / PCB Rev2.0 only | ST77916 360×360 round QSPI LCD; CST816 touch and BOOT | ES8311 + ES7210 dual analog mic slots, NS4150B PA; mono transport; no software AEC | USB/battery switch; screen timeout; no battery telemetry or software shutdown | Experimental; [partial Rev2.0 report](#waveshare-185c-v2-partial-physical-report); full checklist not completed |
| Xorigin AIPI Lite | ST7789 128×128; BOOT and power keys | One ES8311 for speaker and microphone | GPIO 10 power latch and power-off; battery level and charging not reported | Experimental; contributor smoke test on an earlier revision (boot, display and audio start, online pairing); microphone capture, colors, USB console and battery not verified on this revision |
| Espressif ESP32-S3-BOX-3 | ST7789/TT21100 or ILI9342/GT911, detected through I2C; BOOT and touch gestures | ES7210 microphones and ES8311 onboard speaker | USB power; screen timeout | Experimental; physical report not recorded for either panel revision |
| M5Stack CoreS3 (K128) | ILI9342C/E 320×240, detected through touch firmware; FT6336 gestures | ES7210 microphones and AW88298 speaker | AXP2101 readings, backlight and local power-off; AW9523 reset/boost control | Experimental; physical report not recorded for either panel revision |
| LilyGO T-Display-S3 (SKU/Version H587; PCB revision 1.2) | ST7789 320×170 over 8-bit i80; BOOT and Button2 | No onboard audio | Battery divider; no fuel gauge; GPIO15 powers the panel rail, not device shutdown | Experimental; physical smoke check only (screen ready/replies, Wi-Fi, online pairing); full physical checklist not completed; hardware-verified status not claimed |
| Elecrow CrowPanel 2.1-inch HMI (ESP32-S3R8, 16 MB flash) | ST7701 480×480 RGB round panel; CST-family touch, rotary encoder, encoder button on PCF8574 | No onboard audio | USB power; screen timeout | Experimental; physical smoke check only (panel bring-up, brightness scale, Wi-Fi, online pairing, serial console); touch coordinates and encoder direction not physically verified |
| Elecrow CrowPanel Advanced 10.1-inch ESP32-P4 V1.1 | 1280×800 DSI; touch | Direct-I²S speaker; PDM microphone | STC8-reported battery telemetry; battery-only Wi-Fi startup powerdown/reboot reported | Partial physical report; see [test record](#crowpanel-advanced-p4-partial-physical-test-record) and [power report](#crowpanel-advanced-p4-power-report); cause unconfirmed |

The LCD-1.54 `-EN` SKU uses the same hardware. The separate Touch-LCD-1.54 model adds a CST816 touchscreen that this port does not drive. AMOLED-1.75C has its own firmware profile; its reset and audio clock pins differ from the 1.75 model. See [hardware and wiring](hardware.md) for connections and exact model names.

CI builds and packages these profiles. The browser installer lists profiles included in the latest published release, so newly merged profiles may require a source build until the next release. Other chips, wiring, and unlisted hardware revisions are porting targets, not verified configurations.

## CrowPanel Advanced P4 power report

- **Board:** Elecrow CrowPanel Advanced 10.1-inch ESP32-P4 V1.1. The tested unit lacks schematic-listed IC5/IC6.
- **Firmware:** `6165f45-i2s-timeout-ms`.
- **Observed power behavior:** With USB disconnected, the screen powered down when Wi-Fi started at 100% brightness. In a 50%-brightness test, the device rebooted as Wi-Fi connected; the user measured about 4.11 V at the battery terminals before connection and 4.08 V during connection. J13 pin 3 to pin 4 measured 3.29 V through boot and 3.28 V at Wi-Fi connection.
- **Cause:** Not established. These meter readings show no large sustained drop at the measured points, but may miss a brief transient. No reset cause was captured for the battery-only event.
- **Telemetry caveat:** With USB attached, the display showed 4,218 mV / 100% while a meter at the battery terminals read 4.114 V. After unplugging USB, the display showed 3,779 mV / 39%, but there was no simultaneous battery-terminal measurement. STC8 voltage and percentage remain uncalibrated.
- **Candidate mitigation (not tested):** Evaluate a 1 F super capacitor as a possible buffer for Wi-Fi startup load. This is a suggestion, not a validated fix. Required capacitance, placement, voltage rating, ESR, inrush current, and charger/regulator compatibility need engineering review before installation.

## CrowPanel Advanced P4 partial physical test record

This record summarizes the physical checks available for the 10.1-inch V1.1 unit. It is partial; the full checklist is not complete, so the port remains experimental. Unknown and untested fields are explicit.

```text
Model / PCB revision: Elecrow CrowPanel Advanced 10.1-inch ESP32-P4, V1.1.
Firmware build ID: d7c7fe9-stc8-register-list (not a Git commit hash; based on commit prefix d7c7fe9 plus the STC8 register-screen source changes), built with ESP-IDF 5.5.4. The Gateway reported this build. App-only flash at 0x20000; image SHA-256 51b5be8a8ebe4f933a56ff5c4ce07f9f36f4762401224c8a2ab5ca8d71882f96; flash readback verified. On-screen build-ID confirmation was not recorded.
Hermes version / host OS: Hermes Agent v0.22.2 at report compilation; the hardware-test host ran Manjaro Linux, kernel 7.2.3-2-MANJARO. Test-time Hermes version was not recorded.
Flash / PSRAM: profile configures 16 MB flash and 32 MB HEX PSRAM; physical memory-detection log was not recorded.
Power supply / battery / peripherals: USB-C power and a single-cell battery on J3; battery make/capacity not recorded. Integrated 1280x800 DSI display, touch, PDM microphone, direct-I2S speaker, and ESP32-C6 Wi-Fi companion. USB-C connector used for the latest charge-LED observation was not recorded.
USB install and recovery: app-only USB flash at 0x20000 with readback verification. No full-flash erase or recovery procedure was exercised. A prior app image is retained for rollback, but rollback was not physically tested.
Pairing and persistence: device was paired and appeared online at the Gateway. Identity persistence after reboot/update and rejection of an unapproved device were not formally tested.
Display / buttons / touch: display and touch were physically confirmed during bring-up. Button coverage, color accuracy, full touch accuracy, and the current build ID on screen were not recorded.
Microphone / speaker / interruption: normal speech submissions worked; a spoken reply was reported to play without audible breakup. Acoustic measurements and cancel/interrupt testing were not completed.
Network loss and recovery: Wi-Fi/Gateway pairing was established. Deliberate Wi-Fi or Gateway loss/recovery, phone setup with wrong passwords, cancellation/expiry, and setup-page reachability were not tested.
Power and battery: see the power report above. With USB off and battery connected, cell voltage was 4.07 V and the R187/R188 junction was 2.92 V; J13 measured 3.29 V only while the board was on. With USB connected and no battery, J3 was 3.29 V and the divider junction was 3.32 V; these are open-node readings, not cell state. With battery and USB attached, the charge LED was briefly red then green; cell terminals read 4.10 V. A reported 85.3 mV drop from board-side J3 positive to cell positive is consistent with current into the cell if measured with red on board-side J3 and black on cell positive; charge current was not directly quantified. A prior STC8 sample reported 4093 mV, 84%, fully_charged, charging=false, and no external_power value; USB state during that sample was not recorded. STC8 voltage/percentage remain uncalibrated and USB-dependent. The 1 F capacitor suggestion is untested.
OTA / rollback: OTA, settings persistence after OTA, and rollback were not tested.
Two-hour session: Not run.
Failed or untested steps: at 100% brightness, battery-only Wi-Fi startup powered down the screen; cause and reset reason remain unknown. The full checklist items above remain untested where stated, including measured charge-current magnitude and calibrated battery percentage.
Sanitized logs and diagnostics: no sanitized hermes-gadget diag report or log bundle is attached.
```

## Waveshare 1.85C V2 partial physical report

- **Board:** PCB Rev2.0 speaker-box version with ESP32-S3, 16 MB flash and 8 MB PSRAM, powered over USB.
- **Firmware:** `fb8f81e`, built with ESP-IDF 6.1 and tested on 2026-10-07 after an app-only update to `ota_0` on the same board.
- **Earlier firmware:** combined builds of this port's branch up to `edcbd66`. All of them had the SPI flush-timeout change for every `SpiDisplay` panel. The later ones also had the round settings target. The network tests used the branch's own mapped-IPv4 provisioning guard, which main's `ipv4_of` replaces.
- **Confirmed on `fb8f81e`:** a readable, upright display; touch and brightness; the local test tone; a spoken question with an audible Hermes reply; and swipe-down cancel during a reply. Playback stopped, and the next reply played.
- **Earlier results:** the combined builds passed display, touch, brightness, microphone level, test tone, Wi-Fi setup, WSS, pairing and spoken reply checks.
- **Updates:** Wi-Fi and pairing survived the app-only update to `fb8f81e`. The NVS Wi-Fi, server and device-key entries did not change, and the device reconnected online and paired without setup. Earlier app-only updates passed readback verification with byte-identical NVS and the same paired identity.
- **Earlier power cycle:** on the combined builds, after USB was unplugged and plugged back in, the device reconnected without Wi-Fi setup or re-pairing. This does not test losing Wi-Fi while powered.
- **Not verified:** battery operation, OTA and rollback, long-run stability, interrupting touch or audio, recovery from injected faults, Wi-Fi loss while powered, each microphone on its own, and touch accuracy across the whole screen.
- **Not implemented:** software echo cancellation, battery telemetry and software shutdown.

The port stays experimental.

## Record a physical test

Run this checklist for each board revision and release candidate. Report failed and untested steps explicitly. Do not publish Wi-Fi passwords, access tokens, device keys, or private conversation content.

1. Record the model, PCB revision, flash and PSRAM, firmware commit/version, host OS, Hermes version, power supply, and connected peripherals. Include photographs of the board label and wiring when useful.
2. Install through USB. Check the detected chip and memory, boot logs, display orientation, colors, and readable pairing code.
3. Pair with Hermes. Restart the device and confirm that its identity and pairing survive. Confirm that an unapproved device cannot issue actions.
4. Hold TALK, speak, and release. Check the microphone level, transcript, and a complete spoken reply. Cancel a recording and interrupt a playing reply. Verify every physical button and touch gesture.
5. Test volume and brightness where available. Run the device's hardware checks when its firmware provides them. Record speaker distortion, missing audio, display corruption, or unexpected resets.
6. Disconnect and restore Wi-Fi and the gateway. Confirm recovery without resetting the device identity. Test phone setup with valid and wrong passwords, cancellation, its ten-minute expiry, and recovery to the previous network. Confirm the temporary page is unreachable from the station address. Check USB setup after phone setup closes.
7. On battery-capable ports, record charging, voltage/percentage, dimming, sleep, wake, and power-button results. Mark unsupported power functions as not implemented. Check USB and battery operation separately.
8. Install an OTA update for this exact board. Confirm that settings survive and the new version reaches Hermes. On a recoverable test unit, verify rollback with a candidate that cannot reach the gateway. Keep a USB recovery path ready.
9. Run a two-hour session with repeated voice turns and reconnects. Record resets, audio failures, and memory trends rather than only the final state.
10. Attach a sanitized `hermes-gadget diag --port PORT` report and the relevant logs to the pull request or issue.

Use this report format:

```text
Model / PCB revision:
Firmware version / commit:
Hermes version / host OS:
Flash / PSRAM:
Power supply / battery / peripherals:
USB install and recovery:
Pairing and persistence:
Display / buttons / touch:
Microphone / speaker / interruption:
Network loss and recovery:
Power and battery:
OTA / rollback:
Two-hour session:
Failed or untested steps:
Sanitized logs and diagnostics:
```

## Support labels

- **Experimental:** the port builds and passes automated checks, but no complete physical report is linked for that configuration.
- **Hardware verified:** a linked report identifies the exact revision and tested firmware, covers the checklist, and states any limitations.

A report for one model or revision does not verify another. Keep earlier reports when adding a new one so users can find the firmware and hardware combination they own. Update this page in the same pull request that adds a port or changes its verified capabilities.
