#include "storage_status.h"
#include "storage_files.h"
#include "board_bus.h"
#include <cstring>
#include <cstdio>

#include <atomic>
#include <new>
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "ff.h"
#include "diskio_impl.h"
#include "nvs.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace storage_status {
namespace {
// Waveshare schematic, SD-CARD: CLK0, CMD1, D0=2, CS6. CD is not
// connected to a GPIO. SPI2 is already configured by board::begin() for
// the QSPI display. Never initialize/free that bus or touch PMIC/I2S pins.
// https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf
constexpr int64_t kInitBudgetUs = 4000000;
constexpr int64_t kScanBudgetUs = 60000000;
constexpr uint32_t kCommandTimeoutMs = 100;
std::atomic_flag busy = ATOMIC_FLAG_INIT;

struct Probe {
  sdmmc_card_t card = {};
  sdspi_dev_handle_t device = -1;
  BYTE drive = FF_DRV_NOT_USED;
  char path[3] = {};
  FATFS* fs = nullptr;
  bool attached = false;
  bool registered = false;
  int64_t deadline = 0;
  esp_err_t ioError = ESP_OK;
  unsigned blocksRead = 0;
  bool writable = false;
};

// All callbacks run synchronously on the single diagnostics worker. The
// atomic guard also prevents accidental second probes from replacing this.
Probe* active = nullptr;

esp_err_t boundedTransaction(int slot, sdmmc_command_t* command) {
  if (!active || !command) return ESP_ERR_INVALID_STATE;
  const int64_t remaining = active->deadline - esp_timer_get_time();
  if (remaining <= 0) return active->ioError = ESP_ERR_TIMEOUT;
  uint32_t limit = static_cast<uint32_t>((remaining + 999) / 1000);
  const uint32_t ceiling = active->writable ? 1000 : kCommandTimeoutMs;
  if (limit > ceiling) limit = ceiling;
  if (active->writable || !command->timeout_ms || command->timeout_ms > limit) command->timeout_ms = limit;
  const esp_err_t result = sdspi_host_do_transaction(slot, command);
  if (result != ESP_OK) active->ioError = result;
  return result;
}

DSTATUS diskStatus(BYTE drive) {
  return active && active->registered && drive == active->drive
      ? (active->writable ? 0 : STA_PROTECT) : STA_NOINIT;
}

DRESULT diskRead(BYTE drive, BYTE* buffer, uint32_t sector, unsigned count) {
  if (!active || !buffer || !count || drive != active->drive) return RES_PARERR;
  // Check without adding sector+count in a 32-bit type.
  const uint32_t capacity = static_cast<uint32_t>(active->card.csd.capacity);
  if (sector >= capacity || count > capacity - sector) return RES_PARERR;
  if (esp_timer_get_time() >= active->deadline) {
    active->ioError = ESP_ERR_TIMEOUT;
    return RES_ERROR;
  }
  // One block per transaction bounds the time spent owning the shared bus.
  // FatFS normally requests one sector during the allocation-table recount.
  for (unsigned i = 0; i < count; ++i) {
    const esp_err_t error = sdmmc_read_sectors(&active->card,
        buffer + static_cast<size_t>(i) * active->card.csd.sector_size, sector + i, 1);
    if (error != ESP_OK) {
      active->ioError = error;
      return RES_ERROR;
    }
    // Leave time for the idle task/watchdog on a long FAT recount.
    if (++active->blocksRead % 16 == 0) vTaskDelay(1);
  }
  return RES_OK;
}

// Diagnostics always reject writes. Only an explicitly mounted browser cache
// session enables this adapter; neither path formats or trims the card.
DRESULT diskWrite(BYTE drive, const BYTE* data, uint32_t sector, unsigned count) {
  if (!active || !active->writable || drive != active->drive) return RES_WRPRT;
  if (!data || !count || sector >= static_cast<uint32_t>(active->card.csd.capacity) || count > static_cast<uint32_t>(active->card.csd.capacity)-sector) return RES_PARERR;
  for (unsigned i=0;i<count;++i) {
    if (esp_timer_get_time()>=active->deadline) { active->ioError=ESP_ERR_TIMEOUT;return RES_ERROR; }
    const esp_err_t error=sdmmc_write_sectors(&active->card,data+i*512,sector+i,1);
    if(error!=ESP_OK){active->ioError=error;return RES_ERROR;}
    if(++active->blocksRead%16==0)vTaskDelay(1);
  }
  return RES_OK;
}
DRESULT diskIoctl(BYTE drive, BYTE command, void* output) {
  if (!active || drive != active->drive) return RES_NOTRDY;
  if (command == CTRL_SYNC) return RES_OK;  // No writes can be pending.
  if (!output) return RES_PARERR;
  switch (command) {
    case GET_SECTOR_COUNT:
      *static_cast<LBA_t*>(output) = active->card.csd.capacity;
      return RES_OK;
    case GET_SECTOR_SIZE:
      *static_cast<WORD*>(output) = active->card.csd.sector_size;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD*>(output) = 1;
      return RES_OK;
    default: return RES_WRPRT;
  }
}

struct Cleanup {
  Probe& probe;
  void (*initialized)();
  Cleanup(Probe& value, void (*callback)()) : probe(value), initialized(callback) {}
  void notify() {
    void (*callback)() = initialized;
    initialized = nullptr;
    if (callback) callback();
  }
  ~Cleanup() {
    if (probe.registered) {
      // Unmount only our private FatFS object. It has no open files; its disk
      // adapter rejects writes even if the library tries to sync metadata.
      f_mount(nullptr, probe.path, 0);
      ff_diskio_unregister(probe.drive);
    }
    delete probe.fs;
    if (probe.attached) sdspi_host_remove_device(probe.device);
    // IDF's remove_device resets CS to input. Keep only the SD's CS high so
    // later display traffic cannot select it; leave all shared pins alone.
    gpio_set_level(GPIO_NUM_6, 1);
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_OUTPUT);
    active = nullptr;
    busy.clear(std::memory_order_release);
    notify();
  }
};

