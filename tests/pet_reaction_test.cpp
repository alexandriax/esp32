#include "../firmware/sloth_pet/pet_reaction.h"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>

int main() {
  for (uint32_t seed : {0u, 1u, 0xdeadbeefu, UINT32_MAX}) {
    sloth::PetReaction reaction;
    reaction.begin(seed);
    assert(reaction.active(0) == -1);
    int last = -1;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
      unsigned seen = 0;
      for (unsigned draw = 0; draw < sloth::kPetReactionCount; ++draw) {
        const uint32_t now = (cycle * sloth::kPetReactionCount + draw) * 5000u;
        const int id = reaction.start(now);
        assert(id >= 0 && id < sloth::kPetReactionCount);
        assert(id != last && !(seen & (1u << id)));
        seen |= 1u << id;
        assert(reaction.active(now + 3999) == id);
        assert(reaction.active(now + 4000) == -1);
        last = id;
      }
      assert(seen == (1u << sloth::kPetReactionCount) - 1);
    }
  }
  sloth::PetReaction reaction;
  const uint32_t start = UINT32_MAX - 1500;
  const int id = reaction.start(start);
  assert(reaction.active(start + 3999u) == id);
  assert(reaction.elapsed(start + 3000u) == 3000u);
  assert(reaction.active(start + 4000u) == -1);
  reaction.start(100);
  reaction.clear();
  assert(reaction.active(101) == -1);
  assert(reaction.elapsed(101) == 0);
  puts("pet_reaction: shuffled coverage, cycle boundaries, expiry, wrap, cancellation passed");
}
