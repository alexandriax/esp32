#include "storage_mocks/storage_mock.h"
#include "storage_mocks/driver/gpio.h"
#include "../firmware/sloth_pet/storage_status.h"
#include "../firmware/sloth_pet/storage_files.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
struct Mock {
  esp_err_t flashError=ESP_OK, imageError=ESP_OK, nvsError=ESP_OK;
  esp_err_t initError=ESP_OK, attachError=ESP_OK, cardError=ESP_OK;
  esp_err_t driveError=ESP_OK, transactionError=ESP_OK;
  FRESULT mountError=FR_OK, freeError=FR_OK;
  bool cache=false;
  bool appPresent=true, partitionPresent=true, recursive=false;
  bool timeoutInit=false, timeoutScan=false;
  uint32_t flashSize=16*1024*1024, imageSize=1200000;
  uint32_t capacity=64000000, entries=1000002, freeClusters=250000;
  uint16_t clusterSize=32;
  size_t nvsUsed=30, nvsTotal=2016;
  int64_t now=0;
  int attached=0, removed=0, registered=0, unmounted=0;
  int transactions=0, reads=0, callbacks=0, onboardCalls=0, mounts=0;
  int csHigh=0, csOutput=0;
  bool callbackSawAttached=false;
  FATFS* fs=nullptr;
  ff_diskio_impl_t io={};
} mock;
esp_partition_t app, partition;
void reset() {
  mock=Mock{};
  app.address=0x10000; app.size=0x300000;
  partition.address=0xfe0000; partition.size=0x10000;
}
void initialized() {
  ++mock.callbacks;
  mock.callbackSawAttached=mock.attached > mock.removed;
}
void clean() {
  assert(mock.attached==mock.removed);
  assert(mock.registered==0);
  assert(mock.fs==nullptr);
  assert(mock.csHigh==1 && mock.csOutput==1);
}
}

