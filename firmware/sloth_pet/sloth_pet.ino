#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <lwip/tcpip.h>
#include <lwip/priv/tcp_priv.h>
#include "nvs_flash.h"
#include "board.h"
#include "pet_state.h"
#include "pet_record.h"
#include "pet_renderer.h"
#include "pet_humor.h"
#include "pet_speech.h"
#include "pet_reaction.h"
#include "sensors.h"
#include "gestures.h"
#include "pet_rtc.h"
#include "pet_timeline.h"
#include "local_time.h"
#include "pet_settings.h"
#include "settings_ui.h"
#include "storage_status.h"
#include "freertos/queue.h"
#include "menu_ui.h"
#include "radio_explorer.h"
#include "pong_game.h"
#include "tetris_game.h"
#include "tetris_controls.h"
#include "leaf_sweep.h"
#include "forest_fidget.h"
#include "forest_fidget_controls.h"
#include "touch_command.h"
#include "game_records.h"
#include "game_overlay.h"
#include "game_audio.h"
#include "remote_display_ui.h"
#include "remote_connection.h"
#include "pet_power.h"
#include "display_idle.h"
#include "display_controls.h"
#include "volume_overlay.h"
#include "display_stream.h"
#include "wifi_display.h"
#include "wifi_networks.h"
#include "wifi_networks_ui.h"
#include "browser_app.h"
#include "browser_fetch.h"
#include "browser_memory.h"
#include "app_runtime.h"
#include "screen_rotation.h"
#include "radio_explorer.h"
#include "audio_output.h"
#include <stdarg.h>
#include "esp_random.h"
#include "esp_heap_caps.h"

namespace {
sloth::PetState pet;
sloth::PetTimeline timeline;
Preferences storage;
sloth::Settings settings;
sloth::Settings pendingSettings;
sloth::SettingsUi settingsUi;
storage_status::Snapshot storageView;
struct StorageJob { storage_status::Snapshot result;std::atomic<bool> ready{false}; };
StorageJob* storageJob=nullptr;
bool storageRefreshPending = false, storageScanActive = false;
std::atomic<bool> storageBusQuiet{false};
uint32_t storageScanSince = 0, storageScanMs = 0, storageScanCount = 0;
sloth::MenuUi menuUi;
void handleMenuTouch(bool down, int x, int y, uint32_t now);
sloth::RadioExplorerUi radioUi;
bool radioStartPending = false, radioSuspended = false, lastGyroWanted = false;
uint32_t lastRadioUpdate = 0, lastGyroConfigure = 0, radioMotionSamples = 0;
void closeRadio();
void openRadio(sloth::RadioKind kind, uint32_t now);
void applyRadioEvent(sloth::RadioEvent event, uint32_t now);
void handleRadioTouch(bool down, int x, int y, uint32_t now);
void serviceRadio(uint32_t now);
sloth::PongGame pong;
sloth::TetrisGame tetris;
sloth::TetrisControls tetrisControls;
sloth::TetrisMoveButton tetrisMoveButton;
bool diagnosticTetrisHeld = false;
uint32_t diagnosticTetrisSince = 0;
uint32_t tetrisTouchPiece = 0;
bool tetrisTouchPlaying = false;
sloth::LeafSweepGame leafSweep;
sloth::ForestFidget forestFidget;
sloth::ForestFidgetControls forestControls;
bool forestFidgetActive = false;
sloth::TouchCommand touchCommand;
bool leafSweepActive = false, leafFinishedTouchReady = false;
sloth::GameRecords gameRecords;
sloth::GameOverlay gameOverlay;
sloth::GameAudio gameAudio;
bool tetrisActive = false, gameRecordsSavePending = false, gameRecordsSaveFailed = false;
bool gameAudioOwned = false, gameAudioResetPending = false;
uint32_t lastGameRecordsSave = 0, lastGameAudioAttempt = 0;
sloth::RemoteUi remoteUi;
sloth::WifiNetworksUi wifiNetworksUi;
bool browserPending=false, browserLoad=true, browserReclaim=false, wifiReturnBrowser=false;
bool browserNetworkPrepared=false;
browser_app::Destination browserDestination=browser_app::Destination::None;
char* browserResumeUrl=nullptr;
void openBrowser(uint32_t now, const char* url=nullptr, bool load=true);
void serviceBrowser(uint32_t now);
void handleBrowserTouch(bool down,int x,int y,uint32_t now);
bool wifiNetworksPending = false, wifiNetworksOwned = false, wifiReturnRemote = false;
void openWifiNetworks(bool fromRemote, uint32_t now);
void closeWifiNetworks();
void applyWifiNetworksEvent(sloth::WifiNetworksEvent event, uint32_t now);
void handleWifiNetworksTouch(bool down, int x, int y, uint32_t now);
void serviceWifiNetworks();
sloth::PetHumor humor;
sloth::PetSpeech speech;
sloth::PetReaction reaction;
bool jokeRequested = false, humorSavePending = false;
uint32_t lastHumorSaveAttempt = 0;
uint16_t pendingJokeId = sloth::PetHumor::kNoJoke;
uint32_t pendingSelectionMicros = 0;
bool pongActive = false, pongSpeedsSavePending = false, remoteRequested = false;
bool gameActive() { return pongActive || tetrisActive || leafSweepActive; }
sloth::GameKind activeGame() {
  return leafSweepActive ? sloth::GameKind::LeafSweep : tetrisActive ? sloth::GameKind::Tetris : sloth::GameKind::Pong;
}
bool gameFinished() {
  return pongActive ? pong.snapshot().phase == sloth::PongPhase::MatchOver :
      tetrisActive ? tetris.snapshot().phase == sloth::TetrisPhase::GameOver :
      leafSweepActive && leafSweep.snapshot().phase == sloth::LeafSweepPhase::GameOver;
}
void clearGameModes() { pongActive = tetrisActive = leafSweepActive = forestFidgetActive = false; }
void applyGameOverlayEvent(sloth::GameOverlayEvent event, uint32_t now);
void finishGame(uint32_t now);
void serviceGameAudio();
void handleLeafTouch(bool down, int x, int y, uint32_t now);
void handleTetrisTouch(bool down, int x, int y, uint32_t now);
void handleGameOverlayTouch(bool down, int x, int y, uint32_t now);
void handleForestTouch(bool down, int x, int y, uint32_t now);
enum class RemoteAttempt : uint8_t { None, Auto, WifiSaved, Usb = 4 };
RemoteAttempt remoteAttempt = RemoteAttempt::None, pendingRemoteAttempt = RemoteAttempt::None;
uint32_t remoteAttemptSince = 0, remoteListeningSince = 0;
bool remoteTriedUsb = false, remotePrompted = false;
bool settingsSavePending = false;
sloth::DisplayStream displayStream;
bool usbDisplayMode = false, displayEntryPending = false, binarySerialLocked = false;
bool displayHasPixels = false;
bool wifiDisplayMode = false, wifiProvisioned = false;
bool displayNetworkControl = false, displayControlFailed = false;
bool readingNetworkPacket = false;
bool displayAudioCreditPending = false;
uint8_t displayAudioVolume = 35;
sloth::VolumeOverlay volumeOverlay;
sloth::wifi_display::State lastWifiState = sloth::wifi_display::State::Off;
uint32_t displayRegions = 0;
// Cumulative session counters; host compares unsigned deltas across wraparound.
uint32_t displayFeedMicros = 0, displayPanelMicros = 0, displayAckMicros = 0;
uint32_t displayPerfSince = 0, displayPerfLast = 0, displayPerfRegions = 0;
// Front view: buttons above, speaker on right. Rotate top toward speaker.

void reportUsbDisplay();
void enterUsbDisplay();
void exitUsbDisplay(const char* reason);
void handleDisplayEvent(sloth::DisplayStreamEvent event);
void readSerialInput(uint8_t byte, uint32_t now);
void changeDisplayVolume(bool increase, uint32_t now);
void stopDisplayTransport();
void queueRemoteAttempt(RemoteAttempt attempt, uint32_t now);
void startRemoteAuto(uint32_t now);
void applyRemoteEvent(sloth::RemoteEvent event, uint32_t now);
uint16_t* pixels;
sloth::AppWorkspace appWorkspace;
unsigned selected = 0;
uint32_t lastFrame = 0, lastSave = 0;
uint32_t messageSince = 0, actionSince = 0;
const char* feedback = "Hello, little friend!";
int animation = 0;
bool dirty = false;
bool saveFailed = false;
sensors::Capabilities capabilities{};
sloth::ShakeGesture shake;
sloth::ShakeGesture shakeTest;
uint32_t testRateSince = 0, testRateSamples = 0;
float testSampleHz = 0;
sloth::TouchTap tap;
uint32_t lastPoll = 0, lastAcceleration = 0, maxSampleGap = 0;
uint32_t accelerationSamples = 0, lastDiagnostic = 0;
float accelerationX = 0, accelerationY = 0, accelerationZ = 0;
bool touchDown = false, diagnostics = false;
int lastTouchX = 0, lastTouchY = 0;
bool clockNeedsSync = true;
bool readingTime = false;
char timeCommand[11] = {};
unsigned timeLength = 0;
uint32_t timeCommandStarted = 0;
sloth::Hud hud;
sloth::ClockTime displayTime = sloth::formatLocalTime(0, 0);
pet_power::Status powerStatus;
uint32_t lastHudUtc = UINT32_MAX, lastPowerRead = 0;
sloth::DisplayIdle displayIdle;
sloth::DisplayLevel appliedDisplay = sloth::DisplayLevel::Bright;
uint32_t displayFrames = 0, lastPowerButtonPoll = 0, wakeGuardSince = 0;
bool powerButtonReady = false, wakeGuard = false, touchNeedsRelease = false;
void resetScreenInputs();
void drawScreen(uint32_t now, int action);
sloth::MotionView motionView(uint32_t now);

const char* displayName(sloth::DisplayLevel level) {
  switch (level) {
    case sloth::DisplayLevel::Bright: return "bright";
    case sloth::DisplayLevel::Dim: return "dim";
    case sloth::DisplayLevel::Off: return "off";
  }
  return "unknown";
}

void advancePet(uint32_t now) {
  const uint32_t elapsed = timeline.advance(now);
  if (elapsed) pet.advance(elapsed);
}

void updateClockDisplay() {
  const uint32_t utc = timeline.savedUtc();
  if (utc == lastHudUtc) return;
  lastHudUtc = utc;
  displayTime = sloth::formatLocalTime(utc, settings.timeZone);
  hud.timeText = displayTime.text;
  hud.zoneText = displayTime.zone;
}

void updateHud(uint32_t now, bool force = false) {
  updateClockDisplay();
  // Read only in the main loop, never inside the display's input callback.
  // A gauge is slow-changing; five-second polling leaves the motion bus free.
  if (!force && now - lastPowerRead < 5000) return;
  lastPowerRead = now;
  powerStatus = pet_power::read();
  hud.batteryPercent = powerStatus.available ? powerStatus.percent : -1;
  hud.batteryPresent = powerStatus.available && powerStatus.batteryPresent;
  hud.charging = powerStatus.available && powerStatus.charging;
  hud.externalPower = powerStatus.available && powerStatus.externalPower;
}

// Poll both active-low buttons; one action per physical press, no auto-repeat.
class Button {
 public:
  explicit Button(int pin) : pin_(pin) {}
  void begin() { raw_ = stable_ = digitalRead(pin_); changed_ = millis(); }
  bool pressed(uint32_t now) {
    const bool value = digitalRead(pin_);
    if (value != raw_) { raw_ = value; changed_ = now; }
    if (now - changed_ >= 35 && stable_ != raw_) {
      stable_ = raw_;
      return stable_ == LOW;
    }
    return false;
  }
  bool held() const { return raw_ == LOW || stable_ == LOW; }
  bool down() const { return stable_ == LOW; }
 private:
  int pin_;
  bool raw_ = HIGH, stable_ = HIGH;
  uint32_t changed_ = 0;
};
Button nextButton(board::kKeyPin), doButton(board::kBootPin);
bool displayKeyPending = false, displayBootPending = false;
bool displayBootTetris = false;
uint32_t displayBootAt = 0, displayBootPiece = 0;
void captureBootEdge(uint32_t now) {
  displayBootPending = true;
  displayBootAt = now;
  displayBootPiece = tetris.pieceGeneration();
  displayBootTetris = tetrisActive && !gameOverlay.isOpen() && !gameFinished();
}
void sampleDisplayButtons() {
  // Latch edges during full-frame DMA; defer actions until the panel is idle.
  const uint32_t now = millis();
  displayKeyPending |= nextButton.pressed(now);
  if (doButton.pressed(now)) captureBootEdge(now);
}

void printTcpState() {
  if(!browserNetworkPrepared)return;
  unsigned states[TIME_WAIT+1]{};
  LOCK_TCPIP_CORE();
  for(const tcp_pcb* pcb=tcp_active_pcbs;pcb;pcb=pcb->next)
    if(pcb->state<=TIME_WAIT)++states[pcb->state];
  for(const tcp_pcb* pcb=tcp_tw_pcbs;pcb;pcb=pcb->next)++states[TIME_WAIT];
  UNLOCK_TCPIP_CORE();
  Serial.printf("TCP established=%u fin_wait_1=%u fin_wait_2=%u close_wait=%u closing=%u last_ack=%u time_wait=%u\n",
      states[ESTABLISHED],states[FIN_WAIT_1],states[FIN_WAIT_2],states[CLOSE_WAIT],states[CLOSING],states[LAST_ACK],states[TIME_WAIT]);
}
void printState() {
  advancePet(millis());
  updateClockDisplay();
  const auto s = pet.snapshot();
  Serial.printf("STATE food=%u joy=%u energy=%u sleeping=%u selected=%u active=%lu heap=%lu\n",
      s.fullness, s.happiness, s.energy, s.sleeping, selected,
      static_cast<unsigned long>(s.active_seconds), static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.printf("INPUT touch=%u imu=%u accel=(%.2f,%.2f,%.2f)g samples=%lu max_gap=%lums\n",
      capabilities.touch, capabilities.acceleration, accelerationX, accelerationY,
      accelerationZ, static_cast<unsigned long>(accelerationSamples),
      static_cast<unsigned long>(maxSampleGap));
  Serial.printf("TIME accounted_utc=%lu food_fraction=%u joy_fraction=%u sync_needed=%u\n",
      static_cast<unsigned long>(timeline.savedUtc()), s.fullness_seconds,
      s.happiness_seconds, clockNeedsSync);
  Serial.printf("HUD time=%s zone=%s battery_available=%u battery_present=%u battery_percent=%d charging=%u usb=%u\n",
      hud.timeText, hud.zoneText, powerStatus.available, powerStatus.batteryPresent,
      powerStatus.percent, powerStatus.charging, powerStatus.externalPower);
  Serial.printf("SCREEN level=%s requested=%s idle_ms=%lu frames=%lu pwr_button=%u\n",
      displayName(appliedDisplay), displayName(displayIdle.level()),
      static_cast<unsigned long>(displayIdle.idleMs(millis())),
      static_cast<unsigned long>(displayFrames), powerButtonReady);
  Serial.printf("USB_DISPLAY mode=%u connected=%u regions=%lu binary=%u\n",
      usbDisplayMode, displayStream.connected(), static_cast<unsigned long>(displayRegions), binarySerialLocked);
  Serial.printf("VOLUME level=%u overlay=%u local=%u\n", displayAudioVolume,
      volumeOverlay.visible(millis()), usbDisplayMode && !displayHasPixels);
  Serial.printf("SETTINGS animal=%u scene=%u timezone=%u subtitle=%u name=\"%s\" sensitivity=%u menu=%u pending=%u rotation=%u\n",
      static_cast<unsigned>(settings.animal), static_cast<unsigned>(settings.scene),
      settings.timeZone, settings.showSubtitle, settings.name, settings.shakeSensitivity,
      settingsUi.isOpen(), settingsSavePending, settings.screenRotation);
  Serial.printf("APP menu=%u page=%u focus=%d remote=%u attempt=%u remote_page=%u pong=%u speeds=%u,%u settings_page=%u settings_focus=%d remote_focus=%d menu_scroll=%d\n",
      menuUi.isOpen(), static_cast<unsigned>(menuUi.page()), menuUi.focus(), remoteRequested,
      static_cast<unsigned>(remoteAttempt), static_cast<unsigned>(remoteUi.page()), pongActive,
      menuUi.speed(0), menuUi.speed(1), static_cast<unsigned>(settingsUi.page()), settingsUi.focus(), remoteUi.focus(), menuUi.scrollOffset());
  Serial.printf("APP_MEMORY owner=%u canvas=%u display_rx=%u wifi_queues=%u radio_ui=%u browser_arena=%u wifi_stack_free=%u wifi_heap_low=%u\n",
      static_cast<unsigned>(appWorkspace.owner()),static_cast<unsigned>(appWorkspace.bytes()),
      static_cast<unsigned>(displayStream.receiveBytes()),static_cast<unsigned>(sloth::wifi_display::retainedBytes()),static_cast<unsigned>(radioUi.retainedBytes()),
      static_cast<unsigned>(browser_memory_live()),static_cast<unsigned>(sloth::wifi_display::snapshot().stackFreeBytes),
      static_cast<unsigned>(sloth::wifi_display::snapshot().minimumFreeHeap));
  printTcpState();
  const auto browser=browser_app::snapshot();
  Serial.printf("BROWSER active=%u pending=%u reclaim=%u phase=%u page=%u focus=%d loading=%u ready=%u history=%u fetch=%u error=%u http=%u bytes=%lu engine_error=%u engine_peak=%lu engine_live=%lu scroll=%u height=%u stack_low=%lu largest=%lu text=%u lines=%u links=%u arena_live=%lu arena_peak=%lu paint_us=%lu paints=%lu images=%u css=%u cache_hits=%u assets_skipped=%u sd_cache=%u\n",
      browser.active,browserPending,browserReclaim,static_cast<unsigned>(browser.phase),browser.page,browser.focus,
      browser.loading,browser.hasPage,browser.historyCount,browser.fetchState,browser.fetchError,browser.httpStatus,
      static_cast<unsigned long>(browser.received),browser.engineError,
      static_cast<unsigned long>(browser.enginePeak),static_cast<unsigned long>(browser.engineLive),browser.scroll,browser.contentHeight,
      static_cast<unsigned long>(sloth::browser_fetch::stackLowWaterBytes()),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),browser.textMode,browser.lines,browser.links,
      static_cast<unsigned long>(browser_memory_live()),static_cast<unsigned long>(browser_memory_peak()),
      static_cast<unsigned long>(browser.readerPaintUs),static_cast<unsigned long>(browser.readerPaints),browser.images,browser.styles,browser.cacheHits,browser.skipped,browser.sdCache);
  const auto network = sloth::wifi_networks::snapshot();
  Serial.printf("WIFI_NETWORKS open=%u page=%u focus=%d pending=%u owned=%u state=%u count=%u saved=%u error=%u rssi=%d\n",
      wifiNetworksUi.isOpen(), static_cast<unsigned>(wifiNetworksUi.page()), wifiNetworksUi.focus(),
      wifiNetworksPending, wifiNetworksOwned, static_cast<unsigned>(network.state), network.count,
      network.hasSaved, static_cast<unsigned>(network.error), network.rssi);
  Serial.printf("RADIO open=%u kind=%u page=%u count=%u state=%u busy=%u error=%d gyro=%u motion=%lu rssi=%d heap=%lu min_heap=%lu\n",
      radioUi.isOpen(), static_cast<unsigned>(radioUi.snapshot().kind), static_cast<unsigned>(radioUi.page()),
      radioUi.snapshot().count, static_cast<unsigned>(radioUi.snapshot().state), radio_explorer::busy(),
      radioUi.snapshot().error, sensors::gyroscopeEnabled(), static_cast<unsigned long>(radioMotionSamples),
      radioUi.selected() ? radioUi.selected()->rssi : -127,
      static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()));
  if (pongActive) {
    const auto& game = pong.snapshot();
    Serial.printf("PONG phase=%u score=%u,%u paddle=%.1f,%.1f direction=%d,%d ball=%.1f,%.1f cheer=%u cheer_ms=%u\n",
        static_cast<unsigned>(game.phase), game.score[0], game.score[1],
        static_cast<double>(game.paddleY[0]), static_cast<double>(game.paddleY[1]),
        game.paddleDirection[0], game.paddleDirection[1], static_cast<double>(game.ballX), static_cast<double>(game.ballY),
        game.cheering, static_cast<unsigned>(game.cheerElapsedMs));
  }
  if (tetrisActive) {
    const auto& game = tetris.snapshot();
    unsigned filled = 0;
    for (const auto& row : game.board) for (uint8_t cell : row) filled += cell != 0;
    Serial.printf("TETRIS phase=%u score=%lu lines=%lu level=%u piece=%u rotation=%u x=%d y=%d ghost=%d grounded=%u next=%u generation=%lu cells=%u touch_control=%u\n",
        static_cast<unsigned>(game.phase), static_cast<unsigned long>(game.score),
        static_cast<unsigned long>(game.lines), static_cast<unsigned>(game.level),
        static_cast<unsigned>(game.piece), static_cast<unsigned>(game.rotation),
        static_cast<int>(game.x), static_cast<int>(game.y), static_cast<int>(game.ghostY), game.grounded,
        static_cast<unsigned>(game.next), static_cast<unsigned long>(tetris.pieceGeneration()), filled,
        static_cast<unsigned>(tetrisControls.pressed()));
  }
  if (leafSweepActive) {
    const auto& game = leafSweep.snapshot();
    Serial.printf("LEAF phase=%u score=%lu leaves=%lu energy=%u elapsed_ms=%lu multiplier=%u paw=%u x=%d y=%d\n",
        static_cast<unsigned>(game.phase), static_cast<unsigned long>(game.score),
        static_cast<unsigned long>(game.leafCount), game.energy, static_cast<unsigned long>(game.elapsedMs),
        game.multiplier, game.pawVisible, game.pawX, game.pawY);
    for (unsigned i = 0; i < sizeof(game.objects) / sizeof(game.objects[0]); ++i) {
      const auto& object = game.objects[i];
      if (object.active) Serial.printf("LEAF_OBJECT index=%u type=%u x=%d y=%d age_ms=%u\n",
          i, static_cast<unsigned>(object.type), object.x, object.y, object.ageMs);
    }
  }
  const auto& fidget = forestFidget.snapshot();
  Serial.printf("FIDGET active=%u toy=%u name=\"%s\" elapsed_ms=%lu touch=%u x=%d y=%d motion=%u tilt=%.2f,%.2f bodies=%u trails=%u\n",
      forestFidgetActive, static_cast<unsigned>(fidget.toy), sloth::forestToyName(fidget.toy),
      static_cast<unsigned long>(fidget.elapsedMs), fidget.touching, fidget.touchX, fidget.touchY,
      fidget.motionAvailable, fidget.tiltX, fidget.tiltY, fidget.bodyCount, fidget.trailCount);
  if (forestFidgetActive && fidget.bodyCount) {
    const auto& body = fidget.bodies[0];
    Serial.printf("FIDGET_BODY x=%.2f y=%.2f vx=%.2f vy=%.2f angle=%.2f value=%.2f\n",
        body.x, body.y, body.vx, body.vy, body.angle, body.value);
  }
  const auto audio = audio_output::stats();
  Serial.printf("GAMES tetris=%u leaf=%u overlay=%u focus=%d score_page=%u music=%u,%u,%u effects=%u,%u,%u scores=%u,%u,%u save_pending=%u save_failed=%u audio=%u queued=%lu underruns=%lu overflows=%lu write_errors=%lu rendered=%lu\n",
      tetrisActive, leafSweepActive, static_cast<unsigned>(gameOverlay.page()), gameOverlay.focus(), gameOverlay.scorePage(),
      menuUi.music(0), menuUi.music(1), menuUi.music(2), menuUi.effects(0), menuUi.effects(1), menuUi.effects(2),
      gameRecords.tables[0].count, gameRecords.tables[1].count, gameRecords.tables[2].count, gameRecordsSavePending, gameRecordsSaveFailed,
      gameAudioOwned && audio_output::active(), static_cast<unsigned long>(audio.queuedSamples),
      static_cast<unsigned long>(audio.underruns), static_cast<unsigned long>(audio.overflows),
      static_cast<unsigned long>(audio.writeErrors), static_cast<unsigned long>(audio.renderedSamples));
  if (gameOverlay.page() == sloth::GameOverlayPage::Name)
    Serial.printf("SCORE_NAME value=\"%s\" scroll=%d\n", gameOverlay.name(), gameOverlay.scrollOffset());
  Serial.printf("STORAGE busy=%u checks=%lu scan_ms=%lu flash_ok=%u flash=%llu firmware_ok=%u firmware=%llu firmware_capacity=%llu nvs_ok=%u nvs_partition=%llu nvs_used=%llu nvs_total=%llu card=%u card_capacity=%llu card_total=%llu card_used=%llu card_free=%llu space_known=%u space_cached=%u error=%ld\n",
      storageScanActive, static_cast<unsigned long>(storageScanCount), static_cast<unsigned long>(storageScanMs),
      storageView.flashValid, static_cast<unsigned long long>(storageView.flashBytes),
      storageView.firmwareValid, static_cast<unsigned long long>(storageView.firmwareBytes),
      static_cast<unsigned long long>(storageView.firmwareCapacityBytes), storageView.nvsValid,
      static_cast<unsigned long long>(storageView.nvsPartitionBytes),
      static_cast<unsigned long long>(storageView.nvsUsedEntries),
      static_cast<unsigned long long>(storageView.nvsTotalEntries), static_cast<unsigned>(storageView.cardState),
      static_cast<unsigned long long>(storageView.cardCapacityBytes),
      static_cast<unsigned long long>(storageView.cardTotalBytes), static_cast<unsigned long long>(storageView.cardUsedBytes),
      static_cast<unsigned long long>(storageView.cardFreeBytes), storageView.cardSpaceKnown, storageView.cardSpaceCached, static_cast<long>(storageView.cardError));
  Serial.printf("REACTION active=%d elapsed_ms=%lu count=%u\n", reaction.active(millis()),
      static_cast<unsigned long>(reaction.elapsed(millis())), static_cast<unsigned>(sloth::kPetReactionCount));
  Serial.printf("HUMOR bank=%u id=%u seen=%u cycle=%lu mood=%u visible=%u pending=%u\n",
      static_cast<unsigned>(sloth::jokeCount()), humor.currentId(), humor.seenCount(),
      static_cast<unsigned long>(humor.cycle()), static_cast<unsigned>(humor.currentMood()),
      speech.text(millis()) != nullptr, humorSavePending);
  if (settingsUi.isOpen()) {
    const auto& draft = settingsUi.draft();
    Serial.printf("MENU page=%u focus=%d animal=%u scene=%u timezone=%u subtitle=%u name=\"%s\" sensitivity=%u\n",
        static_cast<unsigned>(settingsUi.page()), settingsUi.focus(),
        static_cast<unsigned>(draft.animal), static_cast<unsigned>(draft.scene),
        draft.timeZone, draft.showSubtitle, draft.name, draft.shakeSensitivity);
    if (settingsUi.page() == sloth::SettingsPage::Motion) {
      const auto m = motionView(millis());
      Serial.printf("MOTION available=%u fresh=%u xyz=(%.3f,%.3f,%.3f)g total=%.3f motion=%.3f peak=%.3f threshold=%.2f tilt=(%.1f,%.1f) samples=%lu hz=%.1f triggers=%lu peaks=%u cooldown=%u detected=%u\n",
          m.available, m.ready, m.x, m.y, m.z, m.magnitude, m.motion, m.peak,
          m.threshold, m.tiltX, m.tiltY, static_cast<unsigned long>(m.samples),
          m.sampleHz, static_cast<unsigned long>(m.triggers), m.peaks, m.cooldown, m.triggered);
    }
  }
}

