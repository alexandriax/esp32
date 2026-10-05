#pragma once
constexpr int WIFI_OFF=0,WIFI_STA=1;
struct MockWifi {void persistent(bool);void setAutoReconnect(bool);bool mode(int);void setSleep(bool);void scanDelete();};
extern MockWifi WiFi;
