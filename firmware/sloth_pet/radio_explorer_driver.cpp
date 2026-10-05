#include "radio_explorer.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Network.h>
#include <atomic>
#include <string.h>
#include "esp_wifi.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"

#if defined(ESP_PLATFORM)
#include "esp_idf_version.h"
#include "esp_arduino_version.h"
#if !CONFIG_IDF_TARGET_ESP32C6 || ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,0) || ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3,3,0)
#error "Re-audit the C6 BLE scan allocation workaround before changing the pinned SDK."
#endif
static_assert(sizeof(void*)==4,"BLE controller workaround requires its audited 32-bit ABI");
#endif

// Pinned Arduino3.3.0 / IDF5.5-b66b5448 C6 libble_app.a, ble_ll_scan.c.o:
// r_ble_ll_scan_env_init allocates3 bytes, then writes a4-byte pointer at +3.
// The resulting three-byte tail-canary overwrite crashes controller deinit.
// ELF disassembly and a live heap-poisoning trace confirm this exact path.
// Related upstream report: https://github.com/espressif/arduino-esp32/issues/12821
// Link with --wrap=r_ble_ll_mem_generic_data_init. Reserve the omitted pointer
// only for that tiny allocation shape; ownership, failure and free stay vendor
// controlled. Never disable heap poisoning or repair a corrupted canary.
extern "C" int __real_r_ble_ll_mem_generic_data_init(void**,uint32_t,uint32_t);
extern "C" int __wrap_r_ble_ll_mem_generic_data_init(void** destination,uint32_t size,uint32_t count){
  return __real_r_ble_ll_mem_generic_data_init(destination,size==3&&count==1?7:size,count);
}