void loadSettings() {
  uint8_t record[sloth::kSettingsRecordSize];
  const size_t length = storage.getBytesLength("settings");
  if (length == sizeof(record) && storage.getBytes("settings", record, length) == length &&
      sloth::decodeSettings(record, length, settings)) {
    Serial.println("SETTINGS restore ok");
  } else {
    settings = sloth::Settings();
    Serial.println("SETTINGS defaults: sloth, Moss, Eastern, jungle");
  }
  shake.setSensitivity(settings.shakeSensitivity);
  const uint16_t speeds = storage.getUShort("pong-speeds", 0x0303);
  const uint8_t left = speeds & 255u, right = speeds >> 8;
  menuUi.setSpeeds(left >= 1 && left <= 5 ? left : 3, right >= 1 && right <= 5 ? right : 3);
  pong.setSpeeds(menuUi.speed(0), menuUi.speed(1));
  uint8_t games[sloth::kGameRecordSize];
  const size_t gameLength = storage.getBytesLength("games-v1");
  const bool restoredGames = (gameLength == sizeof(games) || gameLength == sloth::kLegacyGameRecordSize) &&
      storage.getBytes("games-v1", games, gameLength) == gameLength &&
      sloth::decodeGameRecords(games, gameLength, gameRecords);
  for (unsigned g = 0; g < sloth::kGameCount; ++g) menuUi.setAudio(g, gameRecords.music[g], gameRecords.effects[g]);
  Serial.printf("GAMES history=%s scores=%u,%u,%u\n", restoredGames ? "restored" : "new",
      gameRecords.tables[0].count, gameRecords.tables[1].count, gameRecords.tables[2].count);
  lastHudUtc = UINT32_MAX;
}

void loadHumor() {
  humor.begin(esp_random());
  reaction.begin(esp_random());
  uint8_t record[sloth::PetHumor::kRecordSize];
  const size_t size = storage.getBytesLength("humor-v1");
  const bool restored = size == sizeof(record) &&
      storage.getBytes("humor-v1", record, size) == size && humor.restore(record, size);
  Serial.printf("HUMOR history=%s bank=%u seen=%u\n", restored ? "restored" : "new",
      static_cast<unsigned>(sloth::jokeCount()), humor.seenCount());
}

bool petHomeVisible() {
  return !menuUi.isOpen() && !settingsUi.isOpen() && !remoteRequested && !radioUi.isOpen() && !wifiNetworksUi.isOpen() && !browserPending && !browser_app::active() &&
      !gameActive() && !forestFidgetActive && !gameOverlay.isOpen() && !displayEntryPending && !usbDisplayMode;
}

void clearSpeech() {
  speech.clear();
  reaction.clear();
  jokeRequested = false;
  pendingJokeId = sloth::PetHumor::kNoJoke;
}

bool dismissPetSpeech(uint32_t now) {
  if (!petHomeVisible() || appliedDisplay == sloth::DisplayLevel::Off ||
      displayIdle.level() == sloth::DisplayLevel::Off ||
      (!speech.text(now) && !jokeRequested && pendingJokeId == sloth::PetHumor::kNoJoke)) return false;
  // Consume this press so closing a saying cannot also feed, wake, or select.
  clearSpeech();
  displayIdle.visibleInteraction(now);
  resetScreenInputs();
  lastFrame = 0;
  return true;
}

void saveHumor() {
  uint8_t record[sloth::PetHumor::kRecordSize];
  lastHumorSaveAttempt = millis();
  humorSavePending = !humor.encode(record, sizeof(record)) ||
      storage.putBytes("humor-v1", record, sizeof(record)) != sizeof(record);
  Serial.printf("HUMOR save=%s seen=%u\n", humorSavePending ? "failed" : "ok", humor.seenCount());
}

