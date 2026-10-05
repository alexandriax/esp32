#include "wifi_network_store.h"
#include "paired_wifi_store.h"
#include <stdint.h>
#include <string.h>
#include <nvs.h>
#include <mbedtls/platform_util.h>
namespace sloth { namespace wifi_network_store {
namespace {
constexpr char kNamespace[] = "moss-wifi", kKey[] = "network";
constexpr size_t kBytes = 114;
constexpr uint32_t kMagic = 0x4e57534d; // MSWN little endian.
void put32(uint8_t* p, uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=n>>(i*8); }
uint32_t get32(const uint8_t* p) { uint32_t n=0; for(unsigned i=0;i<4;++i)n|=uint32_t(p[i])<<(i*8); return n; }
uint32_t crc(const uint8_t* p,size_t n) {
  uint32_t c=0xffffffffu;
  for(size_t i=0;i<n;++i){c^=p[i];for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
  return ~c;
}
bool valid(const Credentials& c) {
  return c.ssid[0] && strnlen(c.ssid,sizeof(c.ssid))<sizeof(c.ssid) &&
         strnlen(c.password,sizeof(c.password))<sizeof(c.password);
}
bool write(const Credentials* c) {
  uint8_t bytes[kBytes]{};
  put32(bytes,kMagic); bytes[4]=1; bytes[6]=kBytes; bytes[8]=c?1:0;
  if(c){memcpy(bytes+12,c->ssid,strlen(c->ssid));memcpy(bytes+45,c->password,strlen(c->password));}
  put32(bytes+kBytes-4,crc(bytes,kBytes-4));
  nvs_handle_t handle;
  esp_err_t result=nvs_open_from_partition("nvs",kNamespace,NVS_READWRITE,&handle);
  if(result==ESP_OK){result=nvs_set_blob(handle,kKey,bytes,sizeof(bytes));if(result==ESP_OK)result=nvs_commit(handle);nvs_close(handle);}
  mbedtls_platform_zeroize(bytes,sizeof(bytes));
  return result==ESP_OK;
}
Result legacy(Credentials& c) {
  paired_wifi_store::Profile old{};
  const auto result=paired_wifi_store::load(old);
  if(result==paired_wifi_store::Result::Ok){memcpy(c.ssid,old.ssid,sizeof(c.ssid));memcpy(c.password,old.password,sizeof(c.password));}
  paired_wifi_store::clear(old);
  switch(result){
    case paired_wifi_store::Result::Ok:return Result::Ok;
    case paired_wifi_store::Result::Missing:return Result::Missing;
    case paired_wifi_store::Result::Invalid:return Result::Invalid;
    default:return Result::StorageError;
  }
}
}
void clear(Credentials& c){mbedtls_platform_zeroize(&c,sizeof(c));}
Result load(Credentials& c) {
  clear(c);
  nvs_handle_t handle;
  esp_err_t result=nvs_open_from_partition("nvs",kNamespace,NVS_READONLY,&handle);
  if(result==ESP_ERR_NVS_NOT_FOUND)return legacy(c);
  if(result!=ESP_OK)return Result::StorageError;
  size_t length=0;
  result=nvs_get_blob(handle,kKey,nullptr,&length);
  if(result!=ESP_OK||length!=kBytes){nvs_close(handle);return result==ESP_ERR_NVS_NOT_FOUND?legacy(c):result==ESP_OK?Result::Invalid:Result::StorageError;}
  uint8_t bytes[kBytes]{};
  result=nvs_get_blob(handle,kKey,bytes,&length);nvs_close(handle);
  bool accepted=result==ESP_OK && length==kBytes && get32(bytes)==kMagic &&
      bytes[4]==1 && bytes[5]==0 && bytes[6]==kBytes && bytes[7]==0 &&
      bytes[8]<=1 && bytes[9]==0 && bytes[10]==0 && bytes[11]==0 &&
      get32(bytes+kBytes-4)==crc(bytes,kBytes-4);
  const bool present=bytes[8]==1;
  if(accepted && present){memcpy(c.ssid,bytes+12,sizeof(c.ssid));memcpy(c.password,bytes+45,sizeof(c.password));accepted=valid(c);}
  if(accepted && !present){for(size_t i=12;i<kBytes-4;++i)if(bytes[i])accepted=false;}
  mbedtls_platform_zeroize(bytes,sizeof(bytes));
  if(!accepted)clear(c);
  return !accepted?(result==ESP_OK?Result::Invalid:Result::StorageError):present?Result::Ok:Result::Missing;
}
bool save(const Credentials& c){return valid(c)&&write(&c);}
bool forget(){return write(nullptr);}
} }
