// I2S microphone (INMP441-style) and I2S amplifier (MAX98357A-style) on two
// separate I2S controllers, ESP-IDF 5.x standard-mode driver.
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <algorithm>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "speaker_queue.hpp"

namespace hgp {
namespace {

const char* TAG = "hg.audio";
constexpr size_t kMicChunk = 320;           // 20 ms at 16 kHz
constexpr size_t kSpeakerBuffer = 48 * 1024;  // ~1.5 s at 16 kHz; the server paces 0.5 s ahead
constexpr size_t kSpeakerChunk = 512;       // mono queue samples / stereo staging slots

i2s_std_config_t std_config(uint32_t rate, i2s_data_bit_width_t bits, int bclk, int ws, int dout, int din,
                           bool stereo = false, bool left_align = false) {
  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);
  if (stereo) cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  if (left_align) {
    // Left-justified framing with a one-bit shift, as the CrowPanel P4's NS4168
    // amp expects (matches Elecrow's bsp_audio.c). ws_width 16 bit like the data.
    cfg.slot_cfg.ws_width = bits;
    cfg.slot_cfg.left_align = true;
    cfg.slot_cfg.big_endian = false;
    cfg.slot_cfg.bit_order_lsb = false;
    cfg.slot_cfg.bit_shift = true;
  }
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(bclk);
  cfg.gpio_cfg.ws = static_cast<gpio_num_t>(ws);
  cfg.gpio_cfg.dout = dout < 0 ? I2S_GPIO_UNUSED : static_cast<gpio_num_t>(dout);
  cfg.gpio_cfg.din = din < 0 ? I2S_GPIO_UNUSED : static_cast<gpio_num_t>(din);
  return cfg;
}

}  // namespace

// --------------------------------------------------------------------------
// Microphone

bool I2sMic::begin(const I2sMicConfig& cfg) {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chan, nullptr, &rx_) != ESP_OK) return false;
  // INMP441 outputs 24-bit samples in a 32-bit left slot.
  i2s_std_config_t std_cfg = std_config(rate_, I2S_DATA_BIT_WIDTH_32BIT, cfg.sck, cfg.ws, -1, cfg.sd);
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  if (i2s_channel_init_std_mode(rx_, &std_cfg) != ESP_OK) return false;
  xTaskCreate(&I2sMic::task, "hg-mic", 4096, this, 6, nullptr);
  ESP_LOGI(TAG, "microphone ready");
  return true;
}

bool I2sMic::start(uint32_t sample_rate) {
  if (!rx_) return false;
  if (sample_rate != rate_) {
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    if (i2s_channel_reconfig_std_clock(rx_, &clk) != ESP_OK) return false;
    rate_ = sample_rate;
  }
  if (i2s_channel_enable(rx_) != ESP_OK) return false;
  capturing_ = true;
  return true;
}

void I2sMic::stop() {
  if (!capturing_) return;
  capturing_ = false;
  i2s_channel_disable(rx_);
}

void I2sMic::task(void* arg) {
  auto* self = static_cast<I2sMic*>(arg);
  int32_t raw[kMicChunk];
  int16_t pcm[kMicChunk];
  for (;;) {
    if (!self->capturing_) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    size_t got = 0;
    if (i2s_channel_read(self->rx_, raw, sizeof(raw), &got, 100) != ESP_OK || got == 0) continue;  // timeout_ms, not FreeRTOS ticks
    size_t n = got / sizeof(int32_t);
    for (size_t i = 0; i < n; ++i) {
      // 24-bit sample left-aligned in 32 bits; keep the top 16 with a little gain.
      int32_t s = raw[i] >> 13;
      pcm[i] = static_cast<int16_t>(std::max<int32_t>(-32768, std::min<int32_t>(32767, s)));
    }
    if (self->capturing_) events::post(EventType::Mic, pcm, n * sizeof(int16_t));
  }
}

// --------------------------------------------------------------------------
// Speaker

