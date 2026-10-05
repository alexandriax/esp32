#include "../firmware/sloth_pet/audio_buffer.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <deque>
#include <random>
#include <algorithm>

using namespace audio_output;
static std::vector<uint8_t> pcm(size_t samples, int16_t value) {
  std::vector<uint8_t> bytes(samples * 2);
  for (size_t i=0; i<samples; ++i) {
    const uint16_t bits = static_cast<uint16_t>(value);
    bytes[i*2] = static_cast<uint8_t>(bits); bytes[i*2+1] = static_cast<uint8_t>(bits >> 8);
  }
  return bytes;
}
int main() {
  uint8_t config[] = {1, 1, 35, 0};
  assert(validConfig(config, 4));
  for (unsigned i=0; i<256; ++i) {
    config[2] = i; assert(validConfig(config, 4) == (i <= 60));
  }
  config[2]=35; config[1]=2; assert(!validConfig(config, 4)); config[1]=1;
  config[0]=2; assert(!validConfig(config, 4)); config[0]=1;
  config[3]=1; assert(!validConfig(config, 4)); config[3]=0;
  assert(!validConfig(nullptr,4)); assert(!validConfig(config,3));
  uint8_t bytes[] = {0xff, 0x7f};
  assert(!validPcm(nullptr,2)); assert(!validPcm(bytes,0)); assert(!validPcm(bytes,1));
  assert(!validPcm(bytes,3201)); assert(!validPcm(bytes,3202));
  assert(validPcm(bytes,2));
  AudioBuffer ring;
  int16_t output[256];
  ring.render(output,256); for(auto sample:output) assert(sample==0);
  auto block=pcm(1600,32767);
  // Credit must allow enough maximum-size packets to reach prefill.
  while(ring.stats().queuedSamples<kPrefillSamples){
    assert(canAcceptPacket(ring.stats().queuedSamples));
    assert(ring.push(block.data(),block.size()));
  }
  const unsigned received=ring.stats().receivedSamples;
  ring.render(output,256);
  assert(output[0]>0&&output[0]<1000&&output[63]==32767&&output[255]==32767);
  while(ring.stats().queuedSamples)ring.render(output,256);
  ring.render(output,256);ring.render(output,256);
  assert(ring.stats().underruns==1&&ring.stats().renderedSamples==received);
  for(auto sample:output)assert(sample==0);
  assert(canAcceptPacket((kBufferBytes-kMaxPacketBytes)/2));
  assert(!canAcceptPacket((kBufferBytes-kMaxPacketBytes)/2+1));
  // Playback credit prevents overflow even when a sender always offers its
  // maximum packet as soon as credit is granted. Exercise start and recovery.
  AudioBuffer paced;
  for(unsigned i=0;i<10000;++i){
    if(canAcceptPacket(paced.stats().queuedSamples))assert(paced.push(block.data(),block.size()));
    paced.render(output,256);assert(paced.stats().overflows==0);
  }
  assert(!paced.stats().underruns&&paced.stats().renderedSamples>2000000);
  AudioBuffer negative;block=pcm(1600,-32768);
  while(negative.stats().queuedSamples<kPrefillSamples)assert(negative.push(block.data(),block.size()));
  negative.render(output,256);assert(output[0]<0&&output[63]==-32768&&output[255]==-32768);
  // Every length and random payload rejected/accepted without escaping bounded storage.
  std::mt19937 random(42);
  struct Guarded { uint64_t before=0x0123456789abcdefULL; AudioBuffer ring; uint64_t after=0xfedcba9876543210ULL; } guarded;
  std::vector<uint8_t> fuzz(3300);
  uint32_t acceptedSamples = 0;
  for (unsigned n=0;n<10000;++n) {
    const size_t length=random()%fuzz.size();
    for(size_t i=0;i<length;++i) fuzz[i]=static_cast<uint8_t>(random());
    const bool expected=length>=2 && length<=3200 && !(length&1);
    assert(guarded.ring.push(fuzz.data(),length)==expected);
    if (expected) acceptedSamples += static_cast<uint32_t>(length / 2);
    assert(guarded.ring.stats().queuedSamples<=kBufferBytes/2);
    if(random()&1) guarded.ring.render(output,random()%257);
    const auto totals = guarded.ring.stats();
    assert(totals.receivedSamples == acceptedSamples);
    assert(totals.receivedSamples == totals.renderedSamples + totals.droppedSamples + totals.queuedSamples);
    assert(guarded.before==0x0123456789abcdefULL && guarded.after==0xfedcba9876543210ULL);
  }
  puts("audio buffer: config/PCM bounds, prefill, signed samples, fades, overflow/underrun recovery and10K mutations passed");
}
