#pragma once
#include "esp_event.h"
#include <stdint.h>
enum wifi_auth_mode_t { WIFI_AUTH_OPEN,WIFI_AUTH_WEP,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,WIFI_AUTH_WPA2_ENTERPRISE,WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK };
struct wifi_init_config_t { int static_tx_buf_num,dynamic_tx_buf_num,tx_buf_type,cache_tx_buf_num,static_rx_buf_num,dynamic_rx_buf_num,rx_ba_win; };
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
constexpr int WIFI_STORAGE_RAM=1,WIFI_MODE_STA=1,WIFI_SCAN_TYPE_ACTIVE=0;
struct wifi_scan_config_t { bool show_hidden;int scan_type;struct {struct {unsigned min,max;} active;} scan_time; };
struct wifi_ap_record_t { uint8_t ssid[33];int8_t rssi;wifi_auth_mode_t authmode; };
struct wifi_event_sta_scan_done_t { uint32_t status; };
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*,bool);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*,wifi_ap_record_t*);
esp_err_t esp_wifi_clear_ap_list();
esp_err_t esp_wifi_scan_stop();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_deinit();
