#include "../firmware/sloth_pet/radio_explorer.h"
#include "radio_explorer_mocks/Arduino.h"
#include "radio_explorer_mocks/WiFi.h"
#include "radio_explorer_mocks/Network.h"
#include "radio_explorer_mocks/esp_wifi.h"
#include "radio_explorer_mocks/host/ble_hs.h"
#include "radio_explorer_mocks/host/ble_gap.h"
#include <atomic>
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <vector>
namespace {
std::mutex tasksMutex,hostMutex;std::condition_variable hostCv;std::vector<std::thread> tasks;
std::atomic<bool> failTask{false},failWifiMode{false},failBleInit{false},failBleTask{false};
std::atomic<bool> failWifiInit{false},failWifiStart{false},rawWifi{false},rawStarted{false},arduinoScanHandler{false};
std::atomic<unsigned> wifiDeinits{0},scanDeletes{0},consumedByArduino{0};
std::atomic<int> wifiError{0},bleScanError{0},wifiMode{0},scans{0},stops{0},clears{0},deinits{0},bleStarts{0};
std::atomic<unsigned> lastChannel{0};std::atomic<bool> scanning{false};bool hostStop=false;
auto epoch=std::chrono::steady_clock::now();
void waitFor(bool(*condition)()){for(unsigned n=0;n<2000&&!condition();++n)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(condition());}
bool stopped(){return !radio_explorer::busy();}
bool gotWifi(){return scans.load()>0;}
bool gotBle(){return bleStarts.load()>0;}
void joinTasks(){std::vector<std::thread> pending;{std::lock_guard<std::mutex> guard(tasksMutex);pending.swap(tasks);}for(auto& task:pending)task.join();}
}
MockWifi WiFi;MockNetwork Network;ble_hs_cfg_t ble_hs_cfg;
namespace {uint32_t allocationSize=0,allocationCount=0;bool failAllocation=false;}
extern "C" int __wrap_r_ble_ll_mem_generic_data_init(void**,uint32_t,uint32_t);
extern "C" int __real_r_ble_ll_mem_generic_data_init(void** out,uint32_t size,uint32_t count){
 allocationSize=size;allocationCount=count;*out=failAllocation?nullptr:calloc(size,count);return *out?0:-1;
}
uint32_t millis(){return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-epoch).count());}
void vTaskDelay(unsigned){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
void vTaskDelete(void*){}
int xTaskCreate(void(*fn)(void*),const char* name,unsigned stack,void*arg,unsigned,void*){
 assert(stack<=6144);if(failTask.exchange(false)||(strstr(name,"host")&&failBleTask.exchange(false)))return 0;
 std::lock_guard<std::mutex> guard(tasksMutex);tasks.emplace_back([=](){fn(arg);});return pdPASS;
}
void MockWifi::persistent(bool value){assert(!value);}void MockWifi::setAutoReconnect(bool value){assert(!value);}void MockWifi::setSleep(bool value){assert(!value);}
bool MockWifi::mode(int value){assert(!rawWifi);wifiMode=value;arduinoScanHandler=value!=WIFI_OFF;return true;}
void MockWifi::scanDelete(){assert(!arduinoScanHandler);++scanDeletes;}
bool MockNetwork::begin(){return true;}
int esp_wifi_init(const wifi_init_config_t* config){assert(!arduinoScanHandler&&!rawWifi);assert(config->static_tx_buf_num==0&&config->tx_buf_type==1&&config->static_rx_buf_num==4);if(failWifiInit)return -3;rawWifi=true;return 0;}
int esp_wifi_deinit(){assert(rawWifi&&!rawStarted);rawWifi=false;++wifiDeinits;return 0;}
int esp_wifi_set_storage(int value){assert(rawWifi&&value==WIFI_STORAGE_RAM);return 0;}
int esp_wifi_set_mode(int value){assert(rawWifi&&value==WIFI_MODE_STA);return failWifiMode?-4:0;}
int esp_wifi_start(){assert(rawWifi);if(failWifiStart)return -5;rawStarted=true;return 0;}
int esp_wifi_stop(){assert(rawWifi);rawStarted=false;return 0;}
int esp_wifi_set_ps(int value){assert(rawStarted&&value==WIFI_PS_NONE);return 0;}
int esp_wifi_scan_start(const wifi_scan_config_t* config,bool block){assert(rawStarted&&block&&config->scan_type==WIFI_SCAN_TYPE_PASSIVE&&config->scan_time.passive==180);lastChannel=config->channel;++scans;vTaskDelay(1);if(arduinoScanHandler)++consumedByArduino;return wifiError;}
int esp_wifi_clear_ap_list(){++clears;return 0;}int esp_wifi_scan_stop(){++stops;return 0;}
int esp_wifi_scan_get_ap_num(uint16_t* count){assert(rawStarted);*count=arduinoScanHandler?0:2;return 0;}
int esp_wifi_scan_get_ap_records(uint16_t* count,wifi_ap_record_t* entries){assert(rawStarted&&*count==24);*count=arduinoScanHandler?0:2;for(unsigned i=0;i<*count;++i){entries[i]=wifi_ap_record_t();entries[i].bssid[5]=i;entries[i].primary=6;entries[i].rssi=-50-i;memcpy(entries[i].ssid,"MOSS",5);}return 0;}
int nimble_port_init(){if(failBleInit)return -1;{std::lock_guard<std::mutex> guard(hostMutex);hostStop=false;}return 0;}
int nimble_port_deinit(){++deinits;return 0;}
void nimble_port_run(){ble_hs_cfg.sync_cb();std::unique_lock<std::mutex> guard(hostMutex);hostCv.wait(guard,[]{return hostStop;});}
int nimble_port_stop(){{std::lock_guard<std::mutex> guard(hostMutex);hostStop=true;}hostCv.notify_all();return 0;}
int ble_hs_id_infer_auto(int privacy,uint8_t* type){assert(!privacy);*type=0;return 0;}
bool ble_gap_disc_active(){return scanning;}
int ble_gap_disc(uint8_t,int32_t duration,const ble_gap_disc_params* p,int(*callback)(ble_gap_event*,void*),void*arg){
 assert(duration==BLE_HS_FOREVER&&p->passive==1&&!p->filter_duplicates&&p->window<=p->itvl);
 if(bleScanError)return bleScanError;
 scanning=true;ble_gap_event event;event.type=BLE_GAP_EVENT_DISC;event.disc.addr.val[0]=0xab;event.disc.rssi=-42;
 const uint8_t adv[]={5,9,'M','O','S','S'};event.disc.data=adv;event.disc.length_data=sizeof(adv);callback(&event,arg);++bleStarts;return 0;
}
int ble_gap_disc_cancel(){scanning=false;return 0;}
int main(){
 using namespace sloth;RadioSnapshot snapshot;
 // Reproduce the controller's exact3-byte prefix +32-bit pointer layout.
 // ASan rejects this offset3 write without the missing4 bytes of reservation.
 void* allocation=nullptr;assert(__wrap_r_ble_ll_mem_generic_data_init(&allocation,3,1)==0);
 assert(allocationSize==7&&allocationCount==1);
 const uint32_t controllerPointer=0x4086c610;memcpy(static_cast<uint8_t*>(allocation)+3,&controllerPointer,4);
 assert(static_cast<uint8_t*>(allocation)[0]==0);free(allocation);
 assert(__wrap_r_ble_ll_mem_generic_data_init(&allocation,3,2)==0);assert(allocationSize==3&&allocationCount==2);free(allocation);
 assert(__wrap_r_ble_ll_mem_generic_data_init(&allocation,128,1)==0);assert(allocationSize==128&&allocationCount==1);free(allocation);
 failAllocation=true;assert(__wrap_r_ble_ll_mem_generic_data_init(&allocation,3,1)==-1&&!allocation);failAllocation=false;
 failTask=true;assert(!radio_explorer::start(RadioKind::Wifi));radio_explorer::copySnapshot(snapshot);assert(snapshot.state==RadioScanState::Error&&!radio_explorer::busy());
 failWifiInit=true;assert(radio_explorer::start(RadioKind::Wifi));waitFor(stopped);joinTasks();assert(!rawWifi&&wifiDeinits==0);failWifiInit=false;
 failWifiMode=true;assert(radio_explorer::start(RadioKind::Wifi));waitFor(stopped);joinTasks();assert(wifiMode==WIFI_OFF);failWifiMode=false;
 assert(!rawWifi&&!rawStarted&&wifiDeinits==1);
 failWifiStart=true;assert(radio_explorer::start(RadioKind::Wifi));waitFor(stopped);joinTasks();assert(!rawWifi&&!rawStarted&&wifiDeinits==2);failWifiStart=false;
 assert(radio_explorer::start(RadioKind::Wifi));assert(!radio_explorer::start(RadioKind::Bluetooth));waitFor(gotWifi);
 for(unsigned i=0;i<100;++i){radio_explorer::copySnapshot(snapshot);if(snapshot.count)break;vTaskDelay(1);}assert(snapshot.count==2&&!strcmp(snapshot.entries[0].name,"MOSS"));
 RadioExplorerUi ui;ui.open(RadioKind::Wifi);radio_explorer::updateUi(ui,millis());assert(ui.snapshot().count==2);
 assert(ui.activate(0)==RadioEvent::SelectionChanged);radio_explorer::updateUi(ui,millis());assert(ui.selected()&&ui.selected()->address[5]==snapshot.entries[0].address[5]);
 radio_explorer::track(&snapshot.entries[0]);for(unsigned i=0;i<100&&lastChannel!=6;++i)vTaskDelay(1);assert(lastChannel==6);
 radio_explorer::refresh();vTaskDelay(1);radio_explorer::copySnapshot(snapshot);assert(snapshot.count); // Selected detail survives refresh.
 radio_explorer::stop();waitFor(stopped);joinTasks();assert(wifiMode==WIFI_OFF&&stops>0&&clears>0);
 assert(!rawWifi&&!rawStarted&&!arduinoScanHandler&&!consumedByArduino&&scanDeletes==4);
 wifiError=-7;scans=0;assert(radio_explorer::start(RadioKind::Wifi));waitFor(gotWifi);vTaskDelay(1);radio_explorer::stop();waitFor(stopped);joinTasks();assert(wifiMode==WIFI_OFF);wifiError=0;
 failBleInit=true;assert(radio_explorer::start(RadioKind::Bluetooth));waitFor(stopped);joinTasks();radio_explorer::copySnapshot(snapshot);assert(snapshot.state==RadioScanState::Error);failBleInit=false;
 failBleTask=true;assert(radio_explorer::start(RadioKind::Bluetooth));waitFor(stopped);joinTasks();assert(deinits==1);
 for(unsigned repeat=0;repeat<3;++repeat){bleStarts=0;assert(radio_explorer::start(RadioKind::Bluetooth));waitFor(gotBle);radio_explorer::copySnapshot(snapshot);assert(snapshot.count==1&&snapshot.entries[0].address[5]==0xab&&!strcmp(snapshot.entries[0].name,"MOSS"));radio_explorer::stop();waitFor(stopped);joinTasks();assert(!scanning);}
 bleScanError=11;assert(radio_explorer::start(RadioKind::Bluetooth));waitFor(stopped);joinTasks();radio_explorer::copySnapshot(snapshot);assert(snapshot.state==RadioScanState::Error);assert(deinits==5);
 puts("radio_explorer_driver: BLE allocation overrun regression, raw passive Wi-Fi ownership, failure cleanup, target/refresh, cancellation and BLE reentry passed");
}