esp_err_t initializeCard(Probe& probe) {
  esp_err_t error = sdspi_host_init();
  if (error != ESP_OK) return error;
  sdspi_device_config_t config = SDSPI_DEVICE_CONFIG_DEFAULT();
  config.host_id = SPI2_HOST;
  config.gpio_cs = GPIO_NUM_6;
  config.gpio_cd = SDSPI_SLOT_NO_CD;
  config.gpio_wp = SDSPI_SLOT_NO_WP;
  config.gpio_int = SDSPI_SLOT_NO_INT;
  error = sdspi_host_init_device(&config, &probe.device);
  if (error != ESP_OK) return error;
  probe.attached = true;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = probe.device;
  host.max_freq_khz = 10000;
  host.command_timeout_ms = kCommandTimeoutMs;
  host.do_transaction = boundedTransaction;
  return sdmmc_card_init(&host, &probe.card);
}

void readOnboard(Snapshot& value) {
  uint32_t physicalBytes = 0;
  if (esp_flash_get_physical_size(nullptr, &physicalBytes) == ESP_OK && physicalBytes) {
    value.flashValid = true;
    value.flashBytes = physicalBytes;
  }
  const esp_partition_t* app = esp_ota_get_running_partition();
  if (app) {
    value.firmwareCapacityBytes = app->size;
    esp_partition_pos_t position = {};
    position.offset = app->address;
    position.size = app->size;
    esp_image_metadata_t metadata = {};
    // Read the running image's segment metadata, without hashing the entire
    // image or mistaking its allocated partition for its actual size.
    if (esp_image_get_metadata(&position, &metadata) == ESP_OK &&
        metadata.image_len && metadata.image_len <= app->size) {
      value.firmwareBytes = metadata.image_len;
      value.firmwareValid = true;
    }
  }

  // These are the two partitions actually used by this firmware. Do not
  // initialize NVS here: initialization can repair/write damaged pages.
  const char* const names[] = {"nvs", "pet_nvs"};
  bool valid = true;
  for (const char* name : names) {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, name);
    nvs_stats_t stats = {};
    if (!partition || nvs_get_stats(name, &stats) != ESP_OK ||
        !stats.total_entries || stats.used_entries > stats.total_entries ||
        stats.total_entries > partition->size / 32u) {
      valid = false;
      continue;
    }
    value.nvsPartitionBytes += partition->size;
    value.nvsUsedEntries += stats.used_entries;
    value.nvsTotalEntries += stats.total_entries;
  }
  value.nvsValid = valid;
  if (!valid) {
    value.nvsPartitionBytes = value.nvsUsedEntries = value.nvsTotalEntries = 0;
  }
}

