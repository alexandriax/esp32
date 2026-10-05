#include <esp_wifi.h>
#include <cassert>
#include <cstring>
#include <cstdio>
static wifi_init_config_t observed;static bool nullSeen;
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t*);
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config) {
 nullSeen=!config;if(config)observed=*config;return 19;
}
int main(){
 wifi_init_config_t config{};config.static_rx_buf_num=4;config.dynamic_rx_buf_num=32;
 config.dynamic_tx_buf_num=32;config.tx_buf_type=1;config.cache_tx_buf_num=4;config.rx_ba_win=6;
 const auto original=config;assert(__wrap_esp_wifi_init(&config)==19);
 assert(!memcmp(&config,&original,sizeof(config)));
 assert(observed.dynamic_rx_buf_num==8&&observed.dynamic_tx_buf_num==8);
 assert(observed.static_rx_buf_num==4&&observed.cache_tx_buf_num==4&&observed.rx_ba_win==6);
 config.dynamic_rx_buf_num=0;config.dynamic_tx_buf_num=4;
 __wrap_esp_wifi_init(&config);assert(observed.dynamic_rx_buf_num==8&&observed.dynamic_tx_buf_num==4);
 config.dynamic_rx_buf_num=2;config.tx_buf_type=0;config.dynamic_tx_buf_num=32;
 __wrap_esp_wifi_init(&config);assert(observed.dynamic_rx_buf_num==2&&observed.dynamic_tx_buf_num==32);
 assert(__wrap_esp_wifi_init(nullptr)==19&&nullSeen);
 puts("Wi-Fi burst limits: bounded dynamic packet pools, preserved caller configuration and errors passed");
}
