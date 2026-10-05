#pragma once
#include "audio_buffer.h"

namespace audio_output {
// All public calls must run on the main loop after board::begin(). Default is
// off: start only after explicit session AudioConfig. stop on every display
// exit/disconnect. No ADC, microphone pin, or I2S RX channel is configured.
bool start(uint8_t volumePercent = 35);
void stop();
bool setVolume(uint8_t volumePercent); // Clamps to 60; zero mutes the amplifier.
bool pushPcm(const uint8_t* data, size_t bytes); // Bounded copy, no I2S wait.
bool active();
Stats stats();
} // namespace audio_output