void readCard(Snapshot& value, void (*initialized)(), bool recount) {
  value.cardState = CardState::Error;
  if (busy.test_and_set(std::memory_order_acquire)) {
    value.cardError = ESP_ERR_INVALID_STATE;
    if (initialized) initialized();
    return;
  }
  board_bus::Guard bus;
  Probe probe;
  Cleanup cleanup{probe, initialized};
  active = &probe;
  probe.deadline = esp_timer_get_time() + kInitBudgetUs;
  probe.fs = new (std::nothrow) FATFS{};
  if (!probe.fs) { value.cardError = ESP_ERR_NO_MEM; return; }
  value.cardError = initializeCard(probe);
  if (value.cardError != ESP_OK) {
    // No GPIO detects insertion on this board. A timeout cannot distinguish
    // an empty socket from a card that does not respond; never claim NoCard.
    if (value.cardError == ESP_ERR_TIMEOUT || value.cardError == ESP_ERR_NOT_FOUND)
      value.cardState = CardState::Unavailable;
    return;
  }
  if (!probe.card.is_mem || probe.card.csd.sector_size != 512 ||
      probe.card.csd.capacity <= 0) {
    value.cardError = ESP_ERR_NOT_SUPPORTED;
    return;
  }
  value.cardCapacityBytes = static_cast<uint64_t>(probe.card.csd.capacity) * 512u;
  probe.deadline = esp_timer_get_time() + (recount ? kScanBudgetUs : 2000000);
  value.cardError = ff_diskio_get_drive(&probe.drive);
  if (value.cardError != ESP_OK || probe.drive >= 10) {
    if (value.cardError == ESP_OK) value.cardError = ESP_ERR_INVALID_STATE;
    return;
  }
  const ff_diskio_impl_t io = {diskStatus, diskStatus, diskRead, diskWrite, diskIoctl};
  ff_diskio_register(probe.drive, &io);
  probe.registered = true;
  probe.path[0] = static_cast<char>('0' + probe.drive);
  probe.path[1] = ':';
  probe.ioError = ESP_OK;
  // SPI and filesystem errors use positive IDF errors and negative FatFS
  // results respectively; the UI may show a generic error and log this code.
  FRESULT result = f_mount(probe.fs, probe.path, 1);
  if (result != FR_OK) {
    value.cardError = probe.ioError != ESP_OK ? probe.ioError : -static_cast<int>(result);
    return;
  }
  // FAT32 FSInfo is an allocation hint, not a measured recount. Normal UI
  // reads never walk millions of entries or block the display for 30 seconds.
  probe.ioError = ESP_OK;
  DWORD freeClusters = probe.fs->free_clst;
  FATFS* measured = probe.fs;
  if(recount){
    probe.fs->free_clst = UINT32_MAX;
    result = f_getfree(probe.path, &freeClusters, &measured);
  }
  if (result != FR_OK || !measured) {
    value.cardError = probe.ioError != ESP_OK ? probe.ioError :
        (result != FR_OK ? -static_cast<int>(result) : ESP_ERR_INVALID_RESPONSE);
    return;
  }
  if (measured->n_fatent < 2 || !measured->csize ||
      (recount && freeClusters > measured->n_fatent - 2)) {
    value.cardError = ESP_ERR_INVALID_RESPONSE;
    return;
  }
  const uint64_t clusterBytes = static_cast<uint64_t>(measured->csize) * 512u;
  const uint64_t total = static_cast<uint64_t>(measured->n_fatent - 2) * clusterBytes;
  const uint64_t free = static_cast<uint64_t>(freeClusters) * clusterBytes;
  if (total > value.cardCapacityBytes) { value.cardError = ESP_ERR_INVALID_RESPONSE; return; }
  value.cardTotalBytes = total;
  value.cardSpaceKnown = freeClusters <= measured->n_fatent - 2;
  value.cardSpaceCached = value.cardSpaceKnown && !recount;
  value.cardFreeBytes = value.cardSpaceKnown ? free : 0;
  value.cardUsedBytes = value.cardSpaceKnown ? total - free : 0;
  value.cardError = ESP_OK;
  value.cardState = CardState::Ready;
}
}  // namespace

void prepareCard() {
  if (busy.test_and_set(std::memory_order_acquire)) return;
  board_bus::Guard bus;
  Probe probe;
  Cleanup cleanup{probe, nullptr};
  active = &probe;
  probe.deadline = esp_timer_get_time() + kInitBudgetUs;
  initializeCard(probe);
}