esp_err_t esp_flash_get_physical_size(void*,uint32_t* size) {
  ++mock.onboardCalls; *size=mock.flashSize; return mock.flashError;
}
const esp_partition_t* esp_ota_get_running_partition() { return mock.appPresent ? &app:nullptr; }
esp_err_t esp_image_get_metadata(const esp_partition_pos_t* pos,esp_image_metadata_t* data) {
  assert(pos->offset==app.address && pos->size==app.size);
  data->image_len=mock.imageSize; return mock.imageError;
}
const esp_partition_t* esp_partition_find_first(int type,int sub,const char* name) {
  assert(type==ESP_PARTITION_TYPE_DATA && sub==ESP_PARTITION_SUBTYPE_DATA_NVS);
  assert(!strcmp(name,"nvs") || !strcmp(name,"pet_nvs"));
  return mock.partitionPresent ? &partition : nullptr;
}
esp_err_t nvs_get_stats(const char*,nvs_stats_t* stats) {
  stats->used_entries=mock.nvsUsed; stats->total_entries=mock.nvsTotal;
  return mock.nvsError;
}
int64_t esp_timer_get_time() { return mock.now; }
esp_err_t gpio_set_level(int pin,int level) {
  assert(pin==6 && level==1); ++mock.csHigh; return ESP_OK;
}
esp_err_t gpio_set_direction(int pin,int mode) {
  assert(pin==6 && mode==GPIO_MODE_OUTPUT && mock.csHigh==1);
  ++mock.csOutput; return ESP_OK;
}
void vTaskDelay(unsigned ticks) { assert(ticks==1); }
esp_err_t sdspi_host_do_transaction(int slot,sdmmc_command_t* command) {
  assert(slot==17 && command->timeout_ms && command->timeout_ms<=(mock.cache?1000:100));
  ++mock.transactions;
  return mock.transactionError;
}
esp_err_t sdspi_host_init() { return mock.initError; }
esp_err_t sdspi_host_init_device(const sdspi_device_config_t* config,sdspi_dev_handle_t* handle) {
  assert(config->host_id==SPI2_HOST && config->gpio_cs==6);
  assert(config->gpio_cd==-1 && config->gpio_wp==-1 && config->gpio_int==-1);
  if (mock.attachError!=ESP_OK) return mock.attachError;
  *handle=17; ++mock.attached; return ESP_OK;
}
esp_err_t sdspi_host_remove_device(sdspi_dev_handle_t device) {
  assert(device==17); ++mock.removed; return ESP_OK;
}
esp_err_t sdmmc_card_init(const sdmmc_host_t* host,sdmmc_card_t* card) {
  assert(host->slot==17 && host->max_freq_khz==10000 && host->command_timeout_ms==100);
  assert(host->do_transaction);
  if(mock.recursive) {
    mock.recursive=false;
    const auto blocked=storage_status::read(nullptr,true);
    assert(blocked.cardState==storage_status::CardState::Error);
    assert(blocked.cardError==ESP_ERR_INVALID_STATE);
    assert(mock.attached==1 && mock.removed==0);
  }
  card->host=*host; card->csd.capacity=mock.capacity; card->csd.sector_size=512;
  if(mock.timeoutInit) mock.now+=5000000;
  sdmmc_command_t command; command.timeout_ms=10000;
  const auto result=host->do_transaction(host->slot,&command);
  return result!=ESP_OK ? result:mock.cardError;
}
esp_err_t sdmmc_read_sectors(sdmmc_card_t* card,void*,size_t sector,size_t count) {
  assert(count==1 && sector<static_cast<uint32_t>(card->csd.capacity));
  ++mock.reads;
  sdmmc_command_t command; command.timeout_ms=5000;
  return card->host.do_transaction(card->host.slot,&command);
}
esp_err_t ff_diskio_get_drive(BYTE* drive) { *drive=2; return mock.driveError; }
void ff_diskio_register(BYTE drive,const ff_diskio_impl_t* io) {
  assert(drive==2);
  if(io) { assert(!mock.registered); mock.io=*io; ++mock.registered; }
  else { assert(mock.registered==1); --mock.registered; }
}
FRESULT f_mount(FATFS* fs,const char* path,BYTE immediate) {
  assert(!strcmp(path,"2:"));
  if(!fs) {
    assert(!immediate); ++mock.unmounted; mock.fs=nullptr; return FR_OK;
  }
  assert(immediate==1 && mock.callbacks==0); ++mock.mounts; mock.fs=fs;
  fs->n_fatent=mock.entries;fs->csize=mock.clusterSize;fs->free_clst=mock.freeClusters;
  assert(mock.io.init(2)==(mock.cache?0:STA_PROTECT) && mock.io.status(2)==(mock.cache?0:STA_PROTECT));
  assert(mock.io.status(3)==STA_NOINIT);
  return mock.mountError;
}
FRESULT f_getfree(const char* path,DWORD* free,FATFS** fs) {
  assert(!strcmp(path,"2:") && mock.fs && mock.callbacks==0);
  assert(mock.fs->free_clst==UINT32_MAX); // Must recount, never trust FSInfo.
  BYTE buffer[1024]={};
  assert(mock.io.write(2,buffer,0,1)==RES_WRPRT);
  assert(mock.io.ioctl(2,CTRL_TRIM,buffer)==RES_WRPRT);
  assert(mock.io.ioctl(2,CTRL_SYNC,nullptr)==RES_OK);
  WORD sectorSize=0; LBA_t sectorCount=0; DWORD blockSize=0;
  assert(mock.io.ioctl(2,GET_SECTOR_SIZE,&sectorSize)==RES_OK && sectorSize==512);
  assert(mock.io.ioctl(2,GET_SECTOR_COUNT,&sectorCount)==RES_OK && sectorCount==mock.capacity);
  assert(mock.io.ioctl(2,GET_BLOCK_SIZE,&blockSize)==RES_OK && blockSize==1);
  assert(mock.io.ioctl(3,GET_SECTOR_COUNT,&sectorCount)==RES_NOTRDY);
  assert(mock.io.ioctl(2,GET_SECTOR_COUNT,nullptr)==RES_PARERR);
  assert(mock.io.read(2,buffer,UINT32_MAX,2)==RES_PARERR);
  assert(mock.io.read(2,buffer,mock.capacity-1,2)==RES_PARERR);
  assert(mock.io.read(3,buffer,0,1)==RES_PARERR);
  assert(mock.io.read(2,nullptr,0,1)==RES_PARERR);
  if(mock.timeoutScan) mock.now+=61000000;
  if(mock.io.read(2,buffer,0,2)!=RES_OK) return FR_DISK_ERR;
  mock.fs->n_fatent=mock.entries; mock.fs->csize=mock.clusterSize;
  *free=mock.freeClusters; *fs=mock.fs;
  return mock.freeError;
}

