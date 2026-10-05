#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_STATE=0x103,
  ESP_ERR_TIMEOUT=0x107, ESP_ERR_NOT_FOUND=0x105, ESP_ERR_NO_MEM=0x101,
  ESP_ERR_NOT_SUPPORTED=0x106, ESP_ERR_INVALID_RESPONSE=0x108;
using BYTE = unsigned char;
using WORD = uint16_t;
using DWORD = uint32_t;
using LBA_t = uint32_t;
using DSTATUS = BYTE;
enum DRESULT { RES_OK, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR };
constexpr int STA_PROTECT=4, STA_NOINIT=1, CTRL_SYNC=0, GET_SECTOR_COUNT=1,
 GET_SECTOR_SIZE=2, GET_BLOCK_SIZE=3, CTRL_TRIM=4, FF_DRV_NOT_USED=255;
using sdspi_dev_handle_t = int;
constexpr int SPI2_HOST=1, GPIO_NUM_6=6, SDSPI_SLOT_NO_CD=-1,
 SDSPI_SLOT_NO_WP=-1, SDSPI_SLOT_NO_INT=-1;
struct sdmmc_command_t { uint32_t timeout_ms=0; };
struct sdmmc_host_t {
 int slot=SPI2_HOST, max_freq_khz=20000, command_timeout_ms=0;
 esp_err_t (*do_transaction)(int, sdmmc_command_t*)=nullptr;
};
struct sdspi_device_config_t {
 int host_id=SPI2_HOST, gpio_cs=13, gpio_cd=-1, gpio_wp=-1, gpio_int=-1;
};
#define SDSPI_HOST_DEFAULT() sdmmc_host_t{}
#define SDSPI_DEVICE_CONFIG_DEFAULT() sdspi_device_config_t{}
struct sdmmc_card_t {
 sdmmc_host_t host;
 struct { int capacity=0; int sector_size=512; } csd;
 bool is_mem=true;
};
struct esp_partition_t { uint32_t address=0, size=0; };
struct esp_partition_pos_t { uint32_t offset=0, size=0; };
struct esp_image_metadata_t { uint32_t image_len=0; };
constexpr int ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_DATA_NVS=2;
struct nvs_stats_t { size_t used_entries=0, free_entries=0, available_entries=0,
 total_entries=0, namespace_count=0; };
struct FATFS { DWORD free_clst=0, n_fatent=0; WORD csize=0; };
enum FRESULT { FR_OK=0, FR_DISK_ERR=1, FR_INT_ERR=2, FR_NO_FILE=4, FR_EXIST=8, FR_NO_FILESYSTEM=13 };
struct ff_diskio_impl_t {
 DSTATUS (*init)(BYTE);
 DSTATUS (*status)(BYTE);
 DRESULT (*read)(BYTE,BYTE*,uint32_t,unsigned);
 DRESULT (*write)(BYTE,const BYTE*,uint32_t,unsigned);
 DRESULT (*ioctl)(BYTE,BYTE,void*);
};
esp_err_t esp_flash_get_physical_size(void*,uint32_t*);
const esp_partition_t* esp_ota_get_running_partition();
esp_err_t esp_image_get_metadata(const esp_partition_pos_t*,esp_image_metadata_t*);
const esp_partition_t* esp_partition_find_first(int,int,const char*);
esp_err_t nvs_get_stats(const char*,nvs_stats_t*);
int64_t esp_timer_get_time();
esp_err_t sdspi_host_do_transaction(int,sdmmc_command_t*);
esp_err_t sdspi_host_init();
esp_err_t sdspi_host_init_device(const sdspi_device_config_t*,sdspi_dev_handle_t*);
esp_err_t sdspi_host_remove_device(sdspi_dev_handle_t);
esp_err_t sdmmc_card_init(const sdmmc_host_t*,sdmmc_card_t*);
esp_err_t sdmmc_read_sectors(sdmmc_card_t*,void*,size_t,size_t);
esp_err_t ff_diskio_get_drive(BYTE*);
void ff_diskio_register(BYTE,const ff_diskio_impl_t*);
#define ff_diskio_unregister(pdrv) ff_diskio_register(pdrv,nullptr)
FRESULT f_mount(FATFS*,const char*,BYTE);
FRESULT f_getfree(const char*,DWORD*,FATFS**);

using UINT=unsigned;
struct FIL { unsigned pos=0,bytes=0; };
constexpr BYTE FA_READ=1,FA_WRITE=2,FA_CREATE_ALWAYS=8;
esp_err_t sdmmc_write_sectors(sdmmc_card_t*,const void*,size_t,size_t);
FRESULT f_mkdir(const char*);
FRESULT f_open(FIL*,const char*,BYTE);
FRESULT f_read(FIL*,void*,UINT,UINT*);
FRESULT f_write(FIL*,const void*,UINT,UINT*);
FRESULT f_lseek(FIL*,uint32_t);
uint32_t f_size(FIL*);
FRESULT f_close(FIL*);
FRESULT f_unlink(const char*);
FRESULT f_rename(const char*,const char*);
