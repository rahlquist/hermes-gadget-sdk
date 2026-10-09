// PDM microphone (ESP32-P4 CrowPanel Advanced 10.1"): a reader task posts 20 ms
// PCM16 chunks while capturing, mirroring I2sMic. The board's mic is a PDM
// device on I2S0 (clk GPIO24, data GPIO26); the speaker uses standard I2S on
// I2S1. PDM RX decimates the 1-bit stream to 16-bit PCM in hardware.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <algorithm>
#include <cstring>

#include "driver/i2s_pdm.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace hgp {
namespace {

const char* TAG = "hg.pdmmic";
constexpr size_t kMicChunk = 320;  // 20 ms at 16 kHz

}  // namespace

bool PdmMic::begin(const I2sMicConfig& cfg) {
  // PDM mic lives on I2S0; the speaker takes I2S1 (see board config).
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.dma_desc_num = 6;
  chan.dma_frame_num = 256;
  chan.auto_clear_after_cb = true;
  chan.auto_clear_before_cb = true;
  if (i2s_new_channel(&chan, nullptr, &rx_) != ESP_OK) return false;

  i2s_pdm_rx_config_t pdm_cfg = {};
  pdm_cfg.clk_cfg.sample_rate_hz = rate_;
  pdm_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  pdm_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  pdm_cfg.clk_cfg.dn_sample_mode = I2S_PDM_DSR_8S;
  pdm_cfg.clk_cfg.bclk_div = 8;
  pdm_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
  pdm_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
  pdm_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
  pdm_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
  pdm_cfg.slot_cfg.hp_en = true;              // high-pass removes DC offset
  pdm_cfg.slot_cfg.hp_cut_off_freq_hz = 35.5;
  pdm_cfg.slot_cfg.amplify_num = 1;
  pdm_cfg.gpio_cfg.clk = static_cast<gpio_num_t>(cfg.sck);
  pdm_cfg.gpio_cfg.din = static_cast<gpio_num_t>(cfg.sd);
  pdm_cfg.gpio_cfg.invert_flags.clk_inv = false;

  if (i2s_channel_init_pdm_rx_mode(rx_, &pdm_cfg) != ESP_OK) return false;
  xTaskCreate(&PdmMic::task, "hg-pdmmic", 4096, this, 6, nullptr);
  ESP_LOGI(TAG, "pdm microphone ready (clk %d, data %d)", cfg.sck, cfg.sd);
  return true;
}

bool PdmMic::start(uint32_t sample_rate) {
  if (!rx_) return false;
  if (sample_rate != rate_) {
    i2s_pdm_rx_clk_config_t clk = {};
    clk.sample_rate_hz = sample_rate;
    clk.clk_src = I2S_CLK_SRC_DEFAULT;
    clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    clk.dn_sample_mode = I2S_PDM_DSR_8S;
    clk.bclk_div = 8;
    if (i2s_channel_reconfig_pdm_rx_clock(rx_, &clk) != ESP_OK) return false;
    rate_ = sample_rate;
  }
  if (i2s_channel_enable(rx_) != ESP_OK) return false;
  capturing_ = true;
  return true;
}

void PdmMic::stop() {
  if (!capturing_) return;
  capturing_ = false;
  i2s_channel_disable(rx_);
}

void PdmMic::task(void* arg) {
  auto* self = static_cast<PdmMic*>(arg);
  int16_t pcm[kMicChunk];
  for (;;) {
    if (!self->capturing_) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    size_t got = 0;
    if (i2s_channel_read(self->rx_, pcm, sizeof(pcm), &got, pdMS_TO_TICKS(100)) != ESP_OK || got == 0) continue;
    size_t n = got / sizeof(int16_t);
    // PDM output is quiet relative to an I2S MEMS mic; apply a fixed gain and
    // clamp, matching the vendor's record path (bsp_mic.c amplifies ~10x).
    for (size_t i = 0; i < n; ++i) {
      int32_t s = pcm[i] * 10;
      pcm[i] = static_cast<int16_t>(std::max<int32_t>(-32768, std::min<int32_t>(32767, s)));
    }
    if (self->capturing_) events::post(EventType::Mic, pcm, n * sizeof(int16_t));
  }
}

}  // namespace hgp
