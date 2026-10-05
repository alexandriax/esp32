#pragma once

#include <stddef.h>
#include <stdint.h>

namespace moss_lhp {

// Native 480-pixel output without a screen-sized framebuffer. The board's
// CO5300 transport requires even y/height; pair adjacent RGB888 scanlines.
// All calls, including the blocking writer callback, belong to one task.
class StripeSink {
 public:
  static constexpr unsigned kWidth = 480;
  static constexpr unsigned kMaxHeight = 480;
  static constexpr size_t kRowBytes = kWidth * 3;
  static constexpr size_t kBufferBytes = kWidth * 2 * 2;
  typedef bool (*Writer)(void* context, unsigned y, unsigned width,
                         unsigned height, const uint8_t* pixels, size_t bytes);

  enum class State { Idle, Rendering, Complete, Failed, Cancelled };

  StripeSink(Writer writer, void* context) : writer_(writer), context_(context) {}

  bool begin(unsigned top = 0, unsigned height = kMaxHeight) {
    rows_ = presented_ = 0;
    top_ = top;
    height_ = height;
    if (!writer_ || top > kMaxHeight || !height || height > kMaxHeight - top ||
        (top & 1) || (height & 1)) {
      state_ = State::Failed;
      return false;
    }
    state_ = State::Rendering;
    return true;
  }

  // y is relative to this render's viewport. Each row must arrive once and
  // in order. Invalid input or a failed panel write poisons the render.
  bool row(unsigned y, const uint8_t* rgb, size_t bytes) {
    if (state_ != State::Rendering) return false;
    if (!rgb || bytes != kRowBytes || y != rows_ || rows_ >= height_) {
      state_ = State::Failed;
      return false;
    }
    uint8_t* output = stripe_ + (rows_ & 1) * kWidth * 2;
    for (unsigned x = 0; x < kWidth; ++x) {
      const uint16_t pixel = static_cast<uint16_t>(
          ((rgb[3 * x] & 0xf8u) << 8) |
          ((rgb[3 * x + 1] & 0xfcu) << 3) | (rgb[3 * x + 2] >> 3));
      output[2 * x] = static_cast<uint8_t>(pixel);
      output[2 * x + 1] = static_cast<uint8_t>(pixel >> 8);
    }
    ++rows_;
    if (!(rows_ & 1)) {
      if (!writer_(context_, top_ + rows_ - 2, kWidth, 2, stripe_, sizeof(stripe_))) {
        state_ = State::Failed;
        return false;
      }
      presented_ += 2;
    }
    return true;
  }

  bool finish() {
    if (state_ != State::Rendering) return false;
    if (rows_ != height_ || presented_ != height_) {
      state_ = State::Failed;
      return false;
    }
    state_ = State::Complete;
    return true;
  }

  void cancel() { state_ = State::Cancelled; }
  State state() const { return state_; }
  unsigned rowsAccepted() const { return rows_; }
  unsigned rowsPresented() const { return presented_; }

 private:
  Writer writer_;
  void* context_;
  State state_ = State::Idle;
  unsigned top_ = 0, height_ = 0, rows_ = 0, presented_ = 0;
  uint8_t stripe_[kBufferBytes] = {};
};

}  // namespace moss_lhp