// Primary references: ESP-IDF5.5 C6 Wi-Fi scan guide (passive/blocking scans),
// ESP-IDF5.5 C6 NimBLE programming sequence, and Apache NimBLE ble_gap_disc.
// This module never advertises, associates, pairs, requests a BLE connection,
// sends a scan request, or persists discovered addresses/names.
namespace radio_explorer {
namespace {
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
sloth::RadioSnapshot shared;
sloth::RadioEntry target;
bool targeting = false;
std::atomic<bool> running{false}, cancelled{false}, refreshRequested{false};
std::atomic<bool> hostSynced{false}, hostDone{false};
std::atomic<int> hostError{0};
void state(sloth::RadioScanState value,int error=0){portENTER_CRITICAL(&lock);shared.state=value;shared.error=error;++shared.generation;portEXIT_CRITICAL(&lock);}
void observe(const sloth::RadioEntry& e){portENTER_CRITICAL(&lock);sloth::radioObserve(shared,e);portEXIT_CRITICAL(&lock);}
void clearResults(){portENTER_CRITICAL(&lock);if(!targeting){shared.count=0;shared.dropped=0;++shared.generation;}portEXIT_CRITICAL(&lock);}
bool copyTarget(sloth::RadioEntry& value){portENTER_CRITICAL(&lock);const bool selected=targeting;value=target;portEXIT_CRITICAL(&lock);return selected;}
void pause(unsigned ms){for(unsigned i=0;i<ms && !cancelled.load();i+=20)vTaskDelay(pdMS_TO_TICKS(20));}
void wifiWorker(){
  // Arduino's scan-done handler consumes IDF results and allocates one record
  // per AP, even for a scan started outside WiFi.scanNetworks(). Blocking
  // scans can also produce that event. Keep the Arduino Wi-Fi layer OFF and
  // own only the raw driver; Network owns the shared event loop, not Wi-Fi.
  if(!WiFi.mode(WIFI_OFF)||!Network.begin()){state(sloth::RadioScanState::Error,ESP_FAIL);return;}
  WiFi.scanDelete();
  wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
  // Match Arduino's dynamic-buffer memory budget, rather than IDF's larger
  // default static TX pool. No interface, connection or IP stack is needed.
  init.static_tx_buf_num=0;init.dynamic_tx_buf_num=32;init.tx_buf_type=1;
  init.cache_tx_buf_num=4;init.static_rx_buf_num=4;init.dynamic_rx_buf_num=32;
  int error=esp_wifi_init(&init);
  if(error!=ESP_OK){state(sloth::RadioScanState::Error,error);return;}
  struct OffOnExit { ~OffOnExit(){esp_wifi_stop();esp_wifi_deinit();} } offOnExit;
  if((error=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK||
     (error=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK||
     (error=esp_wifi_start())!=ESP_OK||
     (error=esp_wifi_set_ps(WIFI_PS_NONE))!=ESP_OK){state(sloth::RadioScanState::Error,error);return;}
  wifi_ap_record_t records[sloth::kRadioCapacity];
  while(!cancelled.load()){
    if(refreshRequested.exchange(false))clearResults();
    sloth::RadioEntry selected;const bool focused=copyTarget(selected);
    wifi_scan_config_t config={};config.show_hidden=true;config.scan_type=WIFI_SCAN_TYPE_PASSIVE;
    config.scan_time.passive=180;config.channel=focused?selected.channel:0;
    config.bssid=focused?selected.address:nullptr;
    state(sloth::RadioScanState::Scanning);
    // Blocking scan runs on this worker. Any SCAN_DONE event has no Arduino
    // consumer, so these bounded records have exactly one owner.
    const esp_err_t error=esp_wifi_scan_start(&config,true);
    if(cancelled.load()){esp_wifi_clear_ap_list();break;}
    if(error!=ESP_OK){esp_wifi_clear_ap_list();state(sloth::RadioScanState::Error,error);pause(1000);continue;}
    uint16_t available=0;esp_wifi_scan_get_ap_num(&available);
    uint16_t count=sloth::kRadioCapacity;
    const esp_err_t result=esp_wifi_scan_get_ap_records(&count,records);
    if(result!=ESP_OK){esp_wifi_clear_ap_list();state(sloth::RadioScanState::Error,result);pause(500);continue;}
    const uint32_t seenAt=millis();
    for(unsigned i=0;i<count;++i){
      sloth::RadioEntry entry;memcpy(entry.address,records[i].bssid,6);
      sloth::radioName(entry.name,sizeof(entry.name),records[i].ssid,sizeof(records[i].ssid));
      entry.rssi=records[i].rssi;entry.channel=records[i].primary;entry.security=static_cast<uint8_t>(records[i].authmode);entry.seenAt=seenAt;observe(entry);
    }
    if(available>count){portENTER_CRITICAL(&lock);shared.dropped+=available-count;portEXIT_CRITICAL(&lock);}
    pause(focused?40:350);
  }
  esp_wifi_scan_stop();esp_wifi_clear_ap_list();
  // OffOnExit deinitializes Wi-Fi without erasing saved network configuration.
}
int gapEvent(ble_gap_event* event,void*){
  if(cancelled.load())return 0;
  if(event->type==BLE_GAP_EVENT_DISC){
    const auto& d=event->disc;sloth::RadioEntry entry;
    for(unsigned i=0;i<6;++i)entry.address[i]=d.addr.val[5-i];
    entry.addressType=d.addr.type;entry.rssi=d.rssi;entry.seenAt=millis();
    sloth::radioAdvertising(entry,d.data,d.length_data);observe(entry);
  }
#if MYNEWT_VAL(BLE_EXT_ADV)
  else if(event->type==BLE_GAP_EVENT_EXT_DISC){
    const auto& d=event->ext_disc;sloth::RadioEntry entry;
    for(unsigned i=0;i<6;++i)entry.address[i]=d.addr.val[5-i];
    entry.addressType=d.addr.type;entry.rssi=d.rssi;entry.txPower=d.tx_power;entry.seenAt=millis();
    sloth::radioAdvertising(entry,d.data,d.length_data);
    if(d.data_status!=BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE)entry.advertisingTruncated=true;
    observe(entry);
  }
#endif
  return 0;
}
void hostTask(void*){nimble_port_run();hostDone.store(true);vTaskDelete(nullptr);}
void bluetoothWorker(){
  const int initialized=nimble_port_init();
  if(initialized!=ESP_OK){state(sloth::RadioScanState::Error,initialized);return;}
  hostSynced.store(false);hostDone.store(false);hostError.store(0);
  ble_hs_cfg.sync_cb=[](){hostSynced.store(true);};
  ble_hs_cfg.reset_cb=[](int reason){hostSynced.store(false);hostError.store(reason);};
  // Discovery needs no GATT services, security manager storage, or bonding.
  ble_hs_cfg.sm_bonding=0;
  if(xTaskCreate(hostTask,"moss-ble-host",5120,nullptr,5,nullptr)!=pdPASS){nimble_port_deinit();state(sloth::RadioScanState::Error,ESP_ERR_NO_MEM);return;}
  const uint32_t started=millis();
  while(!hostSynced.load()&&!cancelled.load()&&millis()-started<5000)vTaskDelay(pdMS_TO_TICKS(20));
  if(!hostSynced.load())state(sloth::RadioScanState::Error,hostError.load()?hostError.load():ESP_ERR_TIMEOUT);
  uint8_t ownType=0;
  int error=hostSynced.load()?ble_hs_id_infer_auto(0,&ownType):BLE_HS_ENOTSYNCED;
  if(error && !cancelled.load())state(sloth::RadioScanState::Error,error);
  while(!error&&!cancelled.load()){
    if(refreshRequested.exchange(false))clearResults();
    if(!ble_gap_disc_active()){
      ble_gap_disc_params parameters={};parameters.passive=1;parameters.filter_duplicates=0;
      parameters.itvl=160;parameters.window=120; // 75ms listen /100ms, no active probes.
      error=ble_gap_disc(ownType,BLE_HS_FOREVER,&parameters,gapEvent,nullptr);
      if(error){state(sloth::RadioScanState::Error,error);break;}
      state(sloth::RadioScanState::Scanning);
    }
    if(hostError.load()){state(sloth::RadioScanState::Error,hostError.load());break;}
    vTaskDelay(pdMS_TO_TICKS(40));
  }
  ble_gap_disc_cancel();
  // Stop and deinitialize from the worker, never from the host callback/main
  // rendering task. Do not release controller memory permanently: reentry works.
  int stopped=nimble_port_stop();
  while(stopped){state(sloth::RadioScanState::Stopping,stopped);vTaskDelay(pdMS_TO_TICKS(100));stopped=nimble_port_stop();}
  while(!hostDone.load())vTaskDelay(pdMS_TO_TICKS(10));
  nimble_port_deinit();
}
void worker(void*){
  sloth::RadioKind kind;portENTER_CRITICAL(&lock);kind=shared.kind;portEXIT_CRITICAL(&lock);
  if(kind==sloth::RadioKind::Wifi)wifiWorker();else bluetoothWorker();
  if(cancelled.load())state(sloth::RadioScanState::Idle);
  running.store(false);vTaskDelete(nullptr);
}
}
bool start(sloth::RadioKind kind){
  bool expected=false;if(!running.compare_exchange_strong(expected,true))return false;
  cancelled.store(false);refreshRequested.store(false);
  portENTER_CRITICAL(&lock);shared=sloth::RadioSnapshot();shared.kind=kind;shared.state=sloth::RadioScanState::Starting;targeting=false;portEXIT_CRITICAL(&lock);
  if(xTaskCreate(worker,"moss-radio",6144,nullptr,1,nullptr)!=pdPASS){state(sloth::RadioScanState::Error,ESP_ERR_NO_MEM);running.store(false);return false;}
  return true;
}
void stop(){cancelled.store(true);if(running.load())state(sloth::RadioScanState::Stopping);}
bool busy(){return running.load();}
void copySnapshot(sloth::RadioSnapshot& out){portENTER_CRITICAL(&lock);out=shared;portEXIT_CRITICAL(&lock);}
void updateUi(sloth::RadioExplorerUi& ui,uint32_t now){portENTER_CRITICAL(&lock);ui.update(shared,now);portEXIT_CRITICAL(&lock);}
void track(const sloth::RadioEntry* entry){portENTER_CRITICAL(&lock);targeting=entry!=nullptr;if(entry)target=*entry;portEXIT_CRITICAL(&lock);}
void refresh(){if(!running.load()){sloth::RadioKind kind;portENTER_CRITICAL(&lock);kind=shared.kind;portEXIT_CRITICAL(&lock);start(kind);}else refreshRequested.store(true);}
} // namespace radio_explorer
