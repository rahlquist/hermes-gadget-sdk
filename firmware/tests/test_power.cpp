#include <array>
#include <vector>

#include "axp2101.hpp"
#include "stc8_battery.hpp"
#include "cores3.hpp"
#include "check.hpp"

TEST("STC8 battery: reads each register byte and preserves all reported telemetry") {
  const std::array<uint8_t, 12> raw = {0xeb, 0x0b, 0, 0, 0x90, 0x10, 0, 0, 0x64, 0x02, 0x02, 0xaa};
  std::vector<uint8_t> registers;
  hg::Stc8Battery power([&](uint8_t reg, uint8_t& value) {
    registers.push_back(reg);
    value = raw[reg];
    return true;
  });

  const auto status = power.read();

  CHECK(status.has_value());
  CHECK(registers == std::vector<uint8_t>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
  CHECK(status->battery_adc_mv == uint16_t(3051));
  CHECK(status->battery_mv == uint16_t(4240));
  CHECK(status->battery_percent == uint8_t(100));
  CHECK(status->battery_registers == raw);
  CHECK(status->charging == false);
  CHECK(!status->battery_present);
  CHECK(!status->external_power);
  CHECK(status->battery_state == hg::BatteryChargeState::FullyCharged);
  CHECK(status->battery_led_state == hg::BatteryLedState::FullyCharged);
  CHECK(!power.can_power_off());
  CHECK(!power.power_off());
}

TEST("STC8 battery: charging state maps to the generic charging flag") {
  const std::array<uint8_t, 12> raw = {0xeb, 0x0b, 0, 0, 0x88, 0x10, 0, 0, 75, 1, 1, 0};
  hg::Stc8Battery power([&](uint8_t reg, uint8_t& value) { value = raw[reg]; return true; });

  const auto status = power.read();

  CHECK(status.has_value());
  CHECK(status->charging == true);
  CHECK(status->battery_state == hg::BatteryChargeState::Charging);
  CHECK(status->battery_led_state == hg::BatteryLedState::Charging);
}

TEST("STC8 battery: error state is preserved without claiming not charging") {
  const std::array<uint8_t, 12> raw = {0xeb, 0x0b, 0, 0, 0x90, 0x10, 0, 0, 100, 4, 4, 0};
  hg::Stc8Battery power([&](uint8_t reg, uint8_t& value) { value = raw[reg]; return true; });

  const auto status = power.read();

  CHECK(status.has_value());
  CHECK(status->battery_state == hg::BatteryChargeState::Error);
  CHECK(!status->charging.has_value());
}

TEST("STC8 battery: a register read failure discards the sample and stops the sequence") {
  std::vector<uint8_t> registers;
  hg::Stc8Battery power([&](uint8_t reg, uint8_t& value) {
    registers.push_back(reg);
    if (reg == 5) return false;
    value = 0;
    return true;
  });

  CHECK(!power.read());
  CHECK(registers == std::vector<uint8_t>({0, 1, 2, 3, 4, 5}));
}

TEST("STC8 battery: invalid values are omitted rather than reported as real telemetry") {
  const std::array<uint8_t, 12> raw = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 101, 5, 5, 0xaa};
  hg::Stc8Battery power([&](uint8_t reg, uint8_t& value) { value = raw[reg]; return true; });

  const auto status = power.read();

  CHECK(status.has_value());
  CHECK(!status->battery_adc_mv);
  CHECK(!status->battery_mv);
  CHECK(!status->battery_percent);
  CHECK(!status->charging);
  CHECK(!status->battery_state);
  CHECK(!status->battery_led_state);
  CHECK(!status->battery_present);
  CHECK(!status->external_power);
}

TEST("AXP2101: battery, USB and charging readings never alter power configuration") {
  std::array<uint8_t, 256> regs{};
  regs[0x00] = 0x28;
  regs[0x01] = 0x20;
  regs[0x18] = 0x0a;
  regs[0x30] = 0x03;
  regs[0x34] = 0xcf;  // high status bits must not become voltage bits
  regs[0x35] = 0xa0;  // 4000 mV
  regs[0xa4] = 81;
  int failed_reg = -1;
  std::vector<std::pair<uint8_t, uint8_t>> writes;
  hg::Axp2101 power(
      [&](uint8_t reg, uint8_t* out, size_t n) {
        if (reg == failed_reg) return false;
        for (size_t i = 0; i < n; ++i) out[i] = regs[reg + i];
        return true;
      },
      [&](uint8_t reg, uint8_t value) { writes.emplace_back(reg, value); return true; });
  auto p = power.read();
  CHECK(p.has_value());
  CHECK(p->battery_present == true);
  CHECK(p->external_power == true);
  CHECK(p->charging == true);
  CHECK(p->battery_mv == 4000);
  CHECK(p->battery_percent == 81);
  CHECK(writes.empty());

  regs[0xa4] = 255;
  CHECK(!power.read()->battery_percent);
  regs[0x00] = 0x20;
  p = power.read();
  CHECK(p->battery_present == false);
  CHECK(p->charging == false);
  CHECK(!p->battery_mv);
  CHECK(!p->battery_percent);
  regs[0x00] = 0x08;
  regs[0x18] = regs[0x30] = 0;
  p = power.read();
  CHECK(!p->battery_mv);
  CHECK(!p->battery_percent);
  CHECK(writes.empty());
  failed_reg = 0x00;
  CHECK(!power.read());
  failed_reg = 0x10;
  CHECK(!power.power_off());
  CHECK(writes.empty());
  failed_reg = -1;
  regs[0x10] = 0x34;
  CHECK(power.power_off());
  CHECK_EQ(writes.size(), size_t(1));
  CHECK_EQ(writes[0].first, uint8_t(0x10));
  CHECK_EQ(writes[0].second, uint8_t(0x35));
}