// Touch polling also runs inside display DMA. Queue the request there; select
// and commit history in the main loop before putting a new joke on the screen.
void requestJoke(uint32_t now) {
  if (!petHomeVisible() || settingsSavePending || humorSavePending || appliedDisplay == sloth::DisplayLevel::Off ||
      displayIdle.level() == sloth::DisplayLevel::Off) return;
  jokeRequested = true;
  displayIdle.visibleInteraction(now);
  resetScreenInputs();
}

void servePendingJoke() {
  if (jokeRequested) {
    jokeRequested = false;
    if (petHomeVisible() && displayIdle.level() != sloth::DisplayLevel::Off) {
      advancePet(millis());
      const uint32_t selectionStarted = micros();
      const uint16_t id = humor.next(pet.snapshot());
      pendingSelectionMicros = micros() - selectionStarted;
      if (id != sloth::PetHumor::kNoJoke) {
        pendingJokeId = id;
        saveHumor();
      }
    }
  } else if (humorSavePending && millis() - lastHumorSaveAttempt >= 5000u) saveHumor();
  if (pendingJokeId != sloth::PetHumor::kNoJoke && !humorSavePending) {
    const uint16_t id = pendingJokeId;
    pendingJokeId = sloth::PetHumor::kNoJoke;
    if (!petHomeVisible() || displayIdle.level() == sloth::DisplayLevel::Off) return;
    const uint32_t spokenAt = millis();
    speech.show(sloth::jokeAt(id).text, spokenAt);
    reaction.start(spokenAt);
    animation = 0;
    lastFrame = 0;
    Serial.printf("JOKE id=%u seen=%u cycle=%lu mood=%u selection_us=%lu text=%s\n", id, humor.seenCount(),
        static_cast<unsigned long>(humor.cycle()), static_cast<unsigned>(humor.currentMood()),
        static_cast<unsigned long>(pendingSelectionMicros), sloth::jokeAt(id).text);
  }
}

void savePendingSettings() {
  if (!settingsSavePending) return;
  settingsSavePending = false;
  uint8_t record[sloth::kSettingsRecordSize];
  if (!sloth::encodeSettings(pendingSettings, record, sizeof(record)) ||
      storage.putBytes("settings", record, sizeof(record)) != sizeof(record)) {
    settingsUi.setNotice("Couldn't save. Try again.");
    Serial.println("SETTINGS save failed; draft retained");
    return;
  }
  settings = pendingSettings;
  board::setScreenRotation(settings.screenRotation);
  lastHudUtc = UINT32_MAX;
  updateClockDisplay();
  settingsUi.close();
  resetScreenInputs();
  feedback = "Looking good, little friend!";
  messageSince = millis();
  animation = 0;
  Serial.println("SETTINGS save ok");
  printState();
}

void savePet() {
  advancePet(millis());
  uint8_t record[sloth::kRecordSize];
  sloth::encodeRecord(pet.snapshot(), timeline.savedUtc(), record);
  if (storage.putBytes("state", record, sizeof(record)) == sizeof(record)) {
    dirty = false;
    saveFailed = false;
    lastSave = millis();
    Serial.println("SAVE ok");
  } else {
    Serial.println("SAVE failed");
    saveFailed = true;
    // Retry at the normal save interval; avoid a flash/log loop on failure.
    lastSave = millis();
  }
}

void loadPet() {
  esp_err_t result = nvs_flash_init_partition("pet_nvs");
  if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // Only the pet's dedicated partition, never the full chip or factory NVS.
    ESP_ERROR_CHECK(nvs_flash_erase_partition("pet_nvs"));
    result = nvs_flash_init_partition("pet_nvs");
  }
  ESP_ERROR_CHECK(result);
  ESP_ERROR_CHECK(storage.begin("moss-v1", false, "pet_nvs") ? ESP_OK : ESP_FAIL);
  uint8_t record[sloth::kRecordSize];
  sloth::Snapshot saved{};
  uint32_t savedUtc = 0;
  const size_t length = storage.getBytesLength("state");
  if ((length == sloth::kRecordSize || length == sloth::kLegacyRecordSize) &&
      storage.getBytes("state", record, length) == length &&
      sloth::decodeRecord(record, length, saved, savedUtc) && pet.restore(saved)) {
    Serial.println("RESTORE ok");
  } else {
    Serial.println("RESTORE new pet");
  }
  uint32_t rtcUtc = 0;
  const auto rtcStatus = pet_rtc::read(rtcUtc);
  // A factory calendar can look valid without being set to the actual time.
  // Only an existing timestamped save establishes clock authority on boot.
  const uint32_t trustedUtc = savedUtc && rtcStatus == pet_rtc::Status::Valid ? rtcUtc : 0;
  const uint32_t offline = timeline.begin(millis(), savedUtc, trustedUtc);
  pet.advance(offline);
  clockNeedsSync = timeline.savedUtc() == 0;
  Serial.printf("CLOCK rtc_status=%u rtc_utc=%lu saved_utc=%lu offline_seconds=%lu\n",
      static_cast<unsigned>(rtcStatus), static_cast<unsigned long>(rtcUtc),
      static_cast<unsigned long>(savedUtc), static_cast<unsigned long>(offline));
  if (offline >= 180) feedback = "Welcome back, friend!";
  savePet(); // Persist migration/catch-up once, with its paired timestamp.
}

void interact(uint32_t now) {
  displayIdle.visibleInteraction(now);
}

bool hardwareActivity(uint32_t now) {
  // A GPIO edge can finish debouncing after PWR has already turned the screen
  // off. Consume it before touching the display policy until all keys release.
  if (wakeGuard) return true;
  if (displayIdle.hardwarePress(now)) {
    wakeGuard = true;
    wakeGuardSince = now;
  }
  return wakeGuard;
}

const char* responseMessage(sloth::Response response) {
  if (response == sloth::Response::Fed) {
    if (settings.animal == sloth::Animal::Cat) return "Fish snacks! Yum.";
    if (settings.animal == sloth::Animal::Frog) return "Crunchy bug! Yum.";
  }
  return sloth::message(response);
}

void powerTap(uint32_t now) {
  clearSpeech();
  bool back = false;
  if (browserPending || browserReclaim || browser_app::active()) {
    if(browserPending || browserReclaim){
      browserPending=false;browserDestination=browser_app::Destination::None;
      free(browserResumeUrl);browserResumeUrl=nullptr;
    }
    else browser_app::back();
    back=true;
  } else if (wifiNetworksUi.isOpen()) {
    applyWifiNetworksEvent(wifiNetworksUi.back(), now);
    back = true;
  } else if (remoteRequested) {
    back = true;
    if (displayHasPixels) {
      // Return to connection options so network setup is reachable even when
      // automatic USB connected immediately. Another PWR returns to Main.
      pendingRemoteAttempt = RemoteAttempt::None;
      stopDisplayTransport();
      remoteUi.show(sloth::RemoteStatus::Ready, static_cast<bool>(Serial));
    } else if (remoteUi.page() != sloth::RemotePage::Status) {
      applyRemoteEvent(remoteUi.back(), now);
    } else {
      remoteRequested = false;
      pendingRemoteAttempt = RemoteAttempt::None;
      stopDisplayTransport();
      remoteUi.close();
    }
  } else if (radioUi.isOpen()) {
    applyRadioEvent(radioUi.back(), now);
    back = true;
  } else if (forestFidgetActive) {
    forestFidgetActive = false;
    forestFidget.clearTouch();
    back = true; // Open-ended toys have nothing to lose or confirm.
  } else if (gameOverlay.isOpen()) {
    applyGameOverlayEvent(gameOverlay.back(), now);
    back = true;
  } else if (gameActive()) {
    if (gameFinished()) finishGame(now);
    else gameOverlay.confirmExit(activeGame());
    back = true;
  } else if (settingsUi.isOpen()) {
    if (settingsUi.back() == sloth::SettingsEvent::Cancelled) settingsUi.close();
    back = true;
  } else if (menuUi.isOpen()) {
    if (menuUi.back() == sloth::MenuEvent::Closed) menuUi.close();
    back = true;
  }
  if (back) displayIdle.begin(now);
  else displayIdle.powerTap(now);
  resetScreenInputs();
  wakeGuard = true;
  wakeGuardSince = now;
  lastFrame = 0;
  if (back) Serial.println("PWR short tap: back");
  else Serial.printf("PWR short tap: screen %s\n", displayIdle.level() == sloth::DisplayLevel::Off ? "off" : "on");
}

void perform(sloth::Action action, uint32_t now) {
  if (menuUi.isOpen() || settingsUi.isOpen() || remoteRequested || wifiNetworksUi.isOpen() || browserPending || browser_app::active() || gameActive() || forestFidgetActive || gameOverlay.isOpen() || displayEntryPending) return;
  if (displayIdle.level() == sloth::DisplayLevel::Off) {
    Serial.println("ACTION ignored: screen off; press a hardware button to wake");
    return;
  }
  if (settingsUi.isOpen()) {
    Serial.println("ACTION ignored: settings open");
    return;
  }
  advancePet(now); // Time before a Nap press still belongs to the prior state.
  const auto response = pet.act(action);
  if (response == sloth::Response::Sleeping) {
    // A sleeping shake also leaves brightness, feedback, and the save queue
    // alone. Only explicit care buttons/taps can interrupt Moss's nap.
    Serial.printf("ACTION Shake: %s\n", sloth::message(response));
    printState();
    return;
  }
  clearSpeech();
  interact(now);
  feedback = responseMessage(response);
  messageSince = actionSince = now;
  dirty = true;
  animation = response == sloth::Response::Fed ? 1 :
              response == sloth::Response::Played ? 2 :
              response == sloth::Response::Danced ? 3 : 0;
  Serial.printf("ACTION %s: %s\n", sloth::actionName(action), feedback);
  printState();
}

void openMenu(uint32_t now) {
  if (displayIdle.level() == sloth::DisplayLevel::Off || settingsSavePending) return;
  clearSpeech();
  closeRadio();
  closeWifiNetworks();
  if (!menuUi.isOpen()) menuUi.open();
  selected = 3;
  animation = 0;
  interact(now);
  resetScreenInputs();
  lastFrame = 0;
  Serial.println("MENU opened");
}

void openSettings(uint32_t now) {
  if (displayIdle.level() == sloth::DisplayLevel::Off || settingsSavePending) return;
  if (!settingsUi.isOpen()) settingsUi.open(settings);
  interact(now);
  resetScreenInputs();
  lastFrame = 0;
}

void applySettingsEvent(sloth::SettingsEvent result, uint32_t now) {
  if (!settingsUi.isOpen() || settingsSavePending || displayEntryPending || usbDisplayMode ||
      displayIdle.level() == sloth::DisplayLevel::Off) return;
  interact(now);
  if (result == sloth::SettingsEvent::SaveRequested) {
    pendingSettings = settingsUi.draft();
    settingsSavePending = true;
  } else if (result == sloth::SettingsEvent::StorageRefresh) {
    if (!storageScanActive) storageRefreshPending = true;
  } else if (result == sloth::SettingsEvent::Cancelled) settingsUi.close();
  resetScreenInputs();
  lastFrame = 0;
}

// Card initialization and FAT accounting can take time. This worker never owns
// UI state, preferences or panel buffers; its single result is copied on the
// main loop. The driver shares SPI through IDF transactions, not Arduino SPI.
void storageWorker(void* argument) {
  auto* job=static_cast<StorageJob*>(argument);
  job->result=storage_status::read([](){storageBusQuiet.store(false);});
  job->ready.store(true); // No job access after publishing; main can delete it.
  vTaskDelete(nullptr);
}

void serviceStorage() {
  if (storageJob && storageJob->ready.load()) {
    storageView = storageJob->result;
    delete storageJob;storageJob=nullptr;
    storageScanActive = false;
    storageScanMs = millis() - storageScanSince;
    ++storageScanCount;
    lastFrame = 0;
  }
  if (!storageRefreshPending || storageScanActive) return;
  storageRefreshPending = false;
  if (!settingsUi.isOpen() || settingsUi.page() != sloth::SettingsPage::Storage ||
      appliedDisplay == sloth::DisplayLevel::Off) return;
  storageJob=new(std::nothrow)StorageJob;
  // Show onboard results immediately; the SD worker cannot hide them.
  storageView = storage_status::onboard();
  storageView.cardState = storage_status::CardState::Checking;
  // Finish a Checking frame before handing the shared SPI bus to storage.
  // SD polling must not overlap LCD DMA, including throughout a FAT recount.
  if (pixels) { drawScreen(millis(), 0); board::present(pixels); ++displayFrames; }
  storageScanSince = millis();
  storageScanActive = true;
  storageBusQuiet.store(true);
  if (!storageJob || xTaskCreate(storageWorker, "moss-storage", 6144, storageJob, 1, nullptr) != pdPASS) {
    delete storageJob;storageJob=nullptr;
    storageScanActive = false;
    storageBusQuiet.store(false);
    storageView.cardState = storage_status::CardState::Unavailable;
    storageView.cardError = ESP_ERR_NO_MEM;
  }
  lastFrame = 0;
}

void finishGame(uint32_t now) {
  if (!gameActive()) return;
  const auto game = activeGame();
  uint32_t score;
  uint16_t detail;
  const char* name = "MOSS";
  if (pongActive) {
    const auto& result = pong.snapshot();
    if (result.phase != sloth::PongPhase::MatchOver || result.winner < 0) return;
    score = result.score[result.winner]; detail = result.score[1 - result.winner];
    name = result.winner == 0 ? "PLAYER 1" : "PLAYER 2";
  } else if (tetrisActive) {
    const auto& result = tetris.snapshot();
    if (result.phase != sloth::TetrisPhase::GameOver) return;
    score = result.score;
    detail = static_cast<uint16_t>(result.lines > UINT16_MAX ? UINT16_MAX : result.lines);
  } else {
    const auto& result = leafSweep.snapshot();
    if (result.phase != sloth::LeafSweepPhase::GameOver) return;
    score = result.score;
    detail = static_cast<uint16_t>(result.leafCount > UINT16_MAX ? UINT16_MAX : result.leafCount);
  }
  if (sloth::qualifies(gameRecords, game, score, detail)) gameOverlay.enterName(game, score, detail, name);
  else {
    clearGameModes();
    gameOverlay.showScores(game);
  }
  interact(now); resetScreenInputs(); lastFrame = 0;
}

void applyGameOverlayEvent(sloth::GameOverlayEvent event, uint32_t now) {
  if (event == sloth::GameOverlayEvent::None) {
    interact(now); lastFrame = 0;
    return; // Editing a name or paging scores keeps the same input context.
  }
  if (event == sloth::GameOverlayEvent::Resume || event == sloth::GameOverlayEvent::Closed) {
    // Both clocks discard time spent in the dialog, including display sleep.
    if (pongActive) pong.resume(now);
    if (tetrisActive) tetris.resume(now);
    if (leafSweepActive) leafSweep.resume(now);
  } else if (event == sloth::GameOverlayEvent::Leave) {
    clearGameModes();
    gameOverlay.close();
  } else if (event == sloth::GameOverlayEvent::Save || event == sloth::GameOverlayEvent::Skip) {
    const auto game = gameOverlay.game();
    if (event == sloth::GameOverlayEvent::Save) {
      const auto& result = gameOverlay.result();
      if (!sloth::addGameScore(gameRecords, game, result.score, result.detail, gameOverlay.submittedName())) return;
      gameRecordsSavePending = true;
    }
    clearGameModes();
    gameOverlay.showScores(game);
  }
  interact(now); resetScreenInputs(); lastFrame = 0;
}

