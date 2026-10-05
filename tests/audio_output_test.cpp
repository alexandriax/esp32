#include "audio_mocks/Wire.h"
#include "audio_mocks/driver/i2s_std.h"
#include "audio_mocks/freertos/semphr.h"
#include "../firmware/sloth_pet/audio_output.h"
#include <assert.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <thread>

WireMock Wire;
struct MockSemaphore { std::mutex mutex; std::condition_variable cv; bool given=false; };
static std::thread worker;
static int failStage=0, channels=0, semaphores=0;
static bool channelEnabled=false, workerJoined=true, timeoutOnce=false;
static std::atomic<unsigned> writes{0};
static std::atomic<bool> failWrite{false};
static i2s_chan_config_t channelConfig{};
static i2s_std_config_t standardConfig{};
SemaphoreHandle_t xSemaphoreCreateBinary() {
  if(failStage==1) return nullptr;
  ++semaphores; return new MockSemaphore;
}
int xSemaphoreGive(SemaphoreHandle_t sem) {
  std::lock_guard<std::mutex> guard(sem->mutex); sem->given=true; sem->cv.notify_one(); return pdTRUE;
}
int xSemaphoreTake(SemaphoreHandle_t sem,unsigned ms) {
  if(timeoutOnce) { timeoutOnce=false; return 0; }
  std::unique_lock<std::mutex> guard(sem->mutex);
  if(!sem->cv.wait_for(guard,std::chrono::milliseconds(ms),[sem]{return sem->given;})) return 0;
  sem->given=false; guard.unlock();
  worker.join(); workerJoined=true; return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t sem) { --semaphores; delete sem; }
