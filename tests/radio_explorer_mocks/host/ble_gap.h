#pragma once
#include <stdint.h>
constexpr int BLE_GAP_EVENT_DISC=1;
struct ble_addr_t {uint8_t type=0,val[6]={};};
struct ble_gap_disc_desc {ble_addr_t addr;int8_t rssi=0;const uint8_t* data=nullptr;uint8_t length_data=0;};
struct ble_gap_event {int type=0;ble_gap_disc_desc disc;};
struct ble_gap_disc_params {int passive=0,filter_duplicates=0;uint16_t itvl=0,window=0;};
bool ble_gap_disc_active();
int ble_gap_disc(uint8_t,int32_t,const ble_gap_disc_params*,int(*)(ble_gap_event*,void*),void*);
int ble_gap_disc_cancel();
