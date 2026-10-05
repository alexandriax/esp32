#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sloth {
enum class RadioKind : uint8_t { Wifi, Bluetooth };
enum class RadioScanState : uint8_t { Idle, Starting, Scanning, Stopping, Error };
enum class RadioPage : uint8_t { List, Detail };
enum class RadioEvent : uint8_t { None, Changed, Back, SelectionChanged, Refresh };
constexpr unsigned kRadioCapacity = 24;
struct RadioEntry {
  uint8_t address[6] = {}, addressType = 0;
  char name[33] = {};
  int8_t rssi = -127, txPower = 127;
  uint8_t channel = 0, security = 0, advertising[64] = {}, advertisingLength = 0;
  bool advertisingTruncated = false;
  uint32_t seenAt = 0, observations = 0;
};
struct RadioSnapshot {
  RadioKind kind = RadioKind::Wifi;
  RadioScanState state = RadioScanState::Idle;
  RadioEntry entries[kRadioCapacity];
  uint8_t count = 0;
  uint32_t generation = 0, dropped = 0;
  int error = 0;
};
// Portable bounded metadata parsing. Never follows advertised URLs or connects.
void radioName(char* out, size_t capacity, const uint8_t* data, size_t length);
void radioAddress(char* out, size_t capacity, const uint8_t address[6]);
const char* radioSecurity(uint8_t security);
void radioAdvertising(RadioEntry& entry, const uint8_t* bytes, size_t length);
void radioServiceSummary(const RadioEntry& entry, char* out, size_t capacity);
// Keeps a bounded stable insertion order and updates by address/type. Full lists
// keep their current devices until Refresh, so a moving target cannot shift focus.
void radioObserve(RadioSnapshot& snapshot, const RadioEntry& entry);

struct RadioHint {
  int direction = 0; // -1 stronger left, +1 stronger right, 0 insufficient evidence.
  int trend = 0; // Rounded recent dB change; this is not distance or a bearing.
  unsigned samples = 0;
  bool motionReady = false, fresh = false;
  float sweepDegrees = 0, correlation = 0;
};
class RadioSweep {
 public:
  void reset();
  // Native right-handed gyro (degrees/s) and acceleration (g), same axes.
  // Project rotation onto measured vertical, rather than integrating translation.
  void motion(float gx, float gy, float gz, float ax, float ay, float az, uint32_t now);
  void observe(int rssi, uint32_t seenAt);
  RadioHint hint(uint32_t now) const;
 private:
  struct Heading { float angle = 0; uint32_t at = 0; } headings_[96];
  struct Point { float angle = 0; int16_t rssi = 0; uint32_t at = 0; } points_[24];
  unsigned headingCount_ = 0, headingHead_ = 0, pointCount_ = 0, pointHead_ = 0;
  float angle_ = 0;
  uint32_t motionAt_ = 0, lastSeen_ = 0;
  bool motionReady_ = false, haveSeen_ = false;
};
class RadioExplorerUi {
 public:
  RadioExplorerUi()=default;
  ~RadioExplorerUi();
  RadioExplorerUi(const RadioExplorerUi&)=delete;
  RadioExplorerUi& operator=(const RadioExplorerUi&)=delete;
  size_t retainedBytes() const;
  enum { kRefresh = 50, kBack = 51, kMore = 52, kNavBack = 100, kNavNext = 101, kNavSelect = 102 };
  void open(RadioKind kind);
  void close();
  bool isOpen() const { return open_; }
  RadioPage page() const { return page_; }
  int focus() const { return focus_; }
  int scrollOffset() const { return scroll_; }
  int maxScroll() const;
  const RadioSnapshot& snapshot() const;
  const RadioEntry* selected() const;
  void update(const RadioSnapshot& snapshot, uint32_t now);
  void motion(float gx, float gy, float gz, float ax, float ay, float az, uint32_t now);
  RadioHint hint(uint32_t now) const;
  void next();
  RadioEvent activate(int target = -1);
  RadioEvent back();
  int hitTest(int x, int y) const;
  RadioEvent touch(bool down, int x, int y);
  // After cancellation a held finger must release; feed that release even when
  // the caller also has a global touchNeedsRelease gate.
  void cancelTouch();
 private:
  struct Content;
  Content* content_=nullptr;
  bool open_ = false, touching_ = false, dragged_ = false, releaseRequired_ = false, scrollGesture_ = false;
  RadioPage page_ = RadioPage::List;
  int focus_ = kRefresh, scroll_ = 0, selected_ = -1;
  int touchX_ = 0, touchY_ = 0, touchScroll_ = 0, touchTarget_ = -1;
  void reveal();
};
void drawRadioExplorer(uint16_t* pixels, const RadioExplorerUi& ui, uint32_t now);
} // namespace sloth

namespace radio_explorer {
// Exclusive radio owner. Call only after wifi_display::busy() is false. start,
// stop and track are nonblocking; wait for busy()==false before Remote Display.
bool start(sloth::RadioKind kind);
void stop();
bool busy();
void copySnapshot(sloth::RadioSnapshot& output);
// Main-loop alternative to copySnapshot: copy directly into the UI's bounded
// snapshot under the driver lock, avoiding a third persistent 2.9KB snapshot.
void updateUi(sloth::RadioExplorerUi& ui, uint32_t now);
// nullptr returns to all-device discovery. Selection is copied, not retained.
void track(const sloth::RadioEntry* entry);
void refresh();
} // namespace radio_explorer