Snapshot onboard() { Snapshot value; readOnboard(value); return value; }
Snapshot read(void (*cardReleased)(), bool recount) {
  Snapshot value=onboard();
  readCard(value, cardReleased, recount);
  return value;
}

}  // namespace storage_status

namespace storage_files {
using namespace storage_status;
struct File { FIL handle{}; };
namespace {
Probe* files=nullptr;
void releaseSession(){if(files){ {Cleanup cleanup{*files,nullptr};} delete files;files=nullptr;}}
bool path(const char* name,char (&out)[32]) {
  if(!files||!name||!name[0]||strlen(name)>12)return false;
  unsigned dots=0,base=0,ext=0;
  for(const char* p=name;*p;++p){
    if(*p=='.'){if(++dots>1||!base)return false;}
    else if((*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')){if(dots)++ext;else ++base;}
    else return false;
  }
  if(base>8||ext>3||(dots&&!ext))return false;
  snprintf(out,sizeof(out),"%c:/MOSSWEB/%s",files->path[0],name);return true;
}
void deadline(){files->deadline=esp_timer_get_time()+2000000;files->ioError=ESP_OK;}
}
bool mount(){
  board_bus::Guard bus;
  if(files||busy.test_and_set(std::memory_order_acquire))return false;
  files=new(std::nothrow) Probe;
  if(!files){busy.clear(std::memory_order_release);return false;}
  active=files;files->deadline=esp_timer_get_time()+kInitBudgetUs;
  files->fs=new(std::nothrow) FATFS{};
  if(!files->fs||initializeCard(*files)!=ESP_OK||!files->card.is_mem||files->card.csd.sector_size!=512||files->card.csd.capacity<=0){releaseSession();return false;}
  if(ff_diskio_get_drive(&files->drive)!=ESP_OK||files->drive>=10){releaseSession();return false;}
  const ff_diskio_impl_t io={diskStatus,diskStatus,diskRead,diskWrite,diskIoctl};
  ff_diskio_register(files->drive,&io);files->registered=true;files->writable=true;
  files->path[0]=static_cast<char>('0'+files->drive);files->path[1]=':';deadline();
  if(f_mount(files->fs,files->path,1)!=FR_OK){releaseSession();return false;}
  char directory[]="0:/MOSSWEB";directory[0]=files->path[0];
  const auto result=f_mkdir(directory);
  if(result!=FR_OK&&result!=FR_EXIST){releaseSession();return false;}
  return true;
}
void unmount(){board_bus::Guard bus;releaseSession();}
File* open(const char* name,Mode mode){
  board_bus::Guard bus;char filename[32];if(!path(name,filename))return nullptr;deadline();
  File* file=new(std::nothrow) File;if(!file)return nullptr;
  if(f_open(&file->handle,filename,mode==Mode::Read?FA_READ:FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK){delete file;return nullptr;}return file;
}
size_t read(File* file,void* data,size_t bytes){
  board_bus::Guard bus;if(!files||!file||!data||bytes>4096)return 0;deadline();UINT got=0;
  return f_read(&file->handle,data,bytes,&got)==FR_OK?got:0;
}
bool write(File* file,const void* data,size_t bytes){
  board_bus::Guard bus;if(!files||!file||(!data&&bytes)||bytes>4096)return false;deadline();UINT wrote=0;
  return f_write(&file->handle,data,bytes,&wrote)==FR_OK&&wrote==bytes;
}
bool seek(File* file,uint32_t position){board_bus::Guard bus;if(!files||!file)return false;deadline();return f_lseek(&file->handle,position)==FR_OK;}
uint32_t size(File* file){return files&&file?static_cast<uint32_t>(f_size(&file->handle)):0;}
bool close(File*& file){board_bus::Guard bus;if(!file)return true;bool ok=false;if(files){deadline();ok=f_close(&file->handle)==FR_OK;}delete file;file=nullptr;return ok;}
bool remove(const char* name){board_bus::Guard bus;char filename[32];if(!path(name,filename))return false;deadline();const auto e=f_unlink(filename);return e==FR_OK||e==FR_NO_FILE;}
bool rename(const char* from,const char* to){board_bus::Guard bus;char a[32],b[32];if(!path(from,a)||!path(to,b))return false;deadline();return f_rename(a,b)==FR_OK;}
}