esp_err_t sdmmc_write_sectors(sdmmc_card_t* card,const void*,size_t sector,size_t count){
  assert(mock.cache&&count==1&&sector<mock.capacity);sdmmc_command_t c;c.timeout_ms=5000;return card->host.do_transaction(card->host.slot,&c);
}
FRESULT f_mkdir(const char* path){assert(mock.cache&&!strcmp(path,"2:/MOSSWEB"));return FR_EXIST;}
FRESULT f_open(FIL*,const char* path,BYTE){assert(!strncmp(path,"2:/MOSSWEB/",11));return FR_OK;}
FRESULT f_read(FIL* f,void* data,UINT n,UINT* got){*got=n;memset(data,42,n);f->pos+=n;return FR_OK;}
FRESULT f_write(FIL* f,const void* data,UINT n,UINT* got){
  if(mock.io.write(2,static_cast<const BYTE*>(data),0,1)!=RES_OK)return FR_DISK_ERR;
  *got=n;f->pos+=n;if(f->pos>f->bytes)f->bytes=f->pos;return FR_OK;
}
FRESULT f_lseek(FIL* f,uint32_t p){f->pos=p;return FR_OK;}
uint32_t f_size(FIL* f){return f->bytes;}
FRESULT f_close(FIL*){return FR_OK;}
FRESULT f_unlink(const char*){return FR_OK;}
FRESULT f_rename(const char*,const char*){return FR_OK;}