// The shared speaker has one owner. Game audio is synthesized and queued only
// here in the main loop; touch/DMA callbacks never touch codec, I2S or NVS.
void serviceGameAudio() {
  if(!gameActive()&&!gameAudioOwned)return;
  const unsigned game = static_cast<unsigned>(activeGame());
  const bool audible = gameActive() && !gameOverlay.isOpen() && !remoteRequested &&
      appliedDisplay != sloth::DisplayLevel::Off && displayIdle.level() != sloth::DisplayLevel::Off;
  gameAudio.configure(game, audible && menuUi.music(game), audible && menuUi.effects(game));
  if (gameAudioResetPending) { gameAudioResetPending = false; gameAudio.reset(); }
  const uint16_t pongEvents = pong.takeEvents();
  const uint8_t tetrisEvents = tetris.takeEvents();
  const uint8_t leafEvents = leafSweep.takeEvents();
  if (audible && pongActive) {
    if (pongEvents & sloth::PongServe) gameAudio.trigger(sloth::GameSound::Serve);
    if (pongEvents & sloth::PongWall) gameAudio.trigger(sloth::GameSound::Wall);
    if (pongEvents & sloth::PongPaddle) gameAudio.trigger(sloth::GameSound::Paddle);
    if (pongEvents & sloth::PongPoint) gameAudio.trigger(sloth::GameSound::Point);
    if (pongEvents & sloth::PongGameOver) gameAudio.trigger(sloth::GameSound::GameOver);
  } else if (audible && tetrisActive) {
    if (tetrisEvents & sloth::TetrisMove) gameAudio.trigger(sloth::GameSound::Move);
    if (tetrisEvents & sloth::TetrisRotate) gameAudio.trigger(sloth::GameSound::Rotate);
    if (tetrisEvents & sloth::TetrisLock) gameAudio.trigger(sloth::GameSound::Lock);
    if (tetrisEvents & sloth::TetrisLineClear) gameAudio.trigger(sloth::GameSound::LineClear);
    if (tetrisEvents & sloth::TetrisGameOver) gameAudio.trigger(sloth::GameSound::GameOver);
  } else if (audible && leafSweepActive) {
    if (leafEvents & sloth::LeafSweepCollect) gameAudio.trigger(sloth::GameSound::LeafCollect);
    if (leafEvents & sloth::LeafSweepRound) gameAudio.trigger(sloth::GameSound::LineClear);
    if (leafEvents & sloth::LeafSweepCanHit) gameAudio.trigger(sloth::GameSound::CanHit);
    if (leafEvents & sloth::LeafSweepGameOver) gameAudio.trigger(sloth::GameSound::GameOver);
  }
  const bool wanted = audible && (menuUi.music(game) || menuUi.effects(game));
  if (!wanted) {
    if (gameAudioOwned) { audio_output::stop(); gameAudioOwned = false; }
    return;
  }
  if (!gameAudioOwned) {
    if (lastGameAudioAttempt && millis() - lastGameAudioAttempt < 5000) return;
    lastGameAudioAttempt = millis();
    gameAudioOwned = audio_output::start(30);
    if (!gameAudioOwned) { Serial.println("GAMES speaker unavailable; retrying"); return; }
    lastGameAudioAttempt = 0;
  }
  // 144 ms queued PCM covers a full panel transfer plus control/storage work.
  // SFX-only sessions queue silence between cues so short effects never wait
  // for the remote-audio driver's 120 ms initial prefill threshold.
  size_t queued = audio_output::stats().queuedSamples;
  int16_t samples[256];
  while (queued < 2304) {
    const size_t count = min(static_cast<size_t>(256), 2304 - queued);
    gameAudio.render(samples, count);
    if (!audio_output::pushPcm(reinterpret_cast<const uint8_t*>(samples), count * sizeof(int16_t))) break;
    queued += count;
  }
}

void savePendingGameRecords() {
  if (!gameRecordsSavePending || (gameRecordsSaveFailed && millis() - lastGameRecordsSave < 5000)) return;
  uint8_t record[sloth::kGameRecordSize];
  lastGameRecordsSave = millis();
  const bool saved = sloth::encodeGameRecords(gameRecords, record, sizeof(record)) &&
      storage.putBytes("games-v1", record, sizeof(record)) == sizeof(record);
  gameRecordsSaveFailed = !saved;
  gameRecordsSavePending = !saved;
  lastFrame = 0;
  Serial.printf("GAMES save=%s scores=%u,%u,%u\n", saved ? "ok" : "failed",
      gameRecords.tables[0].count, gameRecords.tables[1].count, gameRecords.tables[2].count);
}

void applyMenuEvent(sloth::MenuEvent event, uint32_t now) {
  interact(now);
  if (event == sloth::MenuEvent::Browser) openBrowser(now);
  else if (event == sloth::MenuEvent::WifiNetworks) openWifiNetworks(false, now);
  else if (event == sloth::MenuEvent::OpenSettings) openSettings(now);
  else if (event == sloth::MenuEvent::RemoteDisplay) startRemoteAuto(now);
  else if (event == sloth::MenuEvent::WifiExplorer) openRadio(sloth::RadioKind::Wifi, now);
  else if (event == sloth::MenuEvent::BluetoothExplorer) openRadio(sloth::RadioKind::Bluetooth, now);
  else if (event == sloth::MenuEvent::PlayPong) {
    clearGameModes();
    pong.setSpeeds(menuUi.speed(0), menuUi.speed(1));
    pong.reset(now);
    pongActive = true;
    gameAudioResetPending = true;
  } else if (event == sloth::MenuEvent::PlayTetris) {
    clearGameModes();
    tetris.reset(now, esp_random());
    tetrisActive = true;
    gameAudioResetPending = true;
  } else if (event == sloth::MenuEvent::PlayLeafSweep) {
    clearGameModes();
    leafSweep.reset(now, esp_random());
    leafSweepActive = true;
    leafFinishedTouchReady = false;
    gameAudioResetPending = true;
  } else if (event == sloth::MenuEvent::PlayForestFidget) {
    clearGameModes();
    forestFidget.reset(now, esp_random());
    forestFidgetActive = true;
  } else if (event == sloth::MenuEvent::ShowPongScores || event == sloth::MenuEvent::ShowTetrisScores ||
             event == sloth::MenuEvent::ShowLeafSweepScores) {
    gameOverlay.showScores(event == sloth::MenuEvent::ShowPongScores ? sloth::GameKind::Pong :
        event == sloth::MenuEvent::ShowTetrisScores ? sloth::GameKind::Tetris : sloth::GameKind::LeafSweep);
  } else if (event == sloth::MenuEvent::GamePreferencesChanged) {
    for (unsigned g = 0; g < sloth::kGameCount; ++g) {
      gameRecords.music[g] = menuUi.music(g); gameRecords.effects[g] = menuUi.effects(g);
    }
    gameRecordsSavePending = true;
  } else if (event == sloth::MenuEvent::SpeedsChanged) {
    pongSpeedsSavePending = true;
  } else if (event == sloth::MenuEvent::Closed) menuUi.close();
  resetScreenInputs();
  lastFrame = 0;
}

void handleMenuTouch(bool down, int x, int y, uint32_t now) {
  if (!menuUi.isOpen() || settingsUi.isOpen() || remoteRequested || gameActive() ||
      forestFidgetActive || gameOverlay.isOpen() || radioUi.isOpen() || wifiNetworksUi.isOpen() ||
      browserPending || browserReclaim || browser_app::active() || displayEntryPending ||
      wakeGuard || appliedDisplay == sloth::DisplayLevel::Off) return;
  if (touchNeedsRelease) {
    if (!down) {
      touchNeedsRelease = false;
      touchDown = false;
      menuUi.cancelTouch();
    }
    return;
  }
  const bool wasDown = touchDown;
  touchDown = down;
  // Sensor release packets need not retain the final contact coordinates.
  if (down) { lastTouchX = x; lastTouchY = y; }
  const auto previousPage = menuUi.page();
  const int previousFocus = menuUi.focus();
  const auto event = menuUi.touch(down, lastTouchX, lastTouchY);
  if (down || wasDown) { interact(now); lastFrame = 0; }
  // A drag keeps its recognizer alive. Internal page changes and rail Next use
  // None, so a completed tap is also recognized by its page/focus change.
  if (!down && (event != sloth::MenuEvent::None || menuUi.page() != previousPage ||
                menuUi.focus() != previousFocus)) applyMenuEvent(event, now);
}

void activateHome(unsigned target, uint32_t now) {
  if (target == sloth::kPetTalkTarget) { requestJoke(now); return; }
  if (target > 3) return;
  selected = target;
  if (target == 3) openMenu(now);
  else perform(static_cast<sloth::Action>(target), now);
}

void nextControl(uint32_t now) {
  if(browserPending || browserReclaim)return;
  if (displayIdle.level() == sloth::DisplayLevel::Off || settingsSavePending) return;
  if (dismissPetSpeech(now)) return;
  if (usbDisplayMode && displayHasPixels) { changeDisplayVolume(true, now); return; }
  interact(now);
  if (gameOverlay.isOpen()) {
    gameOverlay.next(); lastFrame = 0;
    return; // The overlay cancels held touches itself; idle fingers need no extra release.
  }
  else if (forestFidgetActive) {
    forestFidget.cycle(1, now);
    resetScreenInputs();
  } else if (pongActive) {
    if (pong.snapshot().phase == sloth::PongPhase::MatchOver) finishGame(now);
    else pong.press(1, now);
  } else if (tetrisActive) {
    tetrisControls.reset(); tetrisTouchPiece = 0; touchDown = false;
    if (tetris.snapshot().phase == sloth::TetrisPhase::GameOver) finishGame(now);
    else tetris.rotate(now);
  } else if (leafSweepActive) {
    if (gameFinished()) finishGame(now);
  }
  else if (radioUi.isOpen()) radioUi.next();
  else if (browser_app::active()) browser_app::next();
  else if (wifiNetworksUi.isOpen()) wifiNetworksUi.next();
  else if (remoteRequested) remoteUi.next();
  else if (settingsUi.isOpen()) settingsUi.next();
  else if (menuUi.isOpen()) menuUi.next();
  else selected = (selected + 1) % 4;
  tap = sloth::TouchTap();
  menuUi.cancelTouch(); settingsUi.cancelTouch(); remoteUi.cancelTouch(); radioUi.cancelTouch(); wifiNetworksUi.cancelTouch();
  touchNeedsRelease = true;
  lastFrame = 0;
}

void activateControl(uint32_t now) {
  if(browserPending || browserReclaim)return;
  if (displayIdle.level() == sloth::DisplayLevel::Off || settingsSavePending) return;
  if (dismissPetSpeech(now)) return;
  if (usbDisplayMode && displayHasPixels) { changeDisplayVolume(false, now); return; }
  interact(now);
  if (gameOverlay.isOpen()) applyGameOverlayEvent(gameOverlay.activate(), now);
  else if (forestFidgetActive) {
    forestFidget.cycle(-1, now);
    resetScreenInputs();
    lastFrame = 0;
  }
  else if (pongActive) {
    if (pong.snapshot().phase == sloth::PongPhase::MatchOver) finishGame(now);
    else pong.press(0, now);
  } else if (tetrisActive) {
    tetrisControls.reset(); tetrisTouchPiece = 0; touchDown = false; touchNeedsRelease = true;
    if (tetris.snapshot().phase == sloth::TetrisPhase::GameOver) finishGame(now);
    else tetris.moveRight(now);
  } else if (leafSweepActive) {
    if (gameFinished()) finishGame(now);
  }
  else if (radioUi.isOpen()) applyRadioEvent(radioUi.activate(), now);
  else if (browser_app::active()) browser_app::activate();
  else if (wifiNetworksUi.isOpen()) applyWifiNetworksEvent(wifiNetworksUi.activate(), now);
  else if (remoteRequested) applyRemoteEvent(remoteUi.activate(), now);
  else if (settingsUi.isOpen()) applySettingsEvent(settingsUi.activate(), now);
  else if (menuUi.isOpen()) applyMenuEvent(menuUi.activate(), now);
  else activateHome(selected, now);
}

void routeSideButtons(bool key, bool boot, uint32_t now) {
  if ((key || boot) && dismissPetSpeech(now)) return;
  const bool liveDisplay = usbDisplayMode && displayHasPixels;
  // Pet care uses the same left-to-advance, right-to-select order as menus.
  const bool menuNavigation = !liveDisplay && (gameOverlay.isOpen() ||
      (!gameActive() && !forestFidgetActive));
  const bool twoPlayers = pongActive && !gameOverlay.isOpen() && pong.snapshot().phase != sloth::PongPhase::MatchOver;
  const auto action = sloth::sideButtonAction(key, boot, menuNavigation, twoPlayers);
  if (action == sloth::SideButtonAction::Next || action == sloth::SideButtonAction::Both) nextControl(now);
  if (action == sloth::SideButtonAction::Select || action == sloth::SideButtonAction::Both) activateControl(now);
}

const char* status(uint32_t now) {
  if (now - messageSince < 6000) return feedback;
  if (clockNeedsSync) return "USB clock sync needed";
  const auto s = pet.snapshot();
  if (s.sleeping) return s.energy == 100 ? "Fully rested. Sweet dreams!"
                                       : "Rest +1 every 3 seconds";
  if (s.fullness < 25) return "A little snack, please?";
  if (s.energy < 25) return "Getting sleepy...";
  if (s.happiness < 30) return "Let's play together!";
  return "Tap me for a thought.";
}

void printClock() {
  uint32_t utc = 0;
  const auto result = pet_rtc::read(utc);
  Serial.printf("CLOCK status=%u utc=%lu accounted_utc=%lu sync_needed=%u\n",
      static_cast<unsigned>(result), static_cast<unsigned long>(utc),
      static_cast<unsigned long>(timeline.savedUtc()), clockNeedsSync);
}

void finishTimeCommand() {
  readingTime = false;
  if (timeLength < 9 || timeLength > 10) { Serial.println("CLOCK error: expected UTC seconds"); return; }
  uint64_t parsed = 0;
  for (unsigned i = 0; i < timeLength; ++i) parsed = parsed * 10 + (timeCommand[i] - '0');
  if (parsed > UINT32_MAX || !pet_rtc::set(static_cast<uint32_t>(parsed))) {
    Serial.println("CLOCK error: setting RTC failed");
    return;
  }
  advancePet(millis());
  // A deliberate clock correction labels the current snapshot; it does not
  // create gameplay time or retroactively undo care already given this session.
  timeline.anchorUtc(static_cast<uint32_t>(parsed));
  clockNeedsSync = false;
  savePet();
  if (saveFailed) { Serial.println("CLOCK error: saving clock anchor failed"); return; }
  Serial.printf("CLOCK set ok utc=%lu\n", static_cast<unsigned long>(parsed));
}

