#pragma once
#include <stdint.h>
using esp_err_t=int;
constexpr int WIFI_SCAN_TYPE_PASSIVE=1;
constexpr int WIFI_STORAGE_RAM=1,WIFI_MODE_STA=1,WIFI_PS_NONE=0;
struct wifi_init_config_t {int static_tx_buf_num=16,dynamic_tx_buf_num=32,tx_buf_type=0,cache_tx_buf_num=32,static_rx_buf_num=10,dynamic_rx_buf_num=32;};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t()
int esp_wifi_init(const wifi_init_config_t*);
int esp_wifi_deinit();
int esp_wifi_set_storage(int);
int esp_wifi_set_mode(int);
int esp_wifi_start();
int esp_wifi_stop();
int esp_wifi_set_ps(int);
struct wifi_scan_config_t {uint8_t* ssid=nullptr;uint8_t*bssid=nullptr;uint8_t channel=0;bool show_hidden=false;int scan_type=0;struct{unsigned passive=0;}scan_time;};
struct wifi_ap_record_t {uint8_t bssid[6]={},ssid[33]={},primary=0;int8_t rssi=0;unsigned authmode=0;};
int esp_wifi_scan_start(const wifi_scan_config_t*,bool);
int esp_wifi_clear_ap_list();
int esp_wifi_scan_get_ap_num(uint16_t*);
int esp_wifi_scan_get_ap_records(uint16_t*,wifi_ap_record_t*);
int esp_wifi_scan_stop();
