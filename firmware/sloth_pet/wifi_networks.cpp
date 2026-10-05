#include "wifi_networks.h"
#include "wifi_networks_core.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Network.h>
#include <atomic>
#include <string.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sloth { namespace wifi_networks {
namespace {
class EspDriver : public detail::Driver {
 public:
  bool prepare() {
    if(prepared_)return true;
    const bool started=startStation();
    // No join happened. Avoid disconnect(erase=true), which requires the
    // asynchronous STA START event; mode(OFF) also unwinds partial startup.
    const bool stopped=stopStation(false);
    if(!stopped)stop(); // Best-effort cleanup even when the first OFF failed.
    prepared_=started && stopped;
    return prepared_;
  }
  bool scanStart() override {
    // Keep Arduino's Wi-Fi scan handler absent: it allocates one result per AP.
    // The raw scanner retains only kMaxNetworks records and releases the SDK
    // list on every completion, cancellation, timeout and initialization error.
    if(!WiFi.mode(WIFI_OFF)||!Network.begin())return false;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    config.static_tx_buf_num=0;config.dynamic_tx_buf_num=32;config.tx_buf_type=1;
    config.cache_tx_buf_num=4;config.static_rx_buf_num=4;config.dynamic_rx_buf_num=32;
    if(esp_wifi_init(&config)!=ESP_OK)return false;
    raw_=true;scanDone_.store(0);count_=0;
    if(esp_event_handler_instance_register(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,onScan,this,&handler_)!=ESP_OK)return false;
    if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||
       esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK||esp_wifi_start()!=ESP_OK)return false;
    wifi_scan_config_t scan{};
    scan.show_hidden=false;scan.scan_type=WIFI_SCAN_TYPE_ACTIVE;
    scan.scan_time.active.min=60;scan.scan_time.active.max=120;
    return esp_wifi_scan_start(&scan,false)==ESP_OK;
  }
  int scanResult() override {
    const int done=scanDone_.load();
    if(!done)return -1;
    if(done<0)return -2;
    if(!collected_){
      uint16_t count=kMaxNetworks;
      wifi_ap_record_t records[kMaxNetworks]{};
      if(esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK){esp_wifi_clear_ap_list();return -2;}
      // IDF returns records ordered by signal strength. Our retained copy is
      // bounded; the SDK temporarily holds its own complete scan result list.
      for(unsigned i=0;i<count;++i){
        AccessPoint& ap=aps_[i];ap=AccessPoint{};
        memcpy(ap.ssid,records[i].ssid,32);ap.ssid[32]=0;ap.rssi=records[i].rssi;
        const wifi_auth_mode_t auth=records[i].authmode;
        ap.secured=auth!=WIFI_AUTH_OPEN;
        ap.supported=auth==WIFI_AUTH_OPEN||auth==WIFI_AUTH_WPA2_PSK||
                     auth==WIFI_AUTH_WPA_WPA2_PSK||auth==WIFI_AUTH_WPA2_WPA3_PSK;
      }
      count_=count;collected_=true;esp_wifi_clear_ap_list();
    }
    return count_;
  }
  bool accessPoint(unsigned index,AccessPoint& ap) override {
    if(index>=count_)return false;
    ap=aps_[index];return true;
  }
  bool join(const detail::Credentials& c) override {
    if(!startStation())return false;
    WiFi.setSleep(false);
    // Explicit open auth permits open APs; credentials require WPA2 or stronger.
    WiFi.setMinSecurity(c.password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN);
    return WiFi.begin(c.ssid,c.password)==WL_CONNECT_FAILED?false:true;
  }
  detail::Link link() override {
    const wl_status_t state=WiFi.status();
    if(state==WL_CONNECTED)return detail::Link::Connected;
    if(state==WL_CONNECT_FAILED||state==WL_NO_SSID_AVAIL)return detail::Link::Failed;
    return detail::Link::Joining;
  }
  void connection(char address[16],int16_t& rssi) override {
    const IPAddress ip=WiFi.localIP();
    snprintf(address,16,"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);rssi=WiFi.RSSI();
  }
  void stop() override {
    if(raw_){
      esp_wifi_scan_stop();
      if(handler_){esp_event_handler_instance_unregister(WIFI_EVENT,WIFI_EVENT_SCAN_DONE,handler_);handler_=nullptr;}
      esp_wifi_clear_ap_list();esp_wifi_stop();esp_wifi_deinit();raw_=false;
    }
    if(station_)stopStation(true);
    scanDone_.store(0);collected_=false;count_=0;memset(aps_,0,sizeof(aps_));
  }
 private:
  bool startStation() {
    WiFi.persistent(false);WiFi.setAutoReconnect(false);
    if(!Network.begin())return false;
    station_=true; // Partial mode initialization must also be unwound.
    return WiFi.mode(WIFI_STA);
  }
  bool stopStation(bool disconnect) {
    if(disconnect)WiFi.disconnect(true,true);
    const bool stopped=WiFi.mode(WIFI_OFF);
    station_=!stopped;
    return stopped;
  }
  static void onScan(void* context,esp_event_base_t,int32_t,void* data){
    const wifi_event_sta_scan_done_t* event=static_cast<const wifi_event_sta_scan_done_t*>(data);
    static_cast<EspDriver*>(context)->scanDone_.store(event&&event->status==0?1:-1);
  }
  bool raw_=false,station_=false,collected_=false,prepared_=false;
  esp_event_handler_instance_t handler_=nullptr;
  std::atomic<int> scanDone_{0};
  uint16_t count_=0;
  AccessPoint aps_[kMaxNetworks]{};
};
class NvsStore : public detail::Store {
 public:
  detail::Result load(detail::Credentials& c) override{return wifi_network_store::load(c);}
  bool save(const detail::Credentials& c) override{return wifi_network_store::save(c);}
  bool forget() override{return wifi_network_store::forget();}
};
EspDriver driver;
NvsStore storage;
detail::Controller controller(driver,storage);
StaticSemaphore_t mutexStorage;
// Created before either consumer starts; the service never adds a worker stack.
SemaphoreHandle_t mutex=xSemaphoreCreateMutexStatic(&mutexStorage);
class Guard {
 public:
  Guard(){xSemaphoreTake(mutex,portMAX_DELAY);}
  ~Guard(){xSemaphoreGive(mutex);}
};
}
bool prepare(){Guard guard;return !controller.busy() && driver.prepare();}
bool scan(){Guard guard;return controller.scan(millis());}
bool connect(const char* ssid,const char* password,bool persist){Guard guard;return controller.connect(ssid,password,persist,millis());}
bool connectSaved(){Guard guard;return controller.connectSaved(millis());}
void tick(){Guard guard;controller.tick(millis());}
void cancel(){stop();}
void stop(){Guard guard;controller.stop();}
bool busy(){Guard guard;return controller.busy();}
bool connected(){Guard guard;return controller.connected();}
Snapshot snapshot(){Guard guard;return controller.snapshot();}
bool hasSaved(){char ssid[33];return savedSsid(ssid,sizeof(ssid));}
bool savedSsid(char* output,size_t capacity){
  if(!output||!capacity)return false;
  Guard guard;output[0]=0;detail::Credentials c{};
  const bool saved=wifi_network_store::load(c)==detail::Result::Ok;
  if(saved){strncpy(output,c.ssid,capacity-1);output[capacity-1]=0;}
  wifi_network_store::clear(c);return saved;
}
bool forgetSaved(){Guard guard;return controller.forgetSaved();}
} } // namespace sloth::wifi_networks