void handleSerial(char command, uint32_t now) {
  if (touchCommand.active()) {
    touchCommand.expire(now);
    const auto result = touchCommand.feed(command);
    if (result == sloth::TouchCommand::Result::Complete) {
      if(browserPending || browserReclaim)return;
      if (browser_app::active()) handleBrowserTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
      else if (radioUi.isOpen()) handleRadioTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
      else if (wifiNetworksUi.isOpen()) handleWifiNetworksTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
      else if (forestFidgetActive) handleForestTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
      else if (gameOverlay.isOpen()) handleGameOverlayTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
      else if (tetrisActive) handleTetrisTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
      else if (leafSweepActive) handleLeafTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
      else if (!remoteRequested && !settingsUi.isOpen() && menuUi.isOpen())
        handleMenuTouch(touchCommand.down(), touchCommand.x(), touchCommand.y(), now);
    }
    return;
  }
  if (remoteRequested && command == 'x') {
    powerTap(now);
    return;
  }
  if ((browserPending || browser_app::active() || browserReclaim) && command == 'u') {
    browserPending=false;browserDestination=browser_app::Destination::None;
    free(browserResumeUrl);browserResumeUrl=nullptr;browser_app::requestClose();return;
  }
  if (usbDisplayMode && command == 'u') return;
  if (readingTime) {
    if (command == '\r') return;
    if (command == '\n') { finishTimeCommand(); return; }
    if (command < '0' || command > '9' || timeLength >= 10) {
      readingTime = false;
      Serial.println("CLOCK error: invalid time command");
      return;
    }
    timeCommand[timeLength++] = command;
    return;
  }
  switch (command) {
    case '?': reportUsbDisplay(); break;
    case 's': advancePet(now); printState(); break;
    case 'I': Serial.printf("HEAP integrity=%u free=%lu minimum=%lu largest=%lu\n",
        heap_caps_check_integrity_all(false), static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(ESP.getMinFreeHeap()),
        static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))); break;
    case 'w': savePet(); printState(); break;
    case 't': printClock(); break;
    case 'T': readingTime = true; timeLength = 0; timeCommandStarted = now; break;
    case 'L': touchCommand.begin(now); break;
    case 'f': perform(sloth::Action::Feed, now); break;
    case 'p': perform(sloth::Action::Play, now); break;
    case 'n': perform(sloth::Action::Nap, now); break;
    case 'h': perform(sloth::Action::Shake, now); break;
    case 'q': requestJoke(now); break;
    case 'u': openMenu(now); break;
    case 'j': nextControl(now); break;
    case 'k': activateControl(now); break;
    case '[': routeSideButtons(false, true, now); break;
    case ']': routeSideButtons(true, false, now); break;
    case '{': if (tetrisActive && !gameOverlay.isOpen() && !gameFinished()) { diagnosticTetrisHeld = true; diagnosticTetrisSince = now; } break;
    case '}': diagnosticTetrisHeld = false; break;
    case 'x': if (menuUi.isOpen() || settingsUi.isOpen() || remoteRequested || wifiNetworksUi.isOpen() || browserPending || browser_app::active() || gameActive() || forestFidgetActive || gameOverlay.isOpen()) powerTap(now); break;
    case 'o': displayIdle.screenOffNow(); break;
    case 'v': hardwareActivity(now); break;
    case 'b': powerTap(now); break;
    case 'd': diagnostics = !diagnostics; break;
  }
}

// Control replies use the authenticated network once paired; USB is only needed
// for first setup. A failed bounded queue write ends the session in the loop.
void displayLine(const char* format, ...) {
  char line[512];
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(line)) {
    displayControlFailed = true;
    return;
  }
  if (displayNetworkControl) {
    if (!sloth::wifi_display::write(reinterpret_cast<const uint8_t*>(line), length))
      displayControlFailed = true;
  } else if (Serial) Serial.write(reinterpret_cast<const uint8_t*>(line), length);
}
void displayReply(const char* word) {
  const uint32_t started = micros();
  displayLine("DISPLAY %s %016llx %lu\n", word,
      static_cast<unsigned long long>(displayStream.nonce()),
      static_cast<unsigned long>(displayStream.sequence()));
  displayAckMicros += micros() - started;
}
void reportUsbDisplay() {
  if (!usbDisplayMode) { displayReply("IDLE"); return; }
  displayLine("DISPLAY %s %016llx 480 480 15360\n",
      wifiDisplayMode ? "WIFI_REQUEST" : "REQUEST",
      static_cast<unsigned long long>(displayStream.nonce()));
  displayLine("DISPLAY CAPS %016llx %u\n",
      static_cast<unsigned long long>(displayStream.nonce()), wifiDisplayMode ? 127u : 31u);
  displayLine("DISPLAY ROTATION %016llx %u\n",
      static_cast<unsigned long long>(displayStream.nonce()), settings.screenRotation);
  if (wifiDisplayMode && wifiProvisioned && !displayNetworkControl) {
    displayLine("DISPLAY WIFI_SAVED %016llx 1\n", static_cast<unsigned long long>(displayStream.nonce()));
    const auto wifi = sloth::wifi_display::snapshot();
    if (wifi.state == sloth::wifi_display::State::Listening && Serial) {
      // Repeat only over physical USB so a companion launched later can pair.
      Serial.printf("DISPLAY PAIRING %016llx %s %s %s\n",
          static_cast<unsigned long long>(wifi.nonce), wifi.deviceId, wifi.fingerprint, wifi.token);
      Serial.printf("DISPLAY WIFI_READY %016llx %s %u %s %s\n",
          static_cast<unsigned long long>(wifi.nonce), wifi.address, wifi.port, wifi.fingerprint, wifi.token);
    }
  }
}

void changeDisplayVolume(bool increase, uint32_t now) {
  const uint8_t volume = sloth::steppedDisplayVolume(displayAudioVolume, increase);
  displayAudioVolume = volume;
  volumeOverlay.show(volume, now);
  if (!displayHasPixels) lastFrame = now - 125u;
  // Adjusting a button never enables optional audio. If the bus fails, mute
  // safely and let the companion disable audio while retaining the desktop.
  if (audio_output::active() && !audio_output::setVolume(volume)) {
    audio_output::stop();
    displayLine("DISPLAY AUDIO %016llx 0 %u 1\n",
        static_cast<unsigned long long>(displayStream.nonce()), volume);
  }
  displayLine("DISPLAY VOLUME %016llx %u\n",
      static_cast<unsigned long long>(displayStream.nonce()), volume);
  displayIdle.visibleInteraction(now);
}

void openBrowser(uint32_t now,const char* url,bool load) {
  if(browser_app::active() || browserReclaim)return;
  // Copy before freeing: returning from Wi-Fi can pass the retained URL itself.
  char* next=url && *url?strdup(url):nullptr;
  free(browserResumeUrl);browserResumeUrl=next;
  closeRadio();closeWifiNetworks();stopDisplayTransport();
  remoteRequested=false;pendingRemoteAttempt=RemoteAttempt::None;remoteUi.close();
  clearGameModes();gameOverlay.close();settingsUi.close();
  wifiReturnBrowser=false;browserLoad=load;browserPending=true;
  savePet();interact(now);resetScreenInputs();lastFrame=0;
}
void handleBrowserTouch(bool down,int x,int y,uint32_t now) {
  if(wakeGuard || appliedDisplay==sloth::DisplayLevel::Off)return;
  if(touchNeedsRelease){if(!down)touchNeedsRelease=false;return;}
  if(down)interact(now);
  browser_app::touch(down,x,y); // Browser URL/key positions are never logged.
}
void serviceBrowser(uint32_t now) {
  if(storageScanActive || storageBusQuiet.load())return;
  if(browserPending) {
    if(appliedDisplay==sloth::DisplayLevel::Off){browserPending=false;free(browserResumeUrl);browserResumeUrl=nullptr;return;}
    if(sloth::wifi_display::busy() || radio_explorer::busy() || sloth::wifi_networks::busy())return;
    sloth::wifi_display::reap();
    // lwIP and Arduino retain their first-use tasks/queues after Wi-Fi stops.
    // Allocate those while the canvas is protected, not inside its future hole.
    if(!sloth::wifi_networks::prepare()){
      browserPending=false;free(browserResumeUrl);browserResumeUrl=nullptr;
      menuUi.close();feedback="Wi-Fi setup failed";
      messageSince=now;lastFrame=0;return;
    }
    browserNetworkPrepared=true;
    sloth::browser_fetch::prepare();
    displayStream.cancel();
    if(!displayStream.setDecodeWorkspace(nullptr,0))return;
    browserPending=false;
    const bool claimed=appWorkspace.claim(sloth::WorkspaceOwner::Browser);
    if(!claimed || !browser_memory_begin(pixels,240*240*sizeof(uint16_t))){
      if(claimed)appWorkspace.release(sloth::WorkspaceOwner::Browser);
      displayStream.setDecodeWorkspace(pixels,240*240*sizeof(uint16_t));
      free(browserResumeUrl);browserResumeUrl=nullptr;menuUi.close();feedback="Browser memory unavailable";messageSince=now;lastFrame=0;return;
    }
    pixels=nullptr;
    if(!browser_app::open(browserResumeUrl,browserLoad))browserReclaim=true;
    free(browserResumeUrl);browserResumeUrl=nullptr;
    resetScreenInputs();
  }
  if(browser_app::active()) {
    browser_app::tick(now);
    const auto destination=browser_app::finished();
    if(destination!=browser_app::Destination::None) {
      if(destination==browser_app::Destination::Wifi)browserResumeUrl=strdup(browser_app::url());
      browser_app::destroy();browserDestination=destination;browserReclaim=true;
    }
  }
  if(browserReclaim) {
    if(displayIdle.level()==sloth::DisplayLevel::Off){
      browserDestination=browser_app::Destination::None;free(browserResumeUrl);browserResumeUrl=nullptr;
    }
    // All browser/network allocations have been released before reclaiming this.
    // The reserved canvas returns at its original address, without allocation.
    if(!pixels)pixels=static_cast<uint16_t*>(browser_memory_end());
    if(!pixels)return;
    appWorkspace.release(sloth::WorkspaceOwner::Browser);
    displayStream.cancel();
    if(!displayStream.setDecodeWorkspace(pixels,240*240*sizeof(uint16_t)))return;
    browserReclaim=false;resetScreenInputs();lastFrame=0;
    if(browserDestination==browser_app::Destination::Wifi) {
      browserDestination=browser_app::Destination::None;
      openWifiNetworks(false,now);wifiReturnBrowser=true;
    }
  }
}

// Wi-Fi setup is shared by the main menu, Browser and Remote Display.
// Its radio starts only after the old display worker has released ownership.
void openWifiNetworks(bool fromRemote, uint32_t now) {
  closeRadio();
  stopDisplayTransport();
  remoteRequested = false;
  pendingRemoteAttempt = RemoteAttempt::None;
  remoteUi.close();
  wifiReturnRemote = fromRemote;
  wifiNetworksPending = true;
  wifiNetworksUi.show(sloth::wifi_networks::snapshot());
  wifiNetworksUi.setWaiting(true);
  interact(now);
  resetScreenInputs();
  lastFrame = 0;
}

void closeWifiNetworks() {
  wifiReturnBrowser=false;
  wifiNetworksUi.close();
  wifiNetworksPending = false;
  if (wifiNetworksOwned) sloth::wifi_networks::stop();
  wifiNetworksOwned = false;
}

void applyWifiNetworksEvent(sloth::WifiNetworksEvent event, uint32_t now) {
  using E = sloth::WifiNetworksEvent;
  if (!wifiNetworksUi.isOpen()) return;
  interact(now);
  if (event == E::Back) {
    const bool backToRemote = wifiReturnRemote;
    const bool backToBrowser = wifiReturnBrowser;
    wifiReturnBrowser=false;
    closeWifiNetworks();
    if(backToBrowser)openBrowser(now,browserResumeUrl,false);
    if (backToRemote) {
      remoteRequested = true;
      remoteUi.show(sloth::RemoteStatus::Ready, static_cast<bool>(Serial));
    }
  } else if (!wifiNetworksPending && wifiNetworksOwned) {
    bool accepted = true;
    if (event == E::Scan) accepted = sloth::wifi_networks::scan();
    else if (event == E::ConnectSaved) accepted = sloth::wifi_networks::connectSaved();
    else if (event == E::ConnectNetwork) {
      accepted = sloth::wifi_networks::connect(wifiNetworksUi.ssid(), wifiNetworksUi.password());
      wifiNetworksUi.credentialsSubmitted(); // The service copied the credentials.
    } else if (event == E::Forget) accepted = sloth::wifi_networks::forgetSaved();
    if (!accepted) wifiNetworksUi.setNotice("COULD NOT COMPLETE REQUEST");
    if (event != E::None && event != E::Changed)
      wifiNetworksUi.update(sloth::wifi_networks::snapshot());
  }
  resetScreenInputs();
  lastFrame = 0;
}

void handleWifiNetworksTouch(bool down, int x, int y, uint32_t now) {
  if (!wifiNetworksUi.isOpen() || wakeGuard || appliedDisplay == sloth::DisplayLevel::Off) return;
  if (touchNeedsRelease) {
    if (!down) touchNeedsRelease = false;
    return;
  }
  const auto page = wifiNetworksUi.page();
  const auto event = wifiNetworksUi.touch(down, x, y);
  if (down) { interact(now); lastFrame = 0; }
  // Never log keyboard targets or field contents. Keep drag gestures alive.
  if (event == sloth::WifiNetworksEvent::Changed && page == wifiNetworksUi.page()) {
    interact(now); lastFrame = 0;
  } else if (event != sloth::WifiNetworksEvent::None || page != wifiNetworksUi.page()) {
    applyWifiNetworksEvent(event, now);
  }
}

void serviceWifiNetworks() {
  if (!wifiNetworksUi.isOpen() || storageBusQuiet.load()) return;
  if (appliedDisplay == sloth::DisplayLevel::Off) {
    if (wifiNetworksOwned) sloth::wifi_networks::stop();
    wifiNetworksOwned = false;
    wifiNetworksPending = true;
    wifiNetworksUi.setWaiting(true);
    return;
  }
  if (wifiNetworksPending) {
    if (sloth::wifi_display::busy() || radio_explorer::busy()) return;
    wifiNetworksPending = false;
    wifiNetworksOwned = true;
    wifiNetworksUi.setWaiting(false);
    sloth::wifi_networks::scan(); // Failure is visible through the snapshot.
  }
  sloth::wifi_networks::tick();
  const auto state = sloth::wifi_networks::snapshot();
  if (wifiNetworksUi.update(state)) lastFrame = 0;
  if (wifiReturnBrowser && state.state == sloth::wifi_networks::State::Connected &&
      state.error == sloth::wifi_networks::Error::None) {
    wifiReturnBrowser=false;
    closeWifiNetworks();
    openBrowser(millis(),browserResumeUrl,true);
    return;
  }
  if (wifiReturnRemote && state.state == sloth::wifi_networks::State::Connected &&
      state.error == sloth::wifi_networks::Error::None) {
    closeWifiNetworks();
    remoteRequested = true;
    remoteTriedUsb = false;
    queueRemoteAttempt(RemoteAttempt::WifiSaved, millis());
  }
}

void queueRemoteAttempt(RemoteAttempt attempt, uint32_t now) {
  closeRadio();
  closeWifiNetworks();
  stopDisplayTransport();
  pendingRemoteAttempt = attempt;
  displayEntryPending = true;
  remoteUi.show(sloth::RemoteStatus::Connecting, static_cast<bool>(Serial));
  displayIdle.begin(now);
  resetScreenInputs();
  lastFrame = 0;
}

void startRemoteAuto(uint32_t now) {
  remoteRequested = true;
  remoteTriedUsb = false;
  queueRemoteAttempt(RemoteAttempt::Auto, now);
}

void stopDisplayTransport() {
  if (usbDisplayMode) displayReply("STOP");
  audio_output::stop();
  displayNetworkControl = false;
  usbDisplayMode = false;
  volumeOverlay.clear();
  wifiDisplayMode = wifiProvisioned = false;
  sloth::wifi_display::stop();
  displayEntryPending = displayHasPixels = false;
  displayControlFailed = false;
  displayAudioCreditPending = false;
  displayStream.cancel();
  appWorkspace.release(sloth::WorkspaceOwner::Remote);
  remoteAttempt = RemoteAttempt::None;
  lastFrame = 0;
}

void exitUsbDisplay(const char* reason) {
  const bool fallback = remoteRequested && sloth::shouldFallbackToUsb(
      remoteAttempt == RemoteAttempt::WifiSaved,
      displayHasPixels, remoteTriedUsb, static_cast<bool>(Serial));
  stopDisplayTransport();
  pendingRemoteAttempt = RemoteAttempt::None;
  if (fallback) queueRemoteAttempt(RemoteAttempt::Usb, millis());
  else if (remoteRequested) remoteUi.show(sloth::RemoteStatus::NoHost, static_cast<bool>(Serial));
  Serial.printf("REMOTE stopped: %s\n", reason);
  resetScreenInputs();
}

