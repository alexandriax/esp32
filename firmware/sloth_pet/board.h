#pragma once
#include <stddef.h>
#include <stdint.h>

namespace board {
constexpr int kKeyPin = 10;
constexpr int kBootPin = 9;
void begin();
// Shared screen setting for UI frames and streamed browser stripes.
void setScreenRotation(unsigned turns);
unsigned screenRotation();
constexpr unsigned kConfiguredRotation = 4;
// Decode completely into the pet canvas while exclusively borrowing the idle
// DMA stripe's storage. No extra decoder allocation. Returns false before any
// write if uninitialized, asleep, presenting (including between-stripe input
// callbacks), or already decoding. Scratch and decoder lifetime end before
// return; only a successful result may subsequently be presented/ACKed.
bool decodeJpeg(const uint8_t* data, size_t bytes, uint16_t* pixels, size_t pixelCapacity);
// 240x240 native-endian RGB565, scaled to the 480x480 panel.
// Defaults to the saved orientation. Explicit turns (0..3) override it; Remote
// Display passes 0 because the companion already rotates its source pixels.
// Rotation fills the existing DMA stripe without another framebuffer.
// Optional loop-task callback runs between completed DMA stripes to keep
// sensor polling responsive. It must not draw, change display power, or call
// present recursively. Frames requested while the panel sleeps are ignored.
void present(const uint16_t* pixels, void (*serviceInputs)() = nullptr, unsigned turns = kConfiguredRotation);
// RGB565 LE rectangle, optionally 2x scaled and/or run-length encoded as
// {countLE16,pixelLE16}. Logical coordinates expand with scale. Physical
// geometry must be even and fit 480*16 pixels. Invalid input never starts DMA.
// Copies into the existing DMA stripe and waits for completion before return.
bool presentRegion(unsigned x, unsigned y, unsigned width, unsigned height,
                   const uint8_t* littleEndianPixels, unsigned bytes,
                   unsigned scale = 1, bool rle = false, unsigned turns = kConfiguredRotation);
// Brightness is retained across sleep. A change while asleep is applied on wake.
void brightness(uint8_t percent);
// Main-loop operations only, never from a present() callback. All DMA is finished
// when present returns. These leave the PMIC rails, RTC, and reset pin unchanged.
void sleepDisplay();  // Display off + sleep in; waits 120 ms for the transition.
void wakeDisplay();   // Sleep out, waits 600 ms, restores config/brightness; off.
// After wakeDisplay(), upload a complete fresh frame with present() before this
// call. showDisplay rejects an unprepared frame so old RAM contents stay hidden.
void showDisplay();   // Display on; waits the vendor's 100 ms settling interval.
}
