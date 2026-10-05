#pragma once
#include <stdint.h>
namespace browser_app {
enum class Destination : uint8_t { None, Menu, Wifi };
// Stable diagnostic IDs. Snapshot contains no URL, body, SSID or credentials.
enum class Phase : uint8_t { Idle=0, Cancelling=1, Joining=2, Clock=3, Fetch=4, Render=5, Ready=6, Closed=7 };
struct Snapshot {
  bool active=false;
  Phase phase=Phase::Closed;
  uint8_t page=0; // BrowserPage: Page=0, Url=1, ConfirmExit=2.
  int16_t focus=-1;
  bool loading=false, hasPage=false;
  uint8_t historyCount=0, fetchState=0, fetchError=0; // browser_fetch enum IDs.
  uint8_t images=0,styles=0,cacheHits=0,skipped=0;bool sdCache=false;
  uint16_t httpStatus=0;
  uint32_t received=0;
  uint8_t engineError=0; // BrowserEngineError ID; retained after failure cleanup.
  uint32_t enginePeak=0, engineLive=0;
  uint32_t readerPaintUs=0,readerPaints=0;
  uint16_t scroll=0, contentHeight=0,lines=0,links=0;bool textMode=true;
};
// Main-loop only, read-only and allocation-free. Results persist until navigation.
Snapshot snapshot();
// Main-loop only. Caller releases the pet canvas and all other radio owners first.
bool open(const char* url = nullptr, bool load = true);
bool active();
void tick(uint32_t now);
void next();
void activate();
void back();
void touch(bool down, int x, int y); // Logical 240x240 coordinates.
void cancelTouch();
void requestClose(Destination destination=Destination::Menu);
void close(Destination destination = Destination::Menu); // Cancels, then drains fetch worker.
Destination finished(); // Non-None only after sockets, engine and radio are released.
const char* url();
void destroy(); // Only after finished(); caller can then restore its framebuffer.
}
