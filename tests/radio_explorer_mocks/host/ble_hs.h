#pragma once
#define MYNEWT_VAL(x) 0
constexpr int BLE_HS_ENOTSYNCED=22,BLE_HS_FOREVER=-1;
struct ble_hs_cfg_t {void(*sync_cb)()=nullptr;void(*reset_cb)(int)=nullptr;int sm_bonding=0;};
extern ble_hs_cfg_t ble_hs_cfg;