void applyRemoteEvent(sloth::RemoteEvent event, uint32_t now) {
  interact(now);
  if (event == sloth::RemoteEvent::EditNetwork) {
    openWifiNetworks(true, now);
  } else if (event == sloth::RemoteEvent::Retry) startRemoteAuto(now);
  else if (event == sloth::RemoteEvent::UseUSB) queueRemoteAttempt(RemoteAttempt::Usb, now);
  else if (event == sloth::RemoteEvent::Back) {
    remoteRequested = false;
    pendingRemoteAttempt = RemoteAttempt::None;
    stopDisplayTransport(); remoteUi.close();
  }
  resetScreenInputs();
  lastFrame = 0;
}

void enterUsbDisplay() {
  if (sloth::wifi_display::busy() || sloth::wifi_networks::busy() || radio_explorer::busy()) return; // Previous radio owner must finish cleanup.
  displayEntryPending = false;
  if (!remoteRequested || displayIdle.level() == sloth::DisplayLevel::Off) return;
  remoteAttempt = pendingRemoteAttempt;
  pendingRemoteAttempt = RemoteAttempt::None;
  if (remoteAttempt == RemoteAttempt::Auto) {
    const auto transport = sloth::chooseRemoteTransport(sloth::wifi_display::hasSaved(), static_cast<bool>(Serial));
    if (transport == sloth::RemoteTransport::Wifi) remoteAttempt = RemoteAttempt::WifiSaved;
    else if (transport == sloth::RemoteTransport::Usb) remoteAttempt = RemoteAttempt::Usb;
    else {
      remoteAttempt = RemoteAttempt::None;
      openWifiNetworks(true, millis());
      lastFrame = 0;
      return;
    }
  }
  if (remoteAttempt == RemoteAttempt::None) return;
  if (remoteAttempt == RemoteAttempt::Usb) remoteTriedUsb = true;
  savePet();
  uint64_t nonce = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
  if (!nonce) nonce = 1;
  const bool claimed=appWorkspace.claim(sloth::WorkspaceOwner::Remote);
  if(!claimed || !displayStream.begin(nonce, millis())) {
    if(claimed)appWorkspace.release(sloth::WorkspaceOwner::Remote);
    remoteUi.show(sloth::RemoteStatus::Failed,static_cast<bool>(Serial));lastFrame=0;return;
  }
  wifiDisplayMode = remoteAttempt != RemoteAttempt::Usb;
  wifiProvisioned = false;
  displayNetworkControl = displayControlFailed = false;
  lastWifiState = sloth::wifi_display::State::Off;
  usbDisplayMode = true;
  displayHasPixels = false;
  volumeOverlay.clear();
  displayRegions = 0;
  displayFeedMicros = displayPanelMicros = displayAckMicros = 0;
  displayPerfSince = displayPerfLast = remoteAttemptSince = millis();
  remoteListeningSince = 0;
  remotePrompted = false;
  displayPerfRegions = 0;
  resetScreenInputs();
  displayIdle.visibleInteraction(millis());
  lastFrame = 0;
  if (wifiDisplayMode) {
    wifiProvisioned = sloth::wifi_display::beginSaved(nonce);
    if (!wifiProvisioned) { exitUsbDisplay("Wi-Fi startup failed"); return; }
  }
  reportUsbDisplay();
}

void handleDisplayEvent(sloth::DisplayStreamEvent event) {
  using E = sloth::DisplayStreamEvent;
  switch (event) {
    case E::None: break;
    case E::Query: reportUsbDisplay(); break;
    case E::WifiConfigure: {
      if (readingNetworkPacket || !usbDisplayMode || !wifiDisplayMode || wifiProvisioned) { displayStream.clearCredentials(); break; }
      const uint8_t *credentials = displayStream.wifiCredentials();
      char ssid[33] = {}, password[64] = {};
      memcpy(ssid, credentials + 2, credentials[0]);
      memcpy(password, credentials + 2 + credentials[0], credentials[1]);
      wifiProvisioned = sloth::wifi_display::begin(ssid, password, displayStream.nonce());
      volatile char *wipe = ssid;
      for (unsigned i = 0; i < sizeof(ssid); ++i) wipe[i] = 0;
      wipe = password;
      for (unsigned i = 0; i < sizeof(password); ++i) wipe[i] = 0;
      displayStream.clearCredentials();
      displayIdle.visibleInteraction(millis());
      if (!wifiProvisioned) {
        displayLine("DISPLAY WIFI_ERROR %016llx 1\n", static_cast<unsigned long long>(displayStream.nonce()));
        exitUsbDisplay("Wi-Fi setup failed");
      }
      break;
    }
    case E::AudioConfigured: {
      if (!displayNetworkControl || !readingNetworkPacket) break;
      const uint8_t* config = displayStream.audioPayload();
      bool ok = true;
      displayAudioCreditPending = false;
      displayAudioVolume = config[2];
      if (config[1]) ok = audio_output::active() ? audio_output::setVolume(config[2]) : audio_output::start(config[2]);
      else audio_output::stop();
      displayLine("DISPLAY AUDIO %016llx %u %u %u\n", static_cast<unsigned long long>(displayStream.nonce()),
          audio_output::active() ? 1u : 0u, displayAudioVolume, ok ? 0u : 1u);
      break;
    }
    case E::AudioSamples:
      if (displayNetworkControl && readingNetworkPacket && audio_output::active() &&
          audio_output::pushPcm(displayStream.audioPayload(), displayStream.audioBytes())) {
        displayAudioCreditPending = true;
        displayLine("DISPLAY AUDIO_BUFFER %016llx %u\n",static_cast<unsigned long long>(displayStream.nonce()),
            static_cast<unsigned>(audio_output::stats().queuedSamples));
      }
      break;
    case E::Ready:
      if (!usbDisplayMode) { displayStream.cancel(); displayReply("STOP"); break; }
      displayIdle.visibleInteraction(millis());
      displayReply("READY");
      break;
    case E::JpegFrame: {
      if (!usbDisplayMode) { displayStream.cancel(); displayReply("STOP"); break; }
      const auto& rect = displayStream.rectangle();
      // Reuse the pet canvas while display mode owns the panel. Decode fully
      // before presenting: malformed images cannot leave a partial JPEG onscreen.
      if (!board::decodeJpeg(rect.pixels, rect.bytes, pixels, 240 * 240)) {
        exitUsbDisplay("Image decoding failed");
        break;
      }
      const uint32_t panelStarted = micros();
      board::present(pixels, sampleDisplayButtons, 0); // Host already applies display rotation.
      displayPanelMicros += micros() - panelStarted;
      displayHasPixels = true;
      ++displayRegions;
      displayIdle.visibleInteraction(millis());
      displayReply("ACK");
      break;
    }
    case E::Rectangle: {
      if (!usbDisplayMode) { displayStream.cancel(); displayReply("STOP"); break; }
      const auto& rect = displayStream.rectangle();
      const uint32_t panelStarted = micros();
      const bool presented = board::presentRegion(rect.x, rect.y, rect.width, rect.height, rect.pixels, rect.bytes,
                                                rect.scale, rect.rle, 0); // Companion pixels already use the saved orientation.
      displayPanelMicros += micros() - panelStarted;
      if (!presented) {
        exitUsbDisplay("Display transfer failed");
        break;
      }
      displayHasPixels = true;
      ++displayRegions;
      displayIdle.visibleInteraction(millis());
      displayReply("ACK");
      break;
    }
    case E::Duplicate:
      if (usbDisplayMode && displayStream.connected()) displayIdle.visibleInteraction(millis());
      displayReply(displayStream.lastType() == sloth::DisplayPacketType::Hello ? "READY" : "ACK");
      break;
    case E::Pong:
      if (usbDisplayMode && displayStream.connected()) displayIdle.visibleInteraction(millis());
      displayReply("ACK");
      break;
    case E::PermissionNeeded:
    case E::HostError:
      if (usbDisplayMode) {
        displayHasPixels = false;
        remoteUi.show(event == E::PermissionNeeded ? sloth::RemoteStatus::PermissionNeeded
                                                  : sloth::RemoteStatus::Failed, static_cast<bool>(Serial));
        lastFrame = 0;
      }
      displayReply("ACK");
      break;
    case E::Stopped:
      if (usbDisplayMode) exitUsbDisplay("Display stopped");
      else displayReply("STOP");
      break;
    case E::Timeout: exitUsbDisplay("Display disconnected"); break;
    case E::Released:
      if (usbDisplayMode) exitUsbDisplay("Display stopped");
      displayReply("RELEASED");
      binarySerialLocked = false;
      readingTime = false;
      break;
    case E::Rejected:
      displayLine("DISPLAY NACK %016llx %lu %u\n",
          static_cast<unsigned long long>(displayStream.nonce()),
          static_cast<unsigned long>(displayStream.sequence()), static_cast<unsigned>(displayStream.error()));
      break;
  }
}

void readSerialInput(uint8_t byte, uint32_t now) {
  if (displayNetworkControl) return; // One authenticated owner; never interleave USB/TLS framing.
  if (byte == 0) {
    binarySerialLocked = true;
    readingTime = false;
    touchCommand.cancel();
  }
  if (binarySerialLocked) {
    handleDisplayEvent(displayStream.feed(byte, now));
  } else handleSerial(static_cast<char>(byte), now);
}

// Continuous Leaf Sweep contact bypasses the menu's tap-on-release recognizer.
// A release after pause/wake/menu changes is required before a new stroke, so
// neither resuming nor entering the game sweeps a path from stale coordinates.
void handleLeafTouch(bool down, int x, int y, uint32_t now) {
  if (!leafSweepActive || gameOverlay.isOpen() || wakeGuard ||
      appliedDisplay == sloth::DisplayLevel::Off || displayIdle.level() == sloth::DisplayLevel::Off) return;
  if (touchNeedsRelease) {
    if (!down) { touchNeedsRelease = false; touchDown = false; leafSweep.clearTouch(); }
    return;
  }
  if (gameFinished()) {
    touchDown = down;
    // Finishing a fatal swipe never also skips the game-over screen.
    if (!leafFinishedTouchReady) {
      if (!down) { leafFinishedTouchReady = true; tap = sloth::TouchTap(); }
      return;
    }
    if (tap.update(down, down ? 0 : -1, x, y, now) >= 0) finishGame(now);
    return;
  }
  if (down && !touchDown && x >= 92 && x < 148 && y >= 8 && y < 24) {
    powerTap(now);
    return;
  }
  leafSweep.touch(down, x, y, now);
  if (gameFinished()) leafFinishedTouchReady = false;
  touchDown = down;
  if (down) interact(now);
}

// Overlay gestures own their contact/release lifecycle. In-place name edits
// must not reset all input or demand a second release packet after every key.
void handleGameOverlayTouch(bool down, int x, int y, uint32_t now) {
  if (!gameOverlay.isOpen() || wakeGuard || appliedDisplay == sloth::DisplayLevel::Off ||
      displayIdle.level() == sloth::DisplayLevel::Off) return;
  if (touchNeedsRelease) {
    if (!down) {
      touchNeedsRelease = false; touchDown = false;
      gameOverlay.cancelTouch();
      // One physical release clears both gates after wake/hardware navigation.
      // The cancelled gesture cannot activate, and the next tap needn't be lost.
      gameOverlay.touch(false, lastTouchX, lastTouchY);
    }
    return;
  }
  const bool wasDown = touchDown;
  touchDown = down;
  if (down) { lastTouchX = x; lastTouchY = y; }
  const auto event = gameOverlay.touch(down, lastTouchX, lastTouchY);
  if (down || wasDown) { interact(now); lastFrame = 0; }
  if (event != sloth::GameOverlayEvent::None) applyGameOverlayEvent(event, now);
}

void handleForestTouch(bool down, int x, int y, uint32_t now) {
  if (!forestFidgetActive || wakeGuard || appliedDisplay == sloth::DisplayLevel::Off ||
      displayIdle.level() == sloth::DisplayLevel::Off) return;
  if (touchNeedsRelease) {
    if (!down) {
      touchNeedsRelease = false; touchDown = false; forestFidget.clearTouch();
      forestControls.touch(false, lastTouchX, lastTouchY);
    }
    return;
  }
  if (down) { lastTouchX = x; lastTouchY = y; interact(now); }
  // Navigation requires a completed tap, so a swipe from the toy into the
  // header never switches toys. Holding a side button also cannot paint.
  const auto target = sloth::ForestFidgetControls::hitTest(x, y);
  const auto nav = forestControls.touch(down, lastTouchX, lastTouchY);
  touchDown = down;
  forestFidget.touch(down && target == sloth::ForestFidgetAction::None, x, y, now);
  if (nav == sloth::ForestFidgetAction::Previous) activateControl(now);
  else if (nav == sloth::ForestFidgetAction::Back) powerTap(now);
  else if (nav == sloth::ForestFidgetAction::Next) nextControl(now);
  if (nav != sloth::ForestFidgetAction::None) {
    // This navigation was accepted on a real release. It also satisfies the
    // destination's fresh-touch gate; rapid consecutive taps need no idle packet.
    touchNeedsRelease = false;
    forestControls.touch(false, lastTouchX, lastTouchY);
  }
}

void handleTetrisTouch(bool down, int x, int y, uint32_t now) {
  if (!tetrisActive || gameOverlay.isOpen() || wakeGuard ||
      appliedDisplay == sloth::DisplayLevel::Off || displayIdle.level() == sloth::DisplayLevel::Off) return;
  if (touchNeedsRelease) {
    if (!down) {
      touchNeedsRelease = false; touchDown = false;
      tetrisControls.touch(false, lastTouchX, lastTouchY);
    }
    return;
  }
  tetris.advance(now);
  const bool wasDown = touchDown;
  if (wasDown && tetrisTouchPiece != tetris.pieceGeneration()) tetrisControls.reset();
  if (down) {
    lastTouchX = x; lastTouchY = y;
    if (!wasDown) {
      tetrisTouchPiece = tetris.pieceGeneration();
      tetrisTouchPlaying = !gameFinished();
    }
  }
  touchDown = down;
  const auto action = tetrisControls.touch(down, lastTouchX, lastTouchY);
  if (down || wasDown) { interact(now); lastFrame = 0; }
  if (action == sloth::TetrisAction::None) return;
  if (gameFinished()) {
    // A contact begun on a falling piece cannot dismiss its game-over result.
    if (!tetrisTouchPlaying) {
      finishGame(now);
      touchNeedsRelease = false;
      gameOverlay.touch(false, lastTouchX, lastTouchY);
    }
    return;
  }
  if (action == sloth::TetrisAction::Pause) {
    powerTap(now);
    // This contact has released; acknowledge both destination touch gates.
    touchNeedsRelease = false;
    gameOverlay.touch(false, lastTouchX, lastTouchY);
    return;
  }
  // A held button belongs to the piece visible at touch-down, never a new one
  // which gravity spawned during that contact. Release actions never repeat.
  if (tetrisTouchPiece != tetris.pieceGeneration()) return;
  if (action == sloth::TetrisAction::Left) tetris.moveLeft(now);
  else if (action == sloth::TetrisAction::Right) tetris.moveRight(now, false);
  else if (action == sloth::TetrisAction::Rotate) tetris.rotate(now);
  else if (action == sloth::TetrisAction::Drop) tetris.hardDrop(now);
}

void closeRadio() {
  radioUi.close();
  radioStartPending = radioSuspended = false;
  radio_explorer::stop();
}

