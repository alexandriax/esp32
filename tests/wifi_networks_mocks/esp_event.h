#pragma once
#include <stdint.h>
typedef int esp_err_t;
typedef const char* esp_event_base_t;
typedef void* esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void*,esp_event_base_t,int32_t,void*);
constexpr int ESP_OK=0;
#define WIFI_EVENT "wifi"
constexpr int WIFI_EVENT_SCAN_DONE=1;
esp_err_t esp_event_handler_instance_register(esp_event_base_t,int32_t,esp_event_handler_t,void*,esp_event_handler_instance_t*);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t,int32_t,esp_event_handler_instance_t);
