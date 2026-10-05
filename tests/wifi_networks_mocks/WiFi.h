#pragma once
#include "esp_wifi.h"
enum wl_status_t { WL_IDLE_STATUS,WL_NO_SSID_AVAIL,WL_CONNECTED,WL_CONNECT_FAILED,WL_DISCONNECTED };
constexpr int WIFI_OFF=0,WIFI_STA=1;
struct IPAddress { unsigned operator[](unsigned i)const { const unsigned a[]={192,0,2,10};return a[i];} };
struct WiFiClass {
 bool mode(int);
 void persistent(bool);
 void setAutoReconnect(bool);
 void setSleep(bool);
 void setMinSecurity(wifi_auth_mode_t);
 wl_status_t begin(const char*,const char*);
 wl_status_t status();
 IPAddress localIP();
 int RSSI();
 bool disconnect(bool,bool);
};
extern WiFiClass WiFi;
