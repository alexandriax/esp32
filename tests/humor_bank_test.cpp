#include "../firmware/sloth_pet/pet_humor.h"
#include "../firmware/sloth_pet/pet_speech.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
  using namespace sloth;
  assert(jokeCount() >= 2048 && jokeCount() <= PetHumor::kMaxJokes);
  size_t moodCounts[8] = {}, topicCounts[12] = {}, lineCounts[kSpeechLines] = {};
  for (size_t id = 0; id < jokeCount(); ++id) {
    const Joke& joke = jokeAt(id);
    assert(joke.text && *joke.text && std::strlen(joke.text) <= 132);
    const auto lines = wrapSpeech(joke.text);
    assert(lines.count && lines.count <= kSpeechLines && !lines.truncated);
    ++lineCounts[lines.count - 1];
    const auto bubble = speechLayout(joke.text);
    // Every shipped saying fits above the meters and keeps the full-size eyes
    // (around y77) visible, including the six-pixel upward pointer.
    assert(bubble.y - 6 > 84 && bubble.y + bubble.height <= 168);
    assert(bubble.x >= 16 && bubble.x + bubble.width <= 224);
    size_t characters = 0;
    for (unsigned row = 0; row < lines.count; ++row) {
      assert(std::strlen(lines.text[row]) <= kSpeechColumns);
      characters += std::strlen(lines.text[row]);
    }
    assert(characters + lines.count - 1 == std::strlen(joke.text));
    assert(static_cast<unsigned>(joke.mood) < 8 && joke.topic < 12);
    ++moodCounts[static_cast<unsigned>(joke.mood)]; ++topicCounts[joke.topic];
    assert(speechDurationMs(joke.text) >= 10000 && speechDurationMs(joke.text) <= 22000);
  }
  for (auto count : moodCounts) assert(count > 20);
  for (auto count : topicCounts) assert(count > 20);
  assert(!*jokeAt(jokeCount()).text);

  bool seen[PetHumor::kMaxJokes] = {};
  PetHumor humor; humor.begin(0xbad5eedu);
  Snapshot state{};
  state.fullness = 75; state.happiness = 75; state.energy = 75;
  uint8_t saved[PetHumor::kRecordSize];
  unsigned adjacentTopics = 0;
  uint8_t previousTopic = 255;
  for (size_t i = 0; i < jokeCount(); ++i) {
    const uint16_t id = humor.next(state);
    assert(id < jokeCount() && !seen[id]); seen[id] = true;
    adjacentTopics += jokeAt(id).topic == previousTopic; previousTopic = jokeAt(id).topic;
    if (i % 73 == 0) {
      assert(humor.encode(saved));
      PetHumor restarted; restarted.begin(123);
      assert(restarted.restore(saved, sizeof(saved)));
      assert(restarted.currentId() == id && restarted.seenCount() == i + 1);
      humor = restarted;
    }
    // Move through different care states while retaining the same unseen pool.
    state.fullness = (i * 17) % 101; state.happiness = (i * 31) % 101;
    state.energy = (i * 47) % 101; state.sleeping = (i % 19 == 0);
  }
  assert(humor.seenCount() == jokeCount() && humor.cycle() == 0);
  const auto last = humor.currentId();
  assert(humor.next(state) != last && humor.cycle() == 1 && humor.seenCount() == 1);
  assert(adjacentTopics < jokeCount() / 20);
  printf("Real corpus: %zu lines fit, every topic/mood represented, full unseen cycle with changing needs and repeated restore; %u adjacent-topic draws\n", jokeCount(), adjacentTopics);
  printf("Speech rows 1..7: %zu %zu %zu %zu %zu %zu %zu\n", lineCounts[0], lineCounts[1], lineCounts[2],
      lineCounts[3], lineCounts[4], lineCounts[5], lineCounts[6]);
}
