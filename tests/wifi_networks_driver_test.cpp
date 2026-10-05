#include "../firmware/sloth_pet/wifi_networks.h"
#include "../firmware/sloth_pet/wifi_network_store.h"
#include <WiFi.h>
#include <Network.h>
#include <freertos/semphr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
using namespace sloth;
namespace {
uint32_t now=0;std::string fail;std::vector<std::string> calls;
bool raw=false,station=false,registered=false;
esp_event_handler_t handler=nullptr;void* handlerContext=nullptr;
wl_status_t linkStatus=WL_IDLE_STATUS;wifi_auth_mode_t security=WIFI_AUTH_OPEN;
wifi_network_store::Credentials saved{};bool savedPresent=false;unsigned saveCalls=0;
int result(const char* call){calls.push_back(call);return fail==call?77:ESP_OK;}
void idle(){assert(!wifi_networks::busy()&&!raw&&!station&&!registered);}
void completeScan(uint32_t status=0){assert(registered&&handler);wifi_event_sta_scan_done_t event{status};handler(handlerContext,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&event);}
size_t where(const char* value){const auto it=std::find(calls.begin(),calls.end(),value);assert(it!=calls.end());return static_cast<size_t>(it-calls.begin());}
}
uint32_t millis(){return now;}
WiFiClass WiFi;NetworkClass Network;
bool WiFiClass::mode(int mode){const int r=result(mode==WIFI_OFF?"arduino-off":"arduino-sta");if(r==ESP_OK)station=mode==WIFI_STA;return r==ESP_OK;}
void WiFiClass::persistent(bool p){assert(!p);result("persistent-off");}
void WiFiClass::setAutoReconnect(bool p){assert(!p);result("reconnect-off");}
void WiFiClass::setSleep(bool p){assert(!p);}
void WiFiClass::setMinSecurity(wifi_auth_mode_t s){security=s;}
wl_status_t WiFiClass::begin(const char* ssid,const char*){assert(station&&ssid[0]);result("begin");return fail=="begin"?WL_CONNECT_FAILED:WL_IDLE_STATUS;}
wl_status_t WiFiClass::status(){return linkStatus;}
IPAddress WiFiClass::localIP(){return IPAddress{};}
int WiFiClass::RSSI(){return -45;}
bool WiFiClass::disconnect(bool off,bool erase){assert(off&&erase);result("disconnect");station=false;return true;}
bool NetworkClass::begin(){return result("network-begin")==ESP_OK;}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* s){s->held=false;return s;}
void xSemaphoreTake(SemaphoreHandle_t s,unsigned){assert(!s->held);s->held=true;}
void xSemaphoreGive(SemaphoreHandle_t s){assert(s->held);s->held=false;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t,int32_t,esp_event_handler_t cb,void* context,esp_event_handler_instance_t* out){const int r=result("register");if(r==ESP_OK){assert(!registered);handler=cb;handlerContext=context;registered=true;*out=reinterpret_cast<void*>(1);}return r;}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t,int32_t,esp_event_handler_instance_t){assert(registered);registered=false;handler=nullptr;return result("unregister");}
esp_err_t esp_wifi_init(const wifi_init_config_t* c){assert(!raw&&!station);assert(c->static_tx_buf_num==0&&c->static_rx_buf_num==4);const int r=result("init");if(r==ESP_OK)raw=true;return r;}
esp_err_t esp_wifi_set_storage(int value){assert(raw&&value==WIFI_STORAGE_RAM);return result("storage");}
esp_err_t esp_wifi_set_mode(int value){assert(raw&&value==WIFI_MODE_STA);return result("mode");}
esp_err_t esp_wifi_start(){assert(raw);return result("start");}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t* c,bool blocking){assert(raw&&registered&&!blocking&&!c->show_hidden);return result("scan-start");}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t* count,wifi_ap_record_t* out){assert(raw&&*count==wifi_networks::kMaxNetworks);const int r=result("get-records");if(r!=ESP_OK)return r;for(unsigned i=0;i<*count;++i){out[i]=wifi_ap_record_t{};snprintf(reinterpret_cast<char*>(out[i].ssid),33,"AP%u",i);out[i].rssi=-30-i;out[i].authmode=i==0?WIFI_AUTH_OPEN:i==1?WIFI_AUTH_WPA2_ENTERPRISE:WIFI_AUTH_WPA2_PSK;}return ESP_OK;}
esp_err_t esp_wifi_clear_ap_list(){return result("clear-results");}
esp_err_t esp_wifi_scan_stop(){assert(raw);return result("scan-stop");}
esp_err_t esp_wifi_stop(){assert(raw);return result("stop");}
esp_err_t esp_wifi_deinit(){assert(raw&&!registered);raw=false;return result("deinit");}
namespace sloth {namespace wifi_network_store {
Result load(Credentials& c){c=Credentials{};if(savedPresent)c=saved;return savedPresent?Result::Ok:Result::Missing;}
bool save(const Credentials& c){saved=c;savedPresent=true;++saveCalls;return true;}
bool forget(){saved=Credentials{};savedPresent=false;return true;}
void clear(Credentials& c){memset(&c,0,sizeof(c));}
}}
int main(){
  using namespace wifi_networks;
  idle();
  // Preparation never scans, joins, changes the store, or leaves the station on.
  for(const char* point:{"network-begin","arduino-sta","arduino-off"}){
    fail=point;calls.clear();assert(!prepare());idle();
    assert(std::find(calls.begin(),calls.end(),"arduino-off")!=calls.end());
    assert(std::find(calls.begin(),calls.end(),"begin")==calls.end());
    assert(std::find(calls.begin(),calls.end(),"scan-start")==calls.end());
    assert(saveCalls==0 && snapshot().state==State::Off);fail.clear();
  }
  assert(scan());calls.clear();assert(!prepare() && calls.empty());stop();idle();
  assert(connect("held","password",false));calls.clear();assert(!prepare() && calls.empty());stop();idle();
  calls.clear();assert(prepare());idle();
  assert(where("persistent-off")<where("arduino-sta") && where("reconnect-off")<where("arduino-sta"));
  assert(where("network-begin")<where("arduino-sta") && where("arduino-sta")<where("arduino-off"));
  assert(std::count(calls.begin(),calls.end(),"arduino-sta")==1 && std::count(calls.begin(),calls.end(),"arduino-off")==1);
  assert(std::find(calls.begin(),calls.end(),"begin")==calls.end());
  assert(std::find(calls.begin(),calls.end(),"scan-start")==calls.end());
  assert(saveCalls==0 && !savedPresent);
  calls.clear();assert(prepare() && calls.empty());idle();
  // Busy refusal precedes the idempotent-success cache too.
  assert(scan());calls.clear();assert(!prepare() && calls.empty());stop();idle();
  // Each partially initialized raw scan must unwind what it owns before return.
  for(const char* point:{"arduino-off","network-begin","init","register","storage","mode","start","scan-start"}){
    fail=point;calls.clear();assert(!scan());idle();assert(snapshot().state==State::Failed);fail.clear();
  }
  calls.clear();assert(scan());assert(raw&&registered&&busy());completeScan();tick();idle();
  const auto s=snapshot();assert(s.state==State::Ready&&s.count==12&&!s.networks[0].secured&&!s.networks[1].supported&&s.networks[2].supported);
  assert(where("scan-stop")<where("unregister")&&where("unregister")<where("stop")&&where("stop")<where("deinit"));
  calls.clear();assert(scan());cancel();idle();assert(where("unregister")<where("deinit"));
  assert(scan());now=15000;tick();idle();assert(snapshot().error==Error::Timeout);
  assert(scan());completeScan(1);tick();idle();assert(snapshot().error==Error::Scan);
  assert(scan());fail="get-records";completeScan();tick();idle();fail.clear();assert(snapshot().error==Error::Scan);
  for(const char* point:{"network-begin","arduino-sta","begin"}){fail=point;assert(!connect("network","password"));idle();fail.clear();}
  assert(connect("open",""));assert(security==WIFI_AUTH_OPEN&&station&&busy());linkStatus=WL_CONNECTED;tick();assert(snapshot().state==State::Connected&&saveCalls==1);stop();idle();
  assert(connect("secure","password"));assert(security==WIFI_AUTH_WPA2_PSK);linkStatus=WL_CONNECT_FAILED;tick();idle();assert(saveCalls==1&&!strcmp(saved.ssid,"open"));
  linkStatus=WL_IDLE_STATUS;assert(connectSaved());cancel();idle();
  assert(scan());assert(connect("secure","password",false));assert(!raw&&!registered&&station);linkStatus=WL_CONNECTED;tick();assert(saveCalls==1);stop();idle();
  char name[4];assert(savedSsid(name,sizeof(name))&&!strcmp(name,"ope"));assert(forgetSaved());assert(!hasSaved());idle();
  puts("wifi_networks driver: real adapter compiled; idempotent preparation, busy refusal, partial init, async scan, bounded retained records and ordered cleanup passed");
}
