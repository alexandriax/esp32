/* DIABLITO: bring the board up, mmap the WHD partition, run Doom. Heap numbers printed
 * here are gate measurements. The phase-2 test pattern and flash benchmark live in git history. */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "board_controls.h"
#include "doom_api.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "DIABLITO";

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free %lu, largest block %u, min ever %lu, dma-capable %u", when,
             esp_get_free_heap_size(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             esp_get_minimum_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_DMA));
}

/* Battery run log: every minute, uptime and battery millivolts go to NVS; the previous run's last
 * record is printed at boot, which is how a full-charge runtime gets measured without a cable. */
typedef struct { uint32_t boot, uptime_s; int mv; } runlog_t;
static runlog_t runlog;
static void runlog_tick(void *arg)
{
    runlog.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    runlog.mv = board_battery_mv();
    nvs_handle_t h;
    if (nvs_open("doom", NVS_READWRITE, &h) == ESP_OK) { nvs_set_blob(h, "runlog", &runlog, sizeof runlog); nvs_commit(h); nvs_close(h); }
}
static void runlog_init(void)
{
    nvs_handle_t h;
    runlog_t prev = {0}; size_t len = sizeof prev;
    if (nvs_open("doom", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_get_blob(h, "runlog", &prev, &len) == ESP_OK)
            ESP_LOGI(TAG, "previous run (boot %lu) lasted %lu s (%lu min), last battery %d mV", prev.boot, prev.uptime_s, prev.uptime_s / 60, prev.mv);
        extern int doom_idle_sleep_s;
        int32_t idle; if (nvs_get_i32(h, "idle_s", &idle) == ESP_OK) doom_idle_sleep_s = idle;
        ESP_LOGI(TAG, "idle sleep after %d s in attract (NVS doom/idle_s)", doom_idle_sleep_s);
        nvs_close(h);
    }
    runlog.boot = prev.boot + 1;
    const esp_timer_create_args_t a = { .callback = runlog_tick, .name = "runlog" };
    esp_timer_handle_t t;
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_periodic(t, 60 * 1000000ULL);
}

static const uint8_t *wad;      /* mapped WHD */
static uint32_t wad_size;

static void mount_wad(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "wad");
    if (!p) { ESP_LOGE(TAG, "no 'wad' partition"); return; }
    struct { char magic[4]; uint32_t numlumps, infotableofs, filesize; } hdr;
    esp_err_t read_error = esp_partition_read(p, 0, &hdr, sizeof hdr);
    if (read_error != ESP_OK) {
        ESP_LOGE(TAG, "cannot read WHD: %s", esp_err_to_name(read_error));
        return;
    }
    ESP_LOGI(TAG, "wad partition @0x%lx size %lu: magic %.4s, %lu lumps, infotable @%lu, file %lu bytes",
             p->address, p->size, hdr.magic, hdr.numlumps, hdr.infotableofs, hdr.filesize);
    if (memcmp(hdr.magic, "IWHD", 4)) { ESP_LOGE(TAG, "unsupported WHD format"); return; }
    if (!hdr.numlumps || hdr.infotableofs != 36 || hdr.infotableofs >= hdr.filesize ||
        hdr.filesize > p->size || (uint64_t)hdr.infotableofs + ((uint64_t)hdr.numlumps + 1) * 4 > hdr.filesize) {
        ESP_LOGE(TAG, "bad WHD directory or length"); return;
    }
    wad_size = hdr.filesize;
    esp_partition_mmap_handle_t h;
    const void *ptr;
    esp_err_t e = esp_partition_mmap(p, 0, wad_size, ESP_PARTITION_MMAP_DATA, &ptr, &h);
    if (e != ESP_OK) { ESP_LOGE(TAG, "esp_partition_mmap(%lu) failed: %s", wad_size, esp_err_to_name(e)); return; }
    wad = ptr;
    ESP_LOGI(TAG, "WHD mapped at %p, header via mmap: %.4s %.9s", wad, (const char *)wad, (const char *)wad + 16);
}

void app_main(void)
{
    board_controls_init();              /* power/reset AMOLED before display init */
    ESP_LOGI(TAG, "DIABLITO skeleton, IDF %s, CPU %d MHz", esp_get_idf_version(), CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    log_heap("at boot");
    display_init();
    log_heap("after display");
    mount_wad();
    log_heap("after wad mmap");
    ESP_LOGI(TAG, "main task stack high-water: %u bytes free", uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));

    /* Doom's 16-bit short pointers cover SHORTPTR_BASE (0x40800000) + 256 KB; the zone, thinkercap
     * and players[] live in .bss, so .bss must end inside that window. */
    extern char _bss_end;
    ESP_LOGI(TAG, ".bss ends at %p (short-pointer window ends at 0x40840000, %ld bytes spare)", &_bss_end,
             (long)(0x40840000 - (uintptr_t)&_bss_end));
    if ((uintptr_t)&_bss_end > 0x40840000) {
        ESP_LOGE(TAG, ".bss overruns the short-pointer window; shrink DOOM_ZONE_SIZE or move statics to the heap");
        abort();
    }
    if (wad) {
        log_heap("before NVS");
        esp_err_t e = nvs_flash_init();
        /* This is Moss's shared NVS partition. Never erase it on an IDF
         * version mismatch or full-page error; preserve pet and Wi-Fi data. */
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "NVS unavailable (%s); returning to Moss", esp_err_to_name(e));
            board_return_to_moss();
        }
        runlog_init();
        doom_set_battery_hooks(board_battery_mv, board_battery_percent);
        log_heap("after NVS init");
        ESP_LOGI(TAG, "starting Doom");
        doom_run(wad);      /* never returns */
    }

    ESP_LOGE(TAG, "Doom data unavailable; returning to Moss");
    vTaskDelay(pdMS_TO_TICKS(1000));
    board_return_to_moss();
}