int main() {
  using storage_status::CardState;
  reset();
  const auto onboard=storage_status::onboard();
  assert(onboard.flashValid && onboard.flashBytes==16777216 && mock.mounts==0 && mock.attached==0);
  reset();
  auto quick=storage_status::read(initialized);
  assert(quick.cardState==CardState::Ready && quick.cardSpaceKnown && quick.cardSpaceCached);
  assert(quick.cardFreeBytes==4096000000ULL && mock.reads==0);clean();
  reset();mock.freeClusters=UINT32_MAX;
  quick=storage_status::read(initialized);
  assert(quick.cardState==CardState::Ready && !quick.cardSpaceKnown && !quick.cardSpaceCached);
  assert(quick.cardTotalBytes==16384000000ULL && !quick.cardFreeBytes && mock.reads==0);clean();
  reset();
  auto value=storage_status::read(initialized,true);
  assert(value.flashValid && value.flashBytes==16777216);
  assert(value.firmwareValid && value.firmwareBytes==1200000 && value.firmwareCapacityBytes==3145728);
  assert(value.nvsValid && value.nvsUsedEntries==60 && value.nvsTotalEntries==4032);
  assert(value.nvsPartitionBytes==131072);
  assert(value.cardState==CardState::Ready && value.cardTotalBytes==16384000000ULL);
  assert(value.cardCapacityBytes==32768000000ULL);
  assert(value.cardFreeBytes==4096000000ULL && value.cardUsedBytes==12288000000ULL);
  assert(mock.callbacks==1 && !mock.callbackSawAttached && mock.reads==2);
  assert(mock.unmounted==1); clean();

  reset(); storage_status::prepareCard();
  assert(mock.onboardCalls==0 && mock.mounts==0 && mock.transactions==1); clean();
  reset(); mock.timeoutInit=true; storage_status::prepareCard();
  assert(mock.transactions==0 && mock.onboardCalls==0); clean();

  for(int failure=0;failure<8;++failure) {
    reset();
    switch(failure) {
      case 0: mock.initError=ESP_ERR_NO_MEM; break;
      case 1: mock.attachError=ESP_ERR_INVALID_STATE; break;
      case 2: mock.cardError=ESP_ERR_TIMEOUT; break;
      case 3: mock.driveError=ESP_ERR_NOT_FOUND; break;
      case 4: mock.mountError=FR_NO_FILESYSTEM; break;
      case 5: mock.freeError=FR_DISK_ERR; break;
      case 6: mock.timeoutInit=true; break;
      case 7: mock.timeoutScan=true; break;
    }
    value=storage_status::read(initialized,true);
    assert(value.cardState==((failure==2 || failure==6) ? CardState::Unavailable:CardState::Error));
    assert(value.cardError!=0 && value.cardTotalBytes==0 && value.cardUsedBytes==0 && value.cardFreeBytes==0);
    assert(mock.callbacks==1);
    if(failure<=2 || failure==6) assert(!mock.callbackSawAttached);
    if(failure==3 || failure==4 || failure==5 || failure==7)
      assert(value.cardCapacityBytes==32768000000ULL);
    clean();
  }
  reset(); mock.recursive=true; assert(storage_status::read(nullptr,true).cardState==CardState::Ready); clean();
  reset(); mock.freeClusters=mock.entries; assert(storage_status::read(nullptr,true).cardState==CardState::Error); clean();
  reset(); mock.entries=1; assert(storage_status::read(nullptr,true).cardState==CardState::Error); clean();
  reset(); mock.clusterSize=0; assert(storage_status::read(nullptr,true).cardState==CardState::Error); clean();
  reset(); mock.entries=UINT32_MAX; mock.clusterSize=65535;
  assert(storage_status::read(nullptr,true).cardState==CardState::Error); clean();
  reset(); mock.freeClusters=1000000; value=storage_status::read(nullptr,true);
  assert(value.cardUsedBytes==0 && value.cardFreeBytes==value.cardTotalBytes); clean();
  reset(); mock.freeClusters=0; value=storage_status::read(nullptr,true);
  assert(value.cardFreeBytes==0 && value.cardUsedBytes==value.cardTotalBytes); clean();

  for(int invalid=0;invalid<8;++invalid) {
    reset();
    switch(invalid) {
      case 0: mock.flashError=ESP_FAIL; break;
      case 1: mock.imageError=ESP_FAIL; break;
      case 2: mock.imageSize=app.size+1; break;
      case 3: mock.appPresent=false; break;
      case 4: mock.nvsError=ESP_ERR_INVALID_STATE; break;
      case 5: mock.nvsUsed=mock.nvsTotal+1; break;
      case 6: mock.nvsTotal=SIZE_MAX; break;
      case 7: mock.partitionPresent=false; break;
    }
    value=storage_status::read(nullptr,true);
    if(invalid==0) assert(!value.flashValid && value.flashBytes==0);
    else if(invalid<4) assert(!value.firmwareValid && value.firmwareBytes==0);
    else assert(!value.nvsValid && value.nvsUsedEntries==0 && value.nvsTotalEntries==0);
    assert(value.cardState==CardState::Ready); clean();
  }
  reset();mock.cache=true;
  assert(storage_files::mount());assert(!storage_files::mount());
  assert(storage_status::read(nullptr,true).cardError==ESP_ERR_INVALID_STATE);
  assert(!storage_files::open("../BAD",storage_files::Mode::Create));
  auto* f=storage_files::open("C00.DAT",storage_files::Mode::Create);assert(f);
  char data[512]{};assert(storage_files::write(f,data,sizeof(data)));assert(storage_files::size(f)==512);
  assert(!storage_files::write(f,data,4097));assert(storage_files::seek(f,0));
  assert(storage_files::read(f,data,sizeof(data))==512&&data[0]==42);
  mock.transactionError=ESP_FAIL;assert(!storage_files::write(f,data,sizeof(data)));
  assert(storage_files::close(f)&&!f);storage_files::unmount();clean();
  reset();mock.cache=true;mock.mountError=FR_NO_FILESYSTEM;
  assert(!storage_files::mount());clean();
  std::puts("Storage cache, read-only, timeout, capacity, callback and cleanup checks passed");
}