void openRadio(sloth::RadioKind kind, uint32_t now) {
  closeRadio();
  closeWifiNetworks();
  clearSpeech();
  stopDisplayTransport();
  remoteRequested = false;
  remoteUi.close();
  radioUi.open(kind);
  if(!radioUi.isOpen()){feedback="Radio memory unavailable";messageSince=now;lastFrame=0;return;}
  radioStartPending = true;
  radioMotionSamples = 0;
  lastRadioUpdate = 0;
  interact(now);
}

void applyRadioEvent(sloth::RadioEvent event, uint32_t now) {
  if (event == sloth::RadioEvent::None) return;
  if (event == sloth::RadioEvent::Back) closeRadio();
  else if (event == sloth::RadioEvent::SelectionChanged) radio_explorer::track(radioUi.selected());
  else if (event == sloth::RadioEvent::Refresh && !radioStartPending &&
           !sloth::wifi_display::busy() && !sloth::wifi_networks::busy()) radio_explorer::refresh();
  interact(now);
  lastFrame = 0;
  if (event != sloth::RadioEvent::Changed) resetScreenInputs();
}

void handleRadioTouch(bool down, int x, int y, uint32_t now) {
  if (!radioUi.isOpen() || wakeGuard || appliedDisplay == sloth::DisplayLevel::Off) return;
  if (touchNeedsRelease) {
    if (!down) {
      touchNeedsRelease = false; touchDown = false;
      radioUi.touch(false, lastTouchX, lastTouchY);
    }
    return;
  }
  const bool wasDown = touchDown;
  touchDown = down;
  if (down) { lastTouchX = x; lastTouchY = y; }
  const auto event = radioUi.touch(down, lastTouchX, lastTouchY);
  if (down || wasDown) { interact(now); lastFrame = 0; }
  applyRadioEvent(event, now);
}

void serviceRadio(uint32_t now) {
  if (storageBusQuiet.load()) return;
  const bool visible = radioUi.isOpen() && appliedDisplay != sloth::DisplayLevel::Off;
  if (radioUi.isOpen() && !visible && !radioSuspended) {
    radio_explorer::stop();
    radioStartPending = radioSuspended = true;
  }
  if (visible && radioStartPending && !radio_explorer::busy() && !sloth::wifi_display::busy() && !sloth::wifi_networks::busy()) {
    radioStartPending = radioSuspended = false;
    radio_explorer::start(radioUi.snapshot().kind); // Failure is reported in the snapshot and can be retried.
    lastRadioUpdate = 0;
  }
  if (radioUi.isOpen() && !radioStartPending && (!lastRadioUpdate || now - lastRadioUpdate >= 125)) {
    lastRadioUpdate = now;
    const auto previousPage = radioUi.page();
    radio_explorer::updateUi(radioUi, now);
    if (previousPage != radioUi.page()) radio_explorer::track(radioUi.selected());
  }
  const bool wantGyro = visible && radioUi.page() == sloth::RadioPage::Detail;
  if (wantGyro != lastGyroWanted || now - lastGyroConfigure >= 500) {
    lastGyroWanted = wantGyro;
    lastGyroConfigure = now;
    // Also retries an uncertain rollback even if the cached state is already false.
    sensors::enableGyroscope(wantGyro);
  }
}

sensors::TouchStatus readScreenTouch(uint16_t& x, uint16_t& y) {
  const auto state = sensors::readTouch(x, y);
  if (state == sensors::TouchStatus::Pressed || state == sensors::TouchStatus::Released) {
    unsigned tx = x, ty = y;
    if (sloth::unrotateTouch(tx, ty, 480, settings.screenRotation)) { x = tx; y = ty; }
  }
  return state;
}

void pollInputs(uint32_t now) {
  if ((usbDisplayMode && displayHasPixels) || displayEntryPending ||
      (gameActive() && !leafSweepActive && !tetrisActive && !gameOverlay.isOpen())) return;
  if (displayIdle.level() == sloth::DisplayLevel::Off ||
      appliedDisplay == sloth::DisplayLevel::Off || wakeGuard) return;
  if (now - lastPoll < 20) return;
  lastPoll = now;
  // Skip unused IMU transactions for continuous play and modal touch controls.
  if (gameOverlay.isOpen() || leafSweepActive || tetrisActive) {
    uint16_t tx = 0, ty = 0;
    const auto touch = readScreenTouch(tx, ty);
    if (touch == sensors::TouchStatus::Pressed || touch == sensors::TouchStatus::Released) {
      if (gameOverlay.isOpen()) handleGameOverlayTouch(touch == sensors::TouchStatus::Pressed, tx / 2, ty / 2, now);
      else if (tetrisActive) handleTetrisTouch(touch == sensors::TouchStatus::Pressed, tx / 2, ty / 2, now);
      else handleLeafTouch(touch == sensors::TouchStatus::Pressed, tx / 2, ty / 2, now);
    }
    return;
  }
  float x, y, z, gx = 0, gy = 0, gz = 0;
  const bool sixAxis = sensors::gyroscopeEnabled();
  const bool motionReady = sixAxis ? sensors::readMotion(x, y, z, gx, gy, gz) : sensors::readAcceleration(x, y, z);
  if (motionReady) {
    if (radioUi.isOpen() && radioUi.page() == sloth::RadioPage::Detail && sixAxis) {
      radioUi.motion(gx, gy, gz, x, y, z, now);
      ++radioMotionSamples;
      if (gx * gx + gy * gy + gz * gz > 144.0f) interact(now);
    }
    if (lastAcceleration) maxSampleGap = max(maxSampleGap, now - lastAcceleration);
    lastAcceleration = now;
    ++accelerationSamples;
    accelerationX = x; accelerationY = y; accelerationZ = z;
    if (forestFidgetActive) {
      // Panel gravity is X/right and Y/down with buttons across the top.
      // The previous extra Y negation inverted top/bottom toy movement.
      float screenX = x, screenY = y;
      sloth::unrotateVector(screenX, screenY, settings.screenRotation);
      forestFidget.motion(screenX, screenY, z, now);
    } else if (settingsUi.isOpen() && settingsUi.page() == sloth::SettingsPage::Motion) {
      if (!testRateSince) testRateSince = now;
      ++testRateSamples;
      if (now - testRateSince >= 1000) {
        testSampleHz = testRateSamples * 1000.0f / (now - testRateSince);
        testRateSince = now;
        testRateSamples = 0;
      }
      shakeTest.setSensitivity(settingsUi.draft().shakeSensitivity);
      if (shakeTest.sample(x, y, z, now)) {
        interact(now);
        Serial.println("SHAKE TEST detected: no care reward");
      }
    } else if (!settingsUi.isOpen() && !menuUi.isOpen() && !remoteRequested) {
      shake.setSensitivity(settings.shakeSensitivity);
      if (shake.sample(x, y, z, now)) {
        Serial.println("SHAKE detected");
        perform(sloth::Action::Shake, now);
      }
    }
  }
  uint16_t tx = 0, ty = 0;
  const auto touch = readScreenTouch(tx, ty);
  if (touch == sensors::TouchStatus::Pressed || touch == sensors::TouchStatus::Released) {
    const bool down = touch == sensors::TouchStatus::Pressed;
    if(browserPending || browserReclaim)return;
    if (browser_app::active()) {handleBrowserTouch(down,tx/2,ty/2,now);return;}
    if (wifiNetworksUi.isOpen()) {
      handleWifiNetworksTouch(down, tx / 2, ty / 2, now);
      return;
    }
    if (forestFidgetActive) {
      handleForestTouch(down, tx / 2, ty / 2, now);
      return;
    }
    if (radioUi.isOpen()) {
      handleRadioTouch(down, tx / 2, ty / 2, now);
      return;
    }
    if (!remoteRequested && !settingsUi.isOpen() && menuUi.isOpen()) {
      handleMenuTouch(down, tx / 2, ty / 2, now);
      return;
    }
    // A finger already resting on the sleeping screen cannot become a new tap.
    if (touchNeedsRelease) {
      if (!down) touchNeedsRelease = false;
      return;
    }
    const int target = !down ? -1 : remoteRequested ? remoteUi.hitTest(tx / 2, ty / 2)
        : settingsUi.isOpen() ? settingsUi.hitTest(tx / 2, ty / 2)
        : (sloth::hitTestPet(tx / 2, ty / 2, pet.snapshot(), now, settings,
              now - actionSince < (animation == 3 ? 4000u : 2500u) ? animation : 0,
              reaction.active(now), reaction.elapsed(now)) ||
            sloth::hitTestPetSpeech(tx / 2, ty / 2, speech.text(now)))
            ? sloth::kPetTalkTarget : sloth::hitTestAction(tx / 2, ty / 2);
    if (down && !touchDown) {
      interact(now);
      Serial.printf("TOUCH down x=%u y=%u target=%d\n", tx, ty, target);
    } else if (!down && touchDown) {
      Serial.println("TOUCH up");
    }
    touchDown = down;
    if (down) { lastTouchX = tx / 2; lastTouchY = ty / 2; }
    if (remoteRequested) {
      const auto previousPage = remoteUi.page();
      const auto event = remoteUi.touch(down, lastTouchX, lastTouchY);
      if (down) { interact(now); lastFrame = 0; }
      if (event == sloth::RemoteEvent::Changed && previousPage == remoteUi.page()) {
        // A drag updates scroll continuously; keep its touch gesture alive.
        interact(now); lastFrame = 0;
      } else if (event != sloth::RemoteEvent::None || previousPage != remoteUi.page()) applyRemoteEvent(event, now);
    } else if (settingsUi.isOpen()) {
      const auto previousPage = settingsUi.page();
      const auto event = settingsUi.touch(down, lastTouchX, lastTouchY);
      if (down) { interact(now); lastFrame = 0; }
      if (event != sloth::SettingsEvent::None || settingsUi.page() != previousPage) {
        applySettingsEvent(event, now); lastFrame = 0;
      }
    } else {
      const int action = tap.update(down, target, tx / 2, ty / 2, now);
      if (action >= 0) {
        activateHome(static_cast<unsigned>(action), now);
      }
    }
  }
  if (diagnostics && now - lastDiagnostic >= 1000) {
    lastDiagnostic = now;
    printState();
  }
}

void resetScreenInputs() {
  tetrisControls.reset();
  tetrisMoveButton.reset();
  diagnosticTetrisHeld = false;
  tetrisTouchPiece = 0;
  tetrisTouchPlaying = false;
  gameOverlay.cancelTouch();
  displayKeyPending = displayBootPending = false;
  menuUi.cancelTouch();
  settingsUi.cancelTouch();
  remoteUi.cancelTouch();
  radioUi.cancelTouch();
  wifiNetworksUi.cancelTouch();
  browser_app::cancelTouch();
  leafSweep.clearTouch();
  forestFidget.clearTouch();
  forestControls.reset();
  leafFinishedTouchReady = false;
  touchCommand.discard();
  tap = sloth::TouchTap();
  shake = sloth::ShakeGesture();
  shake.setSensitivity(settings.shakeSensitivity);
  shakeTest = sloth::ShakeGesture();
  testRateSince = testRateSamples = 0;
  testSampleHz = 0;
  shakeTest.setSensitivity(settingsUi.isOpen() ? settingsUi.draft().shakeSensitivity : settings.shakeSensitivity);
  touchDown = false;
  touchNeedsRelease = true;
  lastAcceleration = 0;
}

sloth::MotionView motionView(uint32_t now) {
  sloth::MotionView view;
  const auto& t = shakeTest.telemetry();
  view.available = capabilities.acceleration;
  view.ready = view.available && t.samples && lastAcceleration && now - lastAcceleration <= 250;
  view.x = t.x; view.y = t.y; view.z = t.z;
  view.magnitude = t.magnitude; view.motion = t.motion; view.peak = t.peak;
  view.tiltX = t.tiltX; view.tiltY = t.tiltY;
  view.samples = t.samples; view.triggers = t.triggers; view.peaks = t.peaks;
  view.cooldown = !t.ready || t.cooldown;
  view.triggered = t.triggers && now - t.lastTrigger < 1500;
  view.threshold = shakeTest.threshold();
  view.sampleHz = testSampleHz;
  return view;
}

void drawScreen(uint32_t now, int action = 0) {
  if (wifiNetworksUi.isOpen()) sloth::drawWifiNetworks(pixels, wifiNetworksUi);
  else if (remoteRequested) {
    sloth::drawRemoteDisplay(pixels, remoteUi);
    if (volumeOverlay.visible(now)) sloth::drawVolumeOverlay(pixels, volumeOverlay.volume());
  } else if (pongActive) sloth::drawPong(pixels, pong);
  else if (tetrisActive) sloth::drawTetris(pixels, tetris, static_cast<int>(tetrisControls.pressed()), now);
  else if (leafSweepActive) sloth::drawLeafSweep(pixels, leafSweep);
  else if (forestFidgetActive) sloth::drawForestFidget(pixels, forestFidget.snapshot());
  else if (radioUi.isOpen()) sloth::drawRadioExplorer(pixels, radioUi, now);
  else if (settingsUi.isOpen()) sloth::drawSettings(pixels, settingsUi, motionView(now), storageView);
  else if (menuUi.isOpen()) sloth::drawMenu(pixels, menuUi);
  else sloth::drawPet(pixels, pet.snapshot(), selected, status(now), now, action, hud, settings, speech.text(now),
      reaction.active(now), reaction.elapsed(now));
  if (gameOverlay.isOpen()) sloth::drawGameOverlay(pixels, gameOverlay, gameRecords, gameRecordsSavePending, gameRecordsSaveFailed);
}

void applyDisplayState() {
  if (storageBusQuiet.load()) return;
  const auto requested = displayIdle.level();
  if (!pixels) {
    if(browser_app::active()) {
      if(requested == sloth::DisplayLevel::Off) browser_app::close();
      return; // Drain the browser before changing the panel's power state.
    }
    // Failed canvas reclamation must not prevent sleep. Waking still waits for
    // a canvas so showDisplay can receive its required complete fresh frame.
    if(requested != sloth::DisplayLevel::Off)return;
  }
  if (requested == appliedDisplay) return;
  if (requested == sloth::DisplayLevel::Off) {
    // All prior frame DMA has completed. Never sleep the panel from a callback.
    savePet();
    clearSpeech();
    resetScreenInputs();
    board::sleepDisplay();
    appliedDisplay = requested;
    Serial.println("DISPLAY off: panel sleeping, rendering and touch/shake polling stopped");
  } else if (appliedDisplay == sloth::DisplayLevel::Off) {
    board::wakeDisplay();
    board::brightness(65);
    // Waking the controller takes time; account it before drawing fresh state.
    const uint32_t ready = millis();
    if (pongActive) pong.resume(ready);
    if (tetrisActive) tetris.resume(ready);
    if (leafSweepActive) leafSweep.resume(ready);
    if (forestFidgetActive) forestFidget.resume(ready);
    advancePet(ready);
    updateHud(ready, true);
    resetScreenInputs();
    drawScreen(ready);
    board::present(pixels); // Complete fresh frame before enabling emission.
    ++displayFrames;
    board::showDisplay();
    appliedDisplay = requested;
    lastFrame = millis();
    Serial.println("DISPLAY bright: wake-only, pet and selection unchanged");
  } else {
    board::brightness(requested == sloth::DisplayLevel::Dim ? 12 : 65);
    appliedDisplay = requested;
    Serial.printf("DISPLAY %s\n", displayName(requested));
  }
}
}  // namespace

