#pragma once
#include <atomic>
#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <thread>
#endif
// LCD DMA and SD polling share SPI2. Hold through DMA completion / one FatFS
// operation. Recursive ownership permits main-loop input callbacks to adjust
// brightness between display stripes. Never use from an ISR. No heap or TLS
// pointer refers to another task's stack, and no RTOS semaphore is allocated.
namespace board_bus {
struct State {std::atomic_flag lock=ATOMIC_FLAG_INIT;std::atomic<void*> owner{nullptr};unsigned depth=0;};
inline State& state(){static State value;return value;}
inline void* identity(){
#if defined(ESP_PLATFORM)
  return xTaskGetCurrentTaskHandle();
#else
  static thread_local char token;return &token;
#endif
}
class Guard {
 public:
  Guard(){
    State& s=state();void* id=identity();
    if(s.owner.load(std::memory_order_acquire)==id){++s.depth;return;}
    while(s.lock.test_and_set(std::memory_order_acquire)){
#if defined(ESP_PLATFORM)
      vTaskDelay(1);
#else
      std::this_thread::yield();
#endif
    }
    s.depth=1;s.owner.store(id,std::memory_order_release);
  }
  ~Guard(){State& s=state();if(!--s.depth){s.owner.store(nullptr,std::memory_order_release);s.lock.clear(std::memory_order_release);}}
  Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
};
}
