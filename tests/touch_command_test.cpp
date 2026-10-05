#include "../firmware/sloth_pet/touch_command.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

using sloth::TouchCommand;
static TouchCommand::Result parse(TouchCommand& command, const char* packet) {
  command.begin(100);
  auto result = TouchCommand::Result::Pending;
  for (size_t i = 0; packet[i]; ++i) result = command.feed(packet[i]);
  return result;
}
int main() {
  TouchCommand c;
  assert(!c.active() && c.feed('1') == TouchCommand::Result::Rejected);
  assert(parse(c, "1,239,0\r\n") == TouchCommand::Result::Complete);
  assert(!c.active() && c.down() && c.x() == 239 && c.y() == 0);
  assert(parse(c, "0,0,239\n") == TouchCommand::Result::Complete);
  assert(!c.down() && c.x() == 0 && c.y() == 239);
  const char* invalid[] = {"2,1,1\n", "1,240,1\n", "1,1,240\n", "1,-1,0\n",
      "1,1\n", "1,,0\n", "1,1,0,1\n", "1,1,\n", "\n", "1,9999999999999999999999999,0\n",
      "1,0,0bqnfp\n", "garbagebfp\n"};
  for (const char* packet : invalid) assert(parse(c, packet) == TouchCommand::Result::Rejected);
  c.begin(UINT32_MAX - 499); c.feed('1'); c.expire(499); assert(c.active());
  c.expire(500); assert(c.active());
  assert(c.feed('b') == TouchCommand::Result::Pending && c.active());
  assert(c.feed('\n') == TouchCommand::Result::Rejected && !c.active());
  c.begin(0); c.feed('1'); c.discard();
  assert(c.active() && c.feed('f') == TouchCommand::Result::Pending);
  assert(c.feed('\n') == TouchCommand::Result::Rejected && !c.active());
  c.begin(0); c.cancel(); assert(!c.active());
  puts("Touch diagnostics: complete packets, bounds, invalid command isolation, cancellation and timeout wrap passed");
}