TEST("AXP2101: audio supply enables ALDO1 at 3.3 V and preserves other rails") {
  std::array<uint8_t, 256> regs{};
  regs[0x90] = 0xa4;
  regs[0x92] = 0x60;
  regs[0x62] = 0x08;  // charge current must stay unchanged
  bool failed = false;
  hg::Axp2101 power(
      [&](uint8_t reg, uint8_t* out, size_t n) {
        if (failed) return false;
        for (size_t i = 0; i < n; ++i) out[i] = regs[reg + i];
        return true;
      },
      [&](uint8_t reg, uint8_t value) { regs[reg] = value; return true; });
  CHECK(power.enable_aldo1_3v3());
  CHECK_EQ(regs[0x90], uint8_t(0xa5));
  CHECK_EQ(regs[0x92], uint8_t(0x7c));
  CHECK_EQ(regs[0x62], uint8_t(0x08));
  failed = true;
  CHECK(!power.enable_aldo1_3v3());
}

TEST("CoreS3: peripheral power preserves charger and external output configuration") {
  std::array<uint8_t, 256> pmic{}, io{};
  pmic.fill(0x60);
  pmic[0x90] = 0xe4;
  io.fill(0xff);
  auto expected_pmic = pmic;
  auto expected_io = io;
  std::vector<uint32_t> delays;
  hg::CoreS3Control control(
      [&](uint8_t addr, uint8_t reg, uint8_t& value) {
        value = (addr == 0x34 ? pmic : io)[reg]; return true;
      },
      [&](uint8_t addr, uint8_t reg, uint8_t value) {
        (addr == 0x34 ? pmic : io)[reg] = value; return true;
      },
      [&](uint32_t ms) {
        delays.push_back(ms);
        if (ms == 10) { CHECK_EQ(io[0x02], 0xfa); CHECK_EQ(io[0x03], 0xfd); }
        if (ms == 300) { CHECK_EQ(io[0x02], 0xff); CHECK_EQ(io[0x03], 0xff); }
      });
  CHECK(control.begin());
  expected_pmic[0x92] = 0x6d;  // ALDO1: 1.8 V
  expected_pmic[0x93] = 0x7c;  // ALDO2: 3.3 V
  expected_pmic[0x90] = 0x67;  // enable audio rails, disable backlight
  expected_io[0x04] = 0xfa;
  expected_io[0x05] = 0x7d;
  CHECK(pmic == expected_pmic);
  CHECK(io == expected_io);
  CHECK(delays == std::vector<uint32_t>({10, 300}));
  CHECK(control.set_brightness(50));
  CHECK_EQ(pmic[0x99], 0x78);  // DLDO1: 2.9 V, upper bits retained
  CHECK_EQ(pmic[0x90], 0xe7);
  CHECK(control.set_brightness(255));
  CHECK_EQ(pmic[0x99], 0x7c);  // clamp to 3.3 V
  CHECK(control.set_brightness(0));
  CHECK_EQ(pmic[0x90], 0x67);
  CHECK_EQ(pmic[0x62], 0x60);  // charging current is untouched
  CHECK(io == expected_io);
}

TEST("CoreS3: failed power access stops initialization without releasing reset") {
  bool read_ok = false;
  int writes = 0, delays = 0;
  hg::CoreS3Control control(
      [&](uint8_t, uint8_t, uint8_t& value) { value = 0; return read_ok; },
      [&](uint8_t, uint8_t, uint8_t) { ++writes; return false; },
      [&](uint32_t) { ++delays; });
  CHECK(!control.begin());
  CHECK_EQ(writes, 0);
  read_ok = true;
  CHECK(!control.begin());
  CHECK_EQ(writes, 1);
  CHECK_EQ(delays, 0);
  CHECK(!control.set_brightness(100));
  CHECK_EQ(writes, 2);
}