bool I2sSpeaker::begin(const I2sSpeakerConfig& cfg) {
  pa_ = cfg.pa;
  pa_active_low_ = cfg.pa_active_low;
  stereo_ = cfg.stereo;
  left_align_ = cfg.left_align;
  if (pa_ >= 0) {
    // External amplifier enable (e.g. NS4168 on the CrowPanel P4, active-low on
    // GPIO30). Start muted; the task raises it only while samples are playing.
    gpio_reset_pin(static_cast<gpio_num_t>(pa_));
    gpio_set_direction(static_cast<gpio_num_t>(pa_), GPIO_MODE_OUTPUT);
    set_pa(false);
  }
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(static_cast<i2s_port_t>(cfg.i2s_port), I2S_ROLE_MASTER);
  chan.auto_clear = true;  // silence on underrun instead of repeating the last buffer
  if (i2s_new_channel(&chan, &tx_, nullptr) != ESP_OK) return false;
  i2s_std_config_t std_cfg = std_config(rate_, I2S_DATA_BIT_WIDTH_16BIT, cfg.bclk, cfg.ws, cfg.dout, -1, stereo_, left_align_);
  if (i2s_channel_init_std_mode(tx_, &std_cfg) != ESP_OK) return false;
  uint8_t* storage = static_cast<uint8_t*>(heap_caps_malloc(kSpeakerBuffer + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  static StaticStreamBuffer_t control;
  if (storage) buffer_ = xStreamBufferCreateStatic(kSpeakerBuffer, 1, storage, &control);
  else buffer_ = xStreamBufferCreate(16 * 1024, 1);
  if (!buffer_) return false;
  xTaskCreate(&I2sSpeaker::task, "hg-spk", 4096, this, 7, nullptr);
  ESP_LOGI(TAG, "speaker ready on I2S%d%s", cfg.i2s_port, pa_ >= 0 ? " (with amplifier enable)" : "");
  return true;
}

void I2sSpeaker::set_pa(bool on) {
  if (pa_ < 0) return;
  gpio_set_level(static_cast<gpio_num_t>(pa_), (on != pa_active_low_) ? 1 : 0);
}

bool I2sSpeaker::begin(uint32_t sample_rate) {
  if (!tx_) return false;
  const bool pending = open_.load() || draining_.load() || !flush_done_.load() ||
                       (buffer_ && xStreamBufferBytesAvailable(buffer_) > 0);
  if (pending) {
    abort();
    for (int i = 0; i < 5 && !flush_done_.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (!flush_done_.load()) {
      ESP_LOGW(TAG, "speaker flush did not finish before new stream");
      return false;
    }
  }
  rate_ = sample_rate;  // applied by the writer task before it next enables the channel
  ESP_LOGI(TAG, "[GADGET-AUDIO-01] begin requested_rate=%u stereo=%d left_align=%d",
           static_cast<unsigned>(sample_rate), static_cast<int>(stereo_), static_cast<int>(left_align_));
  open_ = true;
  draining_ = false;
  return true;
}

void I2sSpeaker::write(const int16_t* samples, size_t count) {
  if (!open_) return;
  size_t bytes = count * sizeof(int16_t);
  // The app task is the only writer, so the room can only grow before the send.
  size_t fit = hg::whole_sample_bytes(bytes, xStreamBufferSpacesAvailable(buffer_));
  size_t sent = xStreamBufferSend(buffer_, samples, fit, 0);
  if (sent < bytes) ESP_LOGW(TAG, "playback buffer full, dropped %u bytes", static_cast<unsigned>(bytes - sent));
}

void I2sSpeaker::end() {
  open_ = false;
  draining_ = true;
}

void I2sSpeaker::abort() {
  open_ = false;
  draining_ = false;
  flush_done_ = false;
  flush_ = true;
}

bool I2sSpeaker::busy() const {
  return open_ || draining_ || !flush_done_.load() || (buffer_ && xStreamBufferBytesAvailable(buffer_) > 0);
}

void I2sSpeaker::task(void* arg) {
  // Owns every channel state change (enable, disable, clock) so the app task never races it.
  auto* self = static_cast<I2sSpeaker*>(arg);
  int16_t chunk[kSpeakerChunk];
  bool enabled = false;
  uint32_t applied_rate = self->rate_.load();
  for (;;) {
    if (self->flush_.exchange(false)) {
      xStreamBufferReset(self->buffer_);
      if (enabled) {
        i2s_channel_disable(self->tx_);
        self->set_pa(false);
        enabled = false;
      }
      self->flush_done_ = true;
    }
    size_t got = xStreamBufferReceive(self->buffer_, chunk, sizeof(chunk), pdMS_TO_TICKS(20));
    if (got == 0) {
      if (self->draining_ && !self->open_) {
        self->draining_ = false;  // everything queued has been written out
        if (enabled) {
          i2s_channel_disable(self->tx_);
          self->set_pa(false);
          enabled = false;
        }
      }
      continue;
    }
    if (!enabled) {
      uint32_t want = self->rate_.load();
      if (want != applied_rate) {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(want);
        const esp_err_t clock_err = i2s_channel_reconfig_std_clock(self->tx_, &clk);
        if (clock_err == ESP_OK) applied_rate = want;
        else ESP_LOGE(TAG, "[GADGET-AUDIO-01] clock_reconfigure_failed requested=%u applied=%u err=%d",
                      static_cast<unsigned>(want), static_cast<unsigned>(applied_rate), static_cast<int>(clock_err));
      }
      ESP_LOGI(TAG, "[GADGET-AUDIO-01] writer_enable rate=%u stereo=%d left_align=%d",
               static_cast<unsigned>(applied_rate), static_cast<int>(self->stereo_),
               static_cast<int>(self->left_align_));
      self->set_pa(true);  // amplifier on before the first sample, avoids a click
      const esp_err_t enable_err = i2s_channel_enable(self->tx_);
      if (enable_err != ESP_OK)
        ESP_LOGE(TAG, "[GADGET-AUDIO-01] channel_enable_failed rate=%u err=%d",
                 static_cast<unsigned>(applied_rate), static_cast<int>(enable_err));
      enabled = true;
    }
    size_t n = got / sizeof(int16_t);
    int vol = self->volume_.load();
    if (self->stereo_) {
      // The channel is framed stereo, so each mono sample must be written to
      // both slots (Elecrow's bsp_mic.c does the same before playback). The fixed buffer handles
      // larger reads in bounded slices without dropping input samples.
      int16_t stereo[kSpeakerChunk];
      size_t offset = 0;
      while (offset < n) {
        const size_t consumed = hg::mono_to_stereo(chunk + offset, n - offset, stereo, kSpeakerChunk);
        if (!consumed) break;
        for (size_t i = 0; i < consumed; ++i) {
          const int16_t sample = static_cast<int16_t>(chunk[offset + i] * vol / 100);
          stereo[2 * i] = sample;
          stereo[2 * i + 1] = sample;
        }
        size_t written = 0;
        const size_t requested = consumed * 2 * sizeof(int16_t);
        const esp_err_t write_err = i2s_channel_write(self->tx_, stereo, requested, &written, 200);  // timeout_ms, not FreeRTOS ticks
        if (write_err != ESP_OK || written != requested)
          ESP_LOGW(TAG, "[GADGET-AUDIO-01] i2s_write_short requested=%u written=%u err=%d",
                   static_cast<unsigned>(requested), static_cast<unsigned>(written), static_cast<int>(write_err));
        offset += consumed;
      }
    } else {
      for (size_t i = 0; i < n; ++i) chunk[i] = static_cast<int16_t>(chunk[i] * vol / 100);
      size_t written = 0;
      const size_t requested = n * sizeof(int16_t);
      const esp_err_t write_err = i2s_channel_write(self->tx_, chunk, requested, &written, 200);  // timeout_ms, not FreeRTOS ticks
      if (write_err != ESP_OK || written != requested)
        ESP_LOGW(TAG, "[GADGET-AUDIO-01] i2s_write_short requested=%u written=%u err=%d",
                 static_cast<unsigned>(requested), static_cast<unsigned>(written), static_cast<int>(write_err));
    }
  }
}

}  // namespace hgp
