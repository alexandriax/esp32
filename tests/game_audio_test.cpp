#include "../firmware/sloth_pet/game_audio.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

using sloth::GameAudio;
using sloth::GameSound;

namespace {
uint32_t hashPcm(const int16_t* samples, size_t count) {
  uint32_t hash=2166136261u;
  for(size_t i=0;i<count;++i) {
    const uint16_t value=static_cast<uint16_t>(samples[i]);
    hash=(hash^static_cast<uint8_t>(value))*16777619u;
    hash=(hash^static_cast<uint8_t>(value>>8))*16777619u;
  }
  return hash;
}
bool audible(const int16_t* samples, size_t count) {
  bool found = false;
  for (size_t i = 0; i < count; ++i) {
    assert(samples[i] >= -6000 && samples[i] <= 6000);
    found |= samples[i] != 0;
  }
  return found;
}

void checkSwitchesAndReset() {
  GameAudio synth;
  int16_t samples[16000], reference[16000];
  assert(!synth.needsAudio());
  synth.trigger(GameSound::GameOver);
  synth.render(samples, 16000);
  assert(!audible(samples, 16000) && !synth.needsAudio());

  synth.configure(0, true, false);
  assert(synth.needsAudio());
  synth.render(reference, 16000);
  assert(audible(reference, 16000));
  synth.reset();
  synth.trigger(GameSound::LineClear);  // Disabled effects cannot alter music.
  synth.render(samples, 16000);
  assert(memcmp(reference, samples, sizeof(samples)) == 0);
  synth.configure(0, false, false);
  synth.render(samples, 16000);
  assert(!audible(samples, 16000));

  synth.configure(0, false, true);
  assert(!synth.needsAudio());
  synth.trigger(GameSound::Serve);
  assert(synth.needsAudio());
  synth.render(samples, 16000);
  assert(audible(samples, 16000) && !synth.needsAudio());
  assert(!audible(samples + 2000, 14000));
  synth.trigger(GameSound::GameOver);
  synth.configure(0, false, false);
  assert(!synth.needsAudio());
  synth.render(samples, 16000);
  assert(!audible(samples, 16000));
  synth.configure(0, false, true);
  synth.trigger(GameSound::GameOver);
  synth.reset();
  assert(!synth.needsAudio());
  synth.render(samples, 16000);
  assert(!audible(samples, 16000));
}

void checkSongsAndChunking() {
  GameAudio pong, tetris, chunked;
  pong.configure(0, true, false);
  tetris.configure(1, true, false);
  chunked.configure(0, true, false);
  static int16_t full[128000], small[128000], different[128000];
  pong.render(nullptr, 1000);  // Null and empty renders do not consume time.
  pong.render(full, 0);
  pong.render(full, 128000);
  chunked.render(small, 7);
  chunked.render(small + 7, 12345);
  chunked.render(small + 12352, 128000 - 12352);
  tetris.render(different, 128000);
  assert(audible(full, 128000) && audible(different, 128000));
  // Recorded before adding Leaf Sweep: existing songs must stay byte-identical.
  assert(hashPcm(full,128000)==0xbd635c5eu);
  assert(hashPcm(different,128000)==0x9ba45449u);
  assert(memcmp(full, small, sizeof(full)) == 0);
  assert(memcmp(full, different, sizeof(full)) != 0);
  // A complete Pong loop has the exact same oscillator and envelope state.
  pong.render(small, 128000);
  assert(memcmp(full, small, sizeof(full)) == 0);
  // Switching tracks resets their clocks; repeated configure calls do not.
  tetris.configure(99, true, false);
  tetris.render(small, 128000);
  assert(memcmp(full, small, sizeof(full)) == 0);
  chunked.reset();
  chunked.render(small, 123);
  chunked.configure(0, true, false);
  chunked.render(small + 123, 128000 - 123);
  assert(memcmp(full, small, sizeof(full)) == 0);
  GameAudio leaf;
  leaf.configure(2,true,false); leaf.render(small,128000);
  assert(audible(small,128000));
  assert(memcmp(small,full,sizeof(small))!=0 && memcmp(small,different,sizeof(small))!=0);
  // Leaf's 32-note loop is 7.2 seconds. Exact looping and arbitrary chunks use
  // only the sample clock, independent of frame rate and the previous track.
  leaf.reset(); leaf.render(full,115200); leaf.render(small,115200);
  assert(!memcmp(full,small,115200*sizeof(int16_t)));
  chunked.configure(2,true,false);
  chunked.render(different,431); chunked.render(different+431,115200-431);
  assert(!memcmp(full,different,115200*sizeof(int16_t)));
  leaf.configure(2,false,true);
  leaf.render(small,128000); assert(!audible(small,128000));
  leaf.trigger(GameSound::LeafCollect); leaf.render(small,16000);
  assert(audible(small,16000) && !leaf.needsAudio());
}

void checkCuesAndPriorities() {
  int16_t samples[16000], reference[16000];
  GameAudio synth, comparison;
  for (unsigned track = 0; track < 3; ++track) {
    for (unsigned music = 0; music < 2; ++music) {
      for (unsigned cue = 0; cue <= static_cast<unsigned>(GameSound::CanHit); ++cue) {
        synth.configure(track, music, true);
        synth.reset();
        synth.trigger(static_cast<GameSound>(cue));
        synth.render(samples, 16000);
        assert(audible(samples, 16000));
        assert(synth.needsAudio() == (music != 0));
        if (!music) {
          assert(samples[0] == 0 && samples[15999] == 0);
          // Soft envelopes bound adjacent sample jumps, including note joins.
          for (unsigned i = 1; i < 16000; ++i) {
            int delta = samples[i] - samples[i - 1];
            assert(delta > -1000 && delta < 1000);
          }
        }
      }
    }
  }
  synth.configure(0, false, true);
  comparison.configure(0, false, true);
  synth.reset(); comparison.reset();
  synth.trigger(GameSound::GameOver); comparison.trigger(GameSound::GameOver);
  synth.render(samples, 1000); comparison.render(reference, 1000);
  synth.trigger(GameSound::Move);  // No interruption of important cues.
  synth.trigger(static_cast<GameSound>(255));
  synth.render(samples, 16000); comparison.render(reference, 16000);
  assert(memcmp(samples, reference, sizeof(samples)) == 0);
  synth.trigger(GameSound::Move);
  synth.trigger(GameSound::GameOver);  // Important cue replaces ordinary tick.
  comparison.trigger(GameSound::GameOver);
  synth.render(samples, 16000); comparison.render(reference, 16000);
  assert(memcmp(samples, reference, sizeof(samples)) == 0);
  // Guard samples remain untouched for arbitrary small output sizes.
  int16_t guarded[5] = {12345, 0, 0, 0, -12345};
  synth.trigger(GameSound::Move);
  synth.render(guarded + 1, 3);
  assert(guarded[0] == 12345 && guarded[4] == -12345);
  synth.reset(); comparison.reset();
  synth.trigger(GameSound::LeafCollect); comparison.trigger(GameSound::CanHit);
  synth.render(samples,16000); comparison.render(reference,16000);
  assert(audible(samples,16000) && audible(reference,16000));
  assert(memcmp(samples,reference,sizeof(samples))!=0);
  for(unsigned i=0;i<16000;++i) {
    assert(samples[i]>=-2000 && samples[i]<=2000);
    assert(reference[i]>=-2200 && reference[i]<=2200);
  }
  synth.trigger(GameSound::CanHit); comparison.trigger(GameSound::CanHit);
  synth.trigger(GameSound::LeafCollect); // Can hit takes precedence over reward.
  synth.render(samples,16000); comparison.render(reference,16000);
  assert(!memcmp(samples,reference,sizeof(samples)));
}
}  // namespace

int main() {
  static_assert(sizeof(GameAudio) <= 64, "Keep game audio state small");
  static_assert(static_cast<unsigned>(GameSound::GameOver)==8 &&
                static_cast<unsigned>(GameSound::LeafCollect)==9 &&
                static_cast<unsigned>(GameSound::CanHit)==10,"Append-only sound ids");
  checkSwitchesAndReset();
  checkSongsAndChunking();
  checkCuesAndPriorities();
  printf("Game audio checks passed: three distinct tracks, unchanged old-song golden PCM, independent switches, original loops, chunk invariance, Leaf/can textures, reset, priority, envelopes, quiet bounds, output guards\n");
}