int xTaskCreate(void (*entry)(void*),const char*,unsigned stack,void* arg,unsigned priority,TaskHandle_t* task) {
  assert(stack==3072 && priority==2);
  if(failStage==5) return 0;
  assert(!worker.joinable()); workerJoined=false;
  worker=std::thread([entry,arg]{entry(arg);}); *task=&worker; return pdPASS;
}
void vTaskDelete(void*) {}
void delay(unsigned ms) { assert(ms==5); }
void vTaskDelay(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
esp_err_t i2s_new_channel(const i2s_chan_config_t* cfg,i2s_chan_handle_t* tx,i2s_chan_handle_t* rx) {
  assert(!rx); if(failStage==2) return -1;
  channelConfig=*cfg; *tx=new MockChannel; ++channels; return ESP_OK;
}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t* cfg) {
  standardConfig=*cfg; return failStage==3 ? -1 : ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t) {
  if(failStage==4) return -1;
  channelEnabled=true; return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t) {
  assert(workerJoined); channelEnabled=false; return ESP_OK;
}
esp_err_t i2s_del_channel(i2s_chan_handle_t tx) {
  assert(!channelEnabled && workerJoined); delete tx; --channels; return ESP_OK;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t,const void*,size_t size,size_t* written,unsigned timeout) {
  assert(channelEnabled && size==512 && timeout==40);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ++writes; *written=failWrite ? 0 : size; return failWrite ? -1 : ESP_OK;
}
static uint8_t originalRails = 0xbd;
static void seed(bool analogEnabled=true) {
  assert(!channels && !semaphores && !worker.joinable());
  Wire=WireMock{}; Wire.present[0x18]=Wire.present[0x34]=true;
  Wire.registers[0x18][0xfd]=0x83; Wire.registers[0x18][0xfe]=0x11;
  originalRails = analogEnabled ? 0xbd : 0xbc;
  Wire.registers[0x34][0x90]=originalRails; Wire.registers[0x34][0x92]=0xa1;
  Wire.registers[0x34][0x93]=0xe0;
}
static void off() {
  assert(!audio_output::active() && !channels && !semaphores && !worker.joinable());
  assert(Wire.registers[0x34][0x90]==originalRails); // All other rails preserved.
  assert(Wire.registers[0x34][0x92]==0xa1); // Prior analog voltage restored.
  assert(Wire.timeout==100);
}
int main() {
  seed(); audio_output::stop(); off();
  assert(!audio_output::setVolume(30));
  uint8_t pcm[3200]={}; assert(!audio_output::pushPcm(pcm,sizeof(pcm)));
  Wire.registers[0x18][0xfd]=0; assert(!audio_output::start()); off();
  for (bool analogEnabled : {false,true}) {
    for(int stage=1;stage<=5;++stage) { seed(analogEnabled); failStage=stage; assert(!audio_output::start()); off(); }
    failStage=0; seed(analogEnabled); Wire.registers[0x18][0xfe]=0;
    assert(!audio_output::start()); off(); // Codec probe failure rolls back ALDO1.
  }
  failStage=0;
  for (const auto failure : {WireMock::Write{0x34,0x92,0}, WireMock::Write{0x34,0x90,0xbd}, WireMock::Write{0x18,0x04,0}}) {
    seed(false); Wire.failAddress=failure.address; Wire.failRegister=failure.reg; Wire.failWrites=1;
    if(failure.reg==0x90) Wire.failValue=failure.value;
    assert(!audio_output::start()); off(); // Partial PMIC/codec programming rolls back.
  }
  seed();
  assert(audio_output::start(35));
  assert(channelConfig.dma_desc_num==4 && channelConfig.dma_frame_num==256 && channelConfig.auto_clear_after_cb);
  assert(standardConfig.clk_cfg.sample_rate_hz==16000);
  assert(standardConfig.slot_cfg.data_bit_width==16 && standardConfig.slot_cfg.slot_mode==I2S_SLOT_MODE_MONO);
  assert(standardConfig.slot_cfg.slot_mask==I2S_STD_SLOT_LEFT);
  assert(standardConfig.gpio_cfg.mclk==19 && standardConfig.gpio_cfg.bclk==20);
  assert(standardConfig.gpio_cfg.ws==22 && standardConfig.gpio_cfg.dout==23 && standardConfig.gpio_cfg.din==-1);
  assert(Wire.registers[0x18][0x00]==0x82 && Wire.registers[0x18][0x01]==0x35);
  assert(Wire.registers[0x18][0x0a]==0x4c && Wire.registers[0x18][0x0e]==0x62);
  assert(Wire.registers[0x18][0x14]==0 && Wire.registers[0x18][0x0d]==0x31);
  assert(Wire.registers[0x34][0x90]==0xbf && Wire.registers[0x34][0x93]==0xfc);
  assert(Wire.registers[0x34][0x92]==0xbc); // Enable3.3V even if prior voltage differs.
  assert(audio_output::setVolume(255)); assert(Wire.registers[0x18][0x32]==0xa7);
  assert(audio_output::setVolume(0)); assert(Wire.registers[0x18][0x31]==0x60 && Wire.registers[0x34][0x90]==0xbd);
  assert(audio_output::start(20)); assert(channels==1 && Wire.registers[0x34][0x90]==0xbf);
  assert(!audio_output::pushPcm(pcm,3199)); assert(audio_output::pushPcm(pcm,3200));
  assert(audio_output::pushPcm(pcm,3200));
  failWrite=true; std::this_thread::sleep_for(std::chrono::milliseconds(30));
  assert(audio_output::stats().writeErrors>0); failWrite=false;
  timeoutOnce=true; audio_output::stop();
  assert(!audio_output::active() && channels==1); // No free beneath worker on timeout.
  audio_output::stop(); off();
  assert(Wire.registers[0x18][0x00]==0x1f && Wire.registers[0x18][0x01]==0);
  for(int i=0;i<10;++i) { seed((i&1)!=0); assert(audio_output::start());
    assert((Wire.registers[0x34][0x90]&1) && Wire.registers[0x34][0x92]==0xbc);
    audio_output::stop(); off();
  }
  seed(false); assert(audio_output::start());
  Wire.failAddress=0x34; Wire.failRegister=0x92; Wire.failWrites=2;
  audio_output::stop(); // Failure restoring voltage retains snapshot for retry.
  assert(!audio_output::start()); // Still failing: cannot overwrite rollback state.
  audio_output::stop(); off();
  seed(false); audio_output::stop(); off(); // Boot stop never enables/rewrites ALDO1.
  for (const auto& write : Wire.writes) assert(write.address!=0x34 || write.reg!=0x92);
  puts("audio output: DAC-only pins, PMIC preservation, cap/mute, start failure cleanup, worker stop timeout/retry and restart passed");
}