void setup() {
  // A complete acknowledged native stripe fits even while panel DMA is busy.
  // HWCDC drops received bytes when its default 256-byte queue fills.
  constexpr size_t usbReceiveBytes = 16384;
  static_assert(usbReceiveBytes >= sloth::kDisplayReceiveBytes + 2 + 512,
      "USB stop-and-wait must fit a complete wire packet plus control traffic");
  ESP_ERROR_CHECK(Serial.setRxBufferSize(usbReceiveBytes) == usbReceiveBytes ? ESP_OK : ESP_ERR_NO_MEM);
  Serial.begin(115200);
  delay(700); // USB may attach late; never wait indefinitely for a monitor.
  Serial.println("\nMOSS pocket pet v1.39 - shared screen rotation and public release builds");
  Serial.printf("RESET reason=%d\n", static_cast<int>(esp_reset_reason()));
  board::begin();
  audio_output::stop();
  powerButtonReady = pet_power::beginButton();
  Serial.printf("PWR button wake=%s\n", powerButtonReady ? "ready" : "retrying");
  capabilities = sensors::begin();
  pixels = static_cast<uint16_t*>(heap_caps_aligned_alloc(16,240 * 240 * sizeof(uint16_t),MALLOC_CAP_8BIT));
  ESP_ERROR_CHECK(pixels ? ESP_OK : ESP_ERR_NO_MEM);
  // Display mode owns this canvas: JPEG and LZ4 consume it sequentially.
  ESP_ERROR_CHECK(appWorkspace.bind(pixels,240*240*sizeof(uint16_t)) ? ESP_OK : ESP_ERR_INVALID_ARG);
  ESP_ERROR_CHECK(displayStream.setDecodeWorkspace(pixels, 240 * 240 * sizeof(uint16_t)) ? ESP_OK : ESP_ERR_INVALID_ARG);
  loadPet();
  loadHumor();
  loadSettings();
  board::setScreenRotation(settings.screenRotation);
  nextButton.begin();
  doButton.begin();
  messageSince = millis();
  displayIdle.begin(messageSince);
  updateHud(millis(), true);
  drawScreen(millis());
  board::present(pixels);
  ++displayFrames;
  Serial.println("READY: HOME BOOT=next KEY=select; MENUS BOOT=next KEY=select PWR=back; serial s=status w=save t=clock T<utc>=sync f=feed p=play n=nap h=dance q=joke u=menu j=next k=select [=BOOT ]=KEY x=cancel o=screen-off v=screen-wake b=power-tap d=diagnostics");
  printState();
}

void loop() {
  sloth::wifi_display::reap();
  const uint32_t now = millis();
  if (remoteRequested) remoteUi.setUsbAvailable(static_cast<bool>(Serial));
  if(petHomeVisible())advancePet(now);
  const bool keyPressed = nextButton.pressed(now) || displayKeyPending;
  if (doButton.pressed(now)) captureBootEdge(now);
  const bool bootPressed = displayBootPending;
  const bool bootBeganInTetris = displayBootTetris;
  const uint32_t bootAt = displayBootAt, bootPiece = displayBootPiece;
  displayKeyPending = displayBootPending = false;
  const bool deferTetrisLeft = tetrisActive && !gameOverlay.isOpen() && !gameFinished();
  bool powerPressed = false;
  if (now - lastPowerButtonPoll >= (powerButtonReady ? 50u : 5000u)) {
    lastPowerButtonPoll = now;
    if (!powerButtonReady) powerButtonReady = pet_power::beginButton();
    else powerPressed = pet_power::readButton() == pet_power::ButtonEvent::Pressed;
  }
  if (powerPressed) {
    powerTap(millis());
  } else if (keyPressed || bootPressed) {
    if (usbDisplayMode) {
      // Ignore simultaneous +/- edges and wake-only/held edges. One debounced
      // physical press changes volume once; neither button exits or rotates.
      if (!wakeGuard && keyPressed != bootPressed) {
        routeSideButtons(keyPressed, bootPressed, millis());
      }
      displayIdle.visibleInteraction(millis());
    } else {
      const bool wakeOnly = hardwareActivity(millis());
      Serial.printf("BUTTON key=%u boot=%u pwr=%u wake_only=%u\n",
          keyPressed, bootPressed, powerPressed, wakeOnly);
      if (!wakeOnly) routeSideButtons(keyPressed && !(deferTetrisLeft && bootPressed), bootPressed && !deferTetrisLeft, millis());
    }
  }
  const uint32_t afterButtons = millis();
  if (gameActive() && !gameOverlay.isOpen() && appliedDisplay != sloth::DisplayLevel::Off) {
    if (pongActive) {
      pong.advance(afterButtons);
      if (pong.snapshot().phase == sloth::PongPhase::Playing) interact(afterButtons);
    } else if (tetrisActive) {
      tetris.advance(afterButtons);
      if (tetris.snapshot().phase == sloth::TetrisPhase::Playing) interact(afterButtons);
    } else {
      leafSweep.advance(afterButtons);
      if (!gameFinished()) interact(afterButtons);
    }
  }
  // Recognize physical left-button short/hold only after advancing gravity so
  // a contact begun on the preceding piece cannot affect its replacement.
  if (diagnosticTetrisHeld && afterButtons - diagnosticTetrisSince > 3000u)
    diagnosticTetrisHeld = false;
  const bool moveEnabled = tetrisActive && !gameOverlay.isOpen() && !gameFinished() && !wakeGuard &&
      appliedDisplay != sloth::DisplayLevel::Off && displayIdle.level() != sloth::DisplayLevel::Off;
  const bool moveDown = doButton.down() || diagnosticTetrisHeld;
  // DMA may see the edge before gravity spawns another piece. Preserve both
  // its generation and timestamp, including complete taps between main loops.
  if (moveEnabled && bootPressed && bootBeganInTetris && !keyPressed)
    tetrisMoveButton.press(bootAt, bootPiece);
  const auto moveAction = tetrisMoveButton.update(moveDown, afterButtons, tetris.pieceGeneration(),
      moveEnabled && !(keyPressed && bootPressed));
  if (moveAction != sloth::TetrisAction::None) {
    tetrisControls.reset(); tetrisTouchPiece = 0; touchDown = false; touchNeedsRelease = true;
    if (moveAction == sloth::TetrisAction::Drop) tetris.hardDrop(afterButtons);
    else tetris.moveRight(afterButtons);
    interact(afterButtons); lastFrame = 0;
    Serial.printf("TETRIS move-button action=%s\n", moveAction == sloth::TetrisAction::Drop ? "drop" : "move");
  }
  if (forestFidgetActive && appliedDisplay != sloth::DisplayLevel::Off &&
      displayIdle.level() != sloth::DisplayLevel::Off) {
    forestFidget.advance(afterButtons);
    interact(afterButtons); // Keep tilt-only toys bright without requiring taps.
  }
  if (nextButton.held() || doButton.held()) interact(afterButtons);
  else if (wakeGuard && afterButtons - wakeGuardSince >= 100) wakeGuard = false;
  pollInputs(afterButtons);
  // Local USB diagnostics also exercise the same game action path.
  const uint32_t serialSince = millis();
  uint8_t serialChunk[256];
  for (unsigned count = 0; count < 8192 && millis() - serialSince < 12;) {
    // Bulk reads avoid repeated queue-count and microsecond-clock operations
    // for every pixel byte. Bound each chunk so hardware buttons stay responsive.
    const size_t received = Serial.read(serialChunk, sizeof(serialChunk));
    if (!received || received > sizeof(serialChunk)) break;
    const uint32_t receivedAt = millis();
    for (size_t i = 0; i < received; ++i)
      readSerialInput(serialChunk[i], binarySerialLocked ? receivedAt : millis());
    count += received;
  }
  if (wifiDisplayMode && wifiProvisioned) {
    const auto wifi = sloth::wifi_display::snapshot();
    if (wifi.state != lastWifiState) {
      Serial.printf("WIFI state=%u error=%u heap=%lu rssi=%ld\n", static_cast<unsigned>(wifi.state),
          static_cast<unsigned>(wifi.error), static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<long>(wifi.rssi));
      lastWifiState = wifi.state;
      if (wifi.state == sloth::wifi_display::State::Listening) {
        remoteListeningSince = millis();
        // Pairing material travels only on the trusted physical USB connection.
        reportUsbDisplay();
      } else if (wifi.state == sloth::wifi_display::State::Connected) {
        displayStream.begin(wifi.nonce, millis()); // Drop any partial USB packet before TLS owns the parser.
        binarySerialLocked = true;
        displayNetworkControl = true;
        reportUsbDisplay(); // Only authenticated peers receive session metadata.
      } else if (wifi.state == sloth::wifi_display::State::Failed) {
        displayLine("DISPLAY WIFI_ERROR %016llx %u\n", static_cast<unsigned long long>(displayStream.nonce()),
            static_cast<unsigned>(wifi.error));
        exitUsbDisplay("Wi-Fi disconnected");
      }
    }
    const uint32_t wifiSince = millis();
    uint8_t received[512];
    while (wifiDisplayMode && millis() - wifiSince < 12) {
      const size_t count = sloth::wifi_display::read(received, sizeof(received));
      if (!count) break;
      const uint32_t receivedAt = millis();
      const uint32_t feedStarted = micros();
      readingNetworkPacket = true;
      for (size_t i = 0; i < count && wifiDisplayMode; ++i) handleDisplayEvent(displayStream.feed(received[i], receivedAt));
      readingNetworkPacket = false;
      displayFeedMicros += micros() - feedStarted;
    }
    if (wifiDisplayMode && wifi.state == sloth::wifi_display::State::Connected &&
        millis() - displayPerfLast >= 1000 && displayRegions != displayPerfRegions) {
      displayPerfLast = millis();
      displayPerfRegions = displayRegions;
      const auto perf = sloth::wifi_display::snapshot();
      // Only numeric performance data; never print pairing material or content.
      displayLine("DISPLAY PERF %016llx %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %ld %lu\n",
          static_cast<unsigned long long>(displayStream.nonce()),
          static_cast<unsigned long>(displayPerfLast - displayPerfSince),
          static_cast<unsigned long>(displayRegions), static_cast<unsigned long>(perf.receivedBytes),
          static_cast<unsigned long>(displayFeedMicros), static_cast<unsigned long>(displayPanelMicros),
          static_cast<unsigned long>(displayAckMicros), static_cast<unsigned long>(perf.tlsReadMicros),
          static_cast<unsigned long>(perf.tlsReadCalls), static_cast<unsigned long>(perf.socketRxBytes),
          static_cast<unsigned long>(perf.socketWouldBlock), static_cast<unsigned long>(perf.rxQueueFullTicks),
          static_cast<unsigned long>(perf.rxQueueHighWaterBytes), static_cast<long>(perf.rssi),
          static_cast<unsigned long>(ESP.getFreeHeap()));
    }
  }
  static uint32_t lastAudioPerf = 0;
  if (displayNetworkControl && audio_output::active() && millis() - lastAudioPerf >= 1000) {
    lastAudioPerf = millis();
    const auto stats = audio_output::stats();
    displayLine("DISPLAY AUDIO_PERF %016llx %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
        static_cast<unsigned long long>(displayStream.nonce()), static_cast<unsigned long>(stats.queuedSamples),
        static_cast<unsigned long>(stats.underruns), static_cast<unsigned long>(stats.overflows),
        static_cast<unsigned long>(stats.droppedSamples), static_cast<unsigned long>(stats.writeErrors),
        static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
        static_cast<unsigned long>(stats.receivedSamples), static_cast<unsigned long>(stats.renderedSamples));
  }
  // Grant one more packet only when the PCM ring has room for the maximum
  // payload. ACK on receipt alone allowed catch-up bursts to overwrite audio.
  if(displayAudioCreditPending && displayNetworkControl && audio_output::active() &&
      audio_output::canAcceptPacket(audio_output::stats().queuedSamples)) {
    displayAudioCreditPending=false;
    displayLine("DISPLAY AUDIO_ACK %016llx\n", static_cast<unsigned long long>(displayStream.nonce()));
  }
  if (remoteRequested && usbDisplayMode && !displayStream.connected() && !remotePrompted) {
    if (sloth::remoteAttemptTimedOut(wifiDisplayMode, remoteAttemptSince, remoteListeningSince,
        lastWifiState == sloth::wifi_display::State::Listening, millis()))
    {
      if (sloth::shouldFallbackToUsb(wifiDisplayMode, displayHasPixels, remoteTriedUsb, static_cast<bool>(Serial)))
        exitUsbDisplay("No companion found");
      else {
        // Keep announcing while displaying useful options: opening the Mac
        // app later can still connect without restarting this screen.
        remotePrompted = true;
        remoteUi.show(sloth::RemoteStatus::NoHost, static_cast<bool>(Serial));
        resetScreenInputs();
        lastFrame = 0;
      }
    }
  }
  if (displayControlFailed) { displayControlFailed = false; exitUsbDisplay("Wi-Fi control disconnected"); }
  handleDisplayEvent(displayStream.poll(millis()));
  // A relaunched companion cannot safely probe unknown firmware with binary.
  // Advertise the retained session so it can release a stale binary lock.
  static uint32_t lastDisplayBeacon = 0;
  if ((binarySerialLocked || usbDisplayMode) && millis() - lastDisplayBeacon >= 2000) {
    lastDisplayBeacon = millis();
    reportUsbDisplay();
  }
  const bool maintenance=sloth::backgroundMaintenanceAllowed(remoteRequested,
      browserPending||browser_app::active()||browserReclaim,storageScanActive);
  if(maintenance){
    updateHud(millis());servePendingJoke();savePendingSettings();savePendingGameRecords();
  }
  serviceWifiNetworks();
  serviceBrowser(millis());
  serviceStorage();
  if (pongSpeedsSavePending) {
    pongSpeedsSavePending = false;
    const uint16_t speeds = menuUi.speed(0) | (menuUi.speed(1) << 8);
    const bool saved = storage.putUShort("pong-speeds", speeds) == sizeof(speeds);
    Serial.printf("PONG speeds save %s\n", saved ? "ok" : "failed");
  }
  serviceGameAudio();
  if (displayEntryPending && !storageScanActive) enterUsbDisplay();
  const uint32_t afterInputs = millis();
  touchCommand.expire(afterInputs);
  if (readingTime && afterInputs - timeCommandStarted >= 2000) readingTime = false;
  // Save in the same loop iteration, outside a display-DMA callback, so a
  // power-off directly after feeding does not lose three seconds of actions.
  // Entry saves a timestamped pet snapshot. Catch up and persist on return;
  // do not stall real-time audio/video with background NVS writes every minute.
  if (maintenance && ((dirty && (!saveFailed || afterInputs - lastSave >= 5000)) ||
      afterInputs - lastSave >= 60000)) savePet();
  if(browser_app::active() && displayIdle.level()!=sloth::DisplayLevel::Off) displayIdle.visibleInteraction(millis());
  displayIdle.update(millis());
  if (usbDisplayMode && displayIdle.level() == sloth::DisplayLevel::Off)
    exitUsbDisplay("Display timed out");
  applyDisplayState();
  serviceRadio(millis());
  // Re-read millis after any blocking panel transition; never pass old times
  // back into care accounting or the display's input callback.
  const uint32_t frameNow = millis();
  const uint32_t frameInterval = (gameActive() && !gameOverlay.isOpen()) || forestFidgetActive ? 20u
      : appliedDisplay == sloth::DisplayLevel::Dim ? 400u
      : petHomeVisible() && reaction.active(frameNow) >= 0 ? 80u : 125u;
  if (pixels && !browser_app::active() && !storageBusQuiet.load() && appliedDisplay != sloth::DisplayLevel::Off && (!usbDisplayMode || !displayHasPixels) &&
      frameNow - lastFrame >= frameInterval) {
    lastFrame = frameNow;
    drawScreen(frameNow,
        frameNow - actionSince < (animation == 3 ? 4000u : 2500u) ? animation : 0);
    board::present(pixels, []() {
      sampleDisplayButtons();
      if (!gameActive() || leafSweepActive || tetrisActive || gameOverlay.isOpen()) pollInputs(millis());
    }); // All local screens share the saved orientation.
    ++displayFrames;
  }
  serviceGameAudio();
  delay(usbDisplayMode ? 1 : appliedDisplay == sloth::DisplayLevel::Off ? 20 : 5);
}
