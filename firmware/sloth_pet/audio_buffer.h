#pragma once
#include <stddef.h>
#include <stdint.h>

namespace audio_output {
constexpr unsigned kSampleRate = 16000;
constexpr size_t kMaxPacketBytes = 3200;
constexpr size_t kBufferBytes = 12288;
constexpr size_t kPrefillSamples = 3840; // 240 ms before playback/recovery.
static_assert(kPrefillSamples <= (kBufferBytes-kMaxPacketBytes)/2, "Credit must allow playback prefill");
constexpr uint8_t kMaxVolume = 60;
inline bool canAcceptPacket(size_t queuedSamples) {
  return queuedSamples <= (kBufferBytes - kMaxPacketBytes) / 2;
}
inline bool validPcm(const uint8_t* data, size_t bytes) {
  return data && bytes >= 2 && bytes <= kMaxPacketBytes && !(bytes & 1);
}
inline bool validConfig(const uint8_t* data, size_t bytes) {
  return data && bytes == 4 && data[0] == 1 && data[1] <= 1 &&
         data[2] <= kMaxVolume && data[3] == 0;
}
struct Stats {
  uint32_t underruns = 0, overflows = 0, droppedSamples = 0, writeErrors = 0;
  // Accepted source samples and source samples consumed by render (including
  // fade-in, excluding generated silence/fade-out). Unsigned totals may wrap.
  uint32_t receivedSamples = 0, renderedSamples = 0;
  size_t queuedSamples = 0;
};

// Portable bounded PCM16LE queue. Caller serializes push/render/stats; the
// firmware holds a short critical section, never over I2S or I2C operations.
class AudioBuffer {
 public:
  bool push(const uint8_t* data, size_t bytes) {
    if (!validPcm(data, bytes)) return false;
    const size_t incoming = bytes / 2;
    stats_.receivedSamples += static_cast<uint32_t>(incoming);
    if (size_ + incoming > kCapacity) {
      const size_t dropped = size_ + incoming - kCapacity;
      read_ = (read_ + dropped) % kCapacity;
      size_ -= dropped;
      ++stats_.overflows;
      stats_.droppedSamples += static_cast<uint32_t>(dropped);
      fadeFrom_ = last_; fade_ = 0; // Smooth discontinuity after dropping old audio.
    }
    for (size_t i = 0; i < incoming; ++i) {
      const uint16_t bits = static_cast<uint16_t>(data[i*2]) |
                            static_cast<uint16_t>(data[i*2+1]) << 8;
      // Avoid implementation-defined unsigned-to-signed conversion.
      samples_[(read_ + size_) % kCapacity] = static_cast<int16_t>(
          bits < 0x8000 ? static_cast<int32_t>(bits) : static_cast<int32_t>(bits) - 65536);
      ++size_;
    }
    return true;
  }
  void render(int16_t* output, size_t count) {
    if (!output) return;
    uint32_t rendered = 0;
    if (!playing_ && size_ >= kPrefillSamples) {
      playing_ = true; fadeFrom_ = last_; fade_ = 0; tail_ = 0;
    }
    for (size_t i = 0; i < count; ++i) {
      if (playing_ && size_) {
        int32_t sample = samples_[read_];
        read_ = (read_ + 1) % kCapacity; --size_;
        ++rendered;
        if (fade_ < kRampSamples) {
          ++fade_;
          sample = (static_cast<int32_t>(fadeFrom_) * (kRampSamples - fade_) +
                    sample * fade_) / kRampSamples;
        }
        last_ = static_cast<int16_t>(sample);
      } else {
        if (playing_) {
          playing_ = false; ++stats_.underruns;
          tailFrom_ = last_; tail_ = kRampSamples;
        }
        if (tail_) {
          --tail_;
          last_ = static_cast<int16_t>(static_cast<int32_t>(tailFrom_) * tail_ / kRampSamples);
        } else last_ = 0;
      }
      output[i] = last_;
    }
    stats_.renderedSamples += rendered;
  }
  Stats stats() const { Stats result = stats_; result.queuedSamples = size_; return result; }
 private:
  static constexpr size_t kCapacity = kBufferBytes / 2;
  static constexpr int kRampSamples = 64; // Four milliseconds.
  int16_t samples_[kCapacity] = {};
  size_t read_ = 0, size_ = 0;
  bool playing_ = false;
  int fade_ = kRampSamples, tail_ = 0;
  int16_t last_ = 0, fadeFrom_ = 0, tailFrom_ = 0;
  Stats stats_;
};
} // namespace audio_output
