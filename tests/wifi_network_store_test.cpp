#include "../firmware/sloth_pet/wifi_network_store.h"
#include "../firmware/sloth_pet/paired_wifi_store.h"
#include <nvs.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
using namespace sloth;
namespace {std::map<std::string,std::vector<uint8_t>> disk,pending;bool opened=false;int fault=0;unsigned writes=0;}
esp_err_t nvs_open_from_partition(const char* part,const char* ns,int mode,nvs_handle_t* h){assert(!strcmp(part,"nvs")&&!strcmp(ns,"moss-wifi")&&!opened);if(fault==1)return 77;if(mode==NVS_READONLY&&disk.empty())return ESP_ERR_NVS_NOT_FOUND;opened=true;pending=disk;*h=7;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* key,void* out,size_t* len){assert(opened&&h==7);if(fault==2)return 77;const auto found=disk.find(key);if(found==disk.end())return ESP_ERR_NVS_NOT_FOUND;if(!out){*len=found->second.size();return ESP_OK;}assert(*len>=found->second.size());memcpy(out,found->second.data(),found->second.size());*len=found->second.size();return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* key,const void* data,size_t length){assert(opened&&h==7);++writes;if(fault==3)return 77;const uint8_t* p=static_cast<const uint8_t*>(data);pending[key]=std::vector<uint8_t>(p,p+length);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(opened&&h==7);if(fault==4)return 77;disk=pending;return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char* key){assert(opened&&h==7);return pending.erase(key)?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
void nvs_close(nvs_handle_t h){assert(opened&&h==7);opened=false;pending.clear();}
bool zero(const wifi_network_store::Credentials& c){const unsigned char* p=reinterpret_cast<const unsigned char*>(&c);for(size_t i=0;i<sizeof(c);++i)if(p[i])return false;return true;}
int main(){
  using wifi_network_store::Credentials;using wifi_network_store::Result;
  Credentials out{};assert(wifi_network_store::load(out)==Result::Missing&&zero(out));
  paired_wifi_store::Profile old{};strcpy(old.ssid,"legacy-network");strcpy(old.password,"legacy-password");memset(old.deviceId,'a',32);memset(old.token,'b',64);old.keyLength=2;old.key[0]=1;old.key[1]=2;old.certificateLength=3;old.certificate[0]=3;
  assert(paired_wifi_store::save(old));const auto identity=disk["profile"];const unsigned originalWrites=writes;
  assert(wifi_network_store::load(out)==Result::Ok&&!strcmp(out.ssid,"legacy-network")&&writes==originalWrites);
  Credentials next{};strcpy(next.ssid,"new-network");strcpy(next.password,"new-password");assert(wifi_network_store::save(next));
  assert(disk["network"].size()==114&&disk["profile"]==identity);const auto valid=disk["network"];
  assert(wifi_network_store::load(out)==Result::Ok&&!memcmp(&out,&next,sizeof(next)));
  // Every single-byte corruption must fail closed; legacy credentials remain
  // present specifically to catch an accidental fallback on an invalid record.
  for(size_t i=0;i<valid.size();++i){disk["network"]=valid;disk["network"][i]^=0x80;memset(&out,0xa5,sizeof(out));assert(wifi_network_store::load(out)==Result::Invalid&&zero(out));}
  disk["network"]=valid;disk["network"].pop_back();assert(wifi_network_store::load(out)==Result::Invalid&&zero(out));
  disk["network"]=valid;disk["network"].push_back(0);assert(wifi_network_store::load(out)==Result::Invalid&&zero(out));disk["network"]=valid;
  for(int error=1;error<=4;++error){fault=error;if(error<=2){memset(&out,0xa5,sizeof(out));assert(wifi_network_store::load(out)==Result::StorageError&&zero(out));}if(error!=2){assert(!wifi_network_store::save(next));assert(!wifi_network_store::forget());}fault=0;assert(disk["network"]==valid&&disk["profile"]==identity);}
  assert(wifi_network_store::forget());assert(disk["profile"]==identity);assert(wifi_network_store::load(out)==Result::Missing&&zero(out));
  const auto tombstone=disk["network"];assert(tombstone.size()==114&&tombstone[8]==0);
  // A reboot/new load cannot resurrect legacy SSID, including corrupt markers.
  for(size_t i=0;i<tombstone.size();++i){disk["network"]=tombstone;disk["network"][i]^=1;assert(wifi_network_store::load(out)==Result::Invalid&&zero(out));}
  disk["network"]=tombstone;assert(wifi_network_store::load(out)==Result::Missing);
  // A new Settings-only network needs no pairing identity at all.
  disk.clear();strcpy(next.ssid,"open");memset(next.password,0,sizeof(next.password));assert(wifi_network_store::save(next));assert(disk.find("profile")==disk.end());assert(wifi_network_store::load(out)==Result::Ok&&out.password[0]==0);
  next.ssid[0]=0;assert(!wifi_network_store::save(next));wifi_network_store::clear(out);assert(zero(out));
  puts("wifi_network_store: legacy reads, canonical migration, identity preservation, tombstones and storage faults passed");
}
