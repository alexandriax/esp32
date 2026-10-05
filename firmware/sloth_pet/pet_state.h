#ifndef SLOTH_PET_PET_STATE_H
#define SLOTH_PET_PET_STATE_H

#include <stdint.h>

// Portable game rules: no Arduino dependency, allocation, or wall-clock access.
// The caller supplies elapsed seconds, including time away when a clock is known.
namespace sloth {

enum class Action : uint8_t { Feed = 0, Play = 1, Nap = 2, Shake = 3 };
enum class Response : uint8_t {
  Fed,
  Full,
  Played,
  TooHungry,
  TooTired,
  FellAsleep,
  WokeUp,
  Danced,
  Sleeping
};

inline const char* actionName(Action action) {
  switch (action) {
    case Action::Feed: return "Feed";
    case Action::Play: return "Play";
    case Action::Nap: return "Nap";
    case Action::Shake: return "Shake";
  }
  return "";
}

inline const char* message(Response response) {
  switch (response) {
    case Response::Fed: return "Leaf snacks! Yum.";
    case Response::Full: return "My tummy is full!";
    case Response::Played: return "That was tree-mendous!";
    case Response::TooHungry: return "A snack before we play?";
    case Response::TooTired: return "A little nap first?";
    case Response::FellAsleep: return "Time for a cozy nap...";
    case Response::WokeUp: return "Hello again, friend!";
    case Response::Danced: return "Leaf party! Wiggle, wiggle!";
    case Response::Sleeping: return "Shh... still napping.";
  }
  return "";
}

// POD suitable for a versioned persistence record. Store fields explicitly if
// persistence must survive compiler/architecture changes (struct padding varies).
// Fractional clocks preserve decay progress across saves. sleeping is 0 or 1.
struct Snapshot {
  uint8_t fullness;
  uint8_t happiness;
  uint8_t energy;
  uint8_t sleeping;
  uint16_t fullness_seconds;
  uint16_t happiness_seconds;
  uint16_t energy_seconds;
  // Observed lifetime elapsed seconds, including known time away. The original
  // field name remains for persistence compatibility; the counter saturates.
  uint32_t active_seconds;
};

class PetState {
 public:
  // Needs: 20 food/hour, 12 happiness/hour, 6 energy/hour awake.
  // Napping restores 20 energy/minute. Needs never cause death or reset progress.
  enum {
    kFullnessPeriodSeconds = 180,
    kHappinessPeriodSeconds = 300,
    kAwakeEnergyPeriodSeconds = 600,
    kSleepEnergyPeriodSeconds = 3,
    kMinimumPlayFullness = 10,
    kMinimumPlayEnergy = 12
  };

  PetState() { reset(); }

  void reset() {
    state_ = Snapshot();
    state_.fullness = 80;
    state_.happiness = 80;
    state_.energy = 75;
  }

  Snapshot snapshot() const { return state_; }

  // Reject corrupt/incompatible values atomically; retain the current state.
  // The persistence layer should additionally check its own version/checksum.
  bool restore(const Snapshot& saved) {
    if (saved.fullness > 100 || saved.happiness > 100 || saved.energy > 100 ||
        saved.sleeping > 1 ||
        saved.fullness_seconds >= kFullnessPeriodSeconds ||
        saved.happiness_seconds >= kHappinessPeriodSeconds ||
        saved.energy_seconds >= (saved.sleeping ? kSleepEnergyPeriodSeconds
                                               : kAwakeEnergyPeriodSeconds)) {
      return false;
    }
    state_ = saved;
    return true;
  }

  // O(1), deterministic for any uint32_t interval, including UINT32_MAX.
  // Splitting an interval across calls produces the same state as one call.
  void advance(uint32_t seconds) {
    state_.fullness = subtract(state_.fullness,
        ticks(state_.fullness_seconds, seconds, kFullnessPeriodSeconds));
    state_.happiness = subtract(state_.happiness,
        ticks(state_.happiness_seconds, seconds, kHappinessPeriodSeconds));
    const uint32_t energy_ticks = ticks(state_.energy_seconds, seconds,
        state_.sleeping ? kSleepEnergyPeriodSeconds : kAwakeEnergyPeriodSeconds);
    state_.energy = state_.sleeping ? add(state_.energy, energy_ticks)
                                   : subtract(state_.energy, energy_ticks);
    state_.active_seconds = seconds > UINT32_MAX - state_.active_seconds
        ? UINT32_MAX : state_.active_seconds + seconds;
  }

  // Feed: +18 fullness; Play: +14 happiness, -8 energy, -3 fullness.
  // Awake shake: +8 fullness, +12 happiness, no energy cost. Nap toggles sleeping.
  // A successful snack or game wakes the pet. Shakes leave a napping pet alone.
  // Refused actions leave every field unchanged.
  Response act(Action action) {
    switch (action) {
      case Action::Feed:
        if (state_.fullness == 100) return Response::Full;
        wake();
        state_.fullness = add(state_.fullness, 18);
        return Response::Fed;
      case Action::Play:
        if (state_.fullness < kMinimumPlayFullness) return Response::TooHungry;
        if (state_.energy < kMinimumPlayEnergy) return Response::TooTired;
        wake();
        state_.happiness = add(state_.happiness, 14);
        state_.energy = subtract(state_.energy, 8);
        state_.fullness = subtract(state_.fullness, 3);
        return Response::Played;
      case Action::Nap:
        state_.sleeping = state_.sleeping ? 0 : 1;
        state_.energy_seconds = 0;
        return state_.sleeping ? Response::FellAsleep : Response::WokeUp;
      case Action::Shake:
        if (state_.sleeping) return Response::Sleeping;
        state_.fullness = add(state_.fullness, 8);
        state_.happiness = add(state_.happiness, 12);
        return Response::Danced;
    }
    return Response::WokeUp;
  }

 private:
  Snapshot state_;

  static uint8_t add(uint8_t value, uint32_t amount) {
    return amount >= static_cast<uint32_t>(100 - value)
        ? 100 : static_cast<uint8_t>(value + amount);
  }

  static uint8_t subtract(uint8_t value, uint32_t amount) {
    return amount >= value ? 0 : static_cast<uint8_t>(value - amount);
  }

  static uint32_t ticks(uint16_t& remainder, uint32_t seconds, uint16_t period) {
    const uint64_t elapsed = static_cast<uint64_t>(remainder) + seconds;
    remainder = static_cast<uint16_t>(elapsed % period);
    return static_cast<uint32_t>(elapsed / period);
  }

  void wake() {
    if (state_.sleeping) {
      state_.sleeping = 0;
      state_.energy_seconds = 0;
    }
  }
};

}  // namespace sloth

#endif  // SLOTH_PET_PET_STATE_H
