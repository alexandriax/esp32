#include "browser_memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#else
#include <mutex>
#endif
namespace {
union Block { max_align_t alignment;struct {size_t size;bool available;} info; };
uint8_t* arena=nullptr;size_t capacity=0,live=0,peak=0;bool active=false;
#ifdef ARDUINO
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
struct Guard {Guard(){portENTER_CRITICAL(&lock);}~Guard(){portEXIT_CRITICAL(&lock);}};
#else
std::mutex lock;
struct Guard {std::lock_guard<std::mutex> held;Guard():held(lock){}};
#endif
bool contains(const void* p){const uintptr_t v=reinterpret_cast<uintptr_t>(p),start=reinterpret_cast<uintptr_t>(arena);return arena && v>=start && v-start<capacity;}
Block* next(Block* b){uint8_t* p=reinterpret_cast<uint8_t*>(b+1)+b->info.size;return p<arena+capacity?reinterpret_cast<Block*>(p):nullptr;}
void* allocate(size_t bytes){
  if(!active || bytes>SIZE_MAX-(alignof(Block)-1))return nullptr;
  bytes=(bytes+alignof(Block)-1)&~(alignof(Block)-1);
  for(Block* b=reinterpret_cast<Block*>(arena);b;b=next(b))if(b->info.available && b->info.size>=bytes){
    if(b->info.size>=bytes+sizeof(Block)+alignof(Block)){
      Block* tail=reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(b+1)+bytes);
      tail->info={b->info.size-bytes-sizeof(Block),true};b->info.size=bytes;
    }
    b->info.available=false;live+=b->info.size;if(live>peak)peak=live;return b+1;
  }
  return nullptr;
}
}
extern "C" bool browser_memory_begin(void* memory,size_t bytes){
  Guard guard;
  if(active || live || !memory || reinterpret_cast<uintptr_t>(memory)%alignof(Block) || bytes<2*sizeof(Block))return false;
  arena=static_cast<uint8_t*>(memory);capacity=bytes&~(alignof(Block)-1);live=peak=0;active=true;
  reinterpret_cast<Block*>(arena)->info={capacity-sizeof(Block),true};return true;
}
extern "C" void* browser_memory_end(){Guard guard;if(!active || live)return nullptr;active=false;return arena;}
extern "C" size_t browser_memory_live(){Guard guard;return live;}
extern "C" size_t browser_memory_peak(){Guard guard;return peak;}
extern "C" size_t browser_memory_capacity(){Guard guard;return active?capacity:0;}
extern "C" void* browser_memory_malloc(size_t bytes){if(!bytes)return nullptr;{Guard guard;if(void* p=allocate(bytes))return p;}return malloc(bytes);}
extern "C" void* browser_memory_calloc(size_t n,size_t bytes){if(bytes && n>SIZE_MAX/bytes)return nullptr;void* p=browser_memory_malloc(n*bytes);if(p)memset(p,0,n*bytes);return p;}
extern "C" void browser_memory_free(void* p){
  if(!p)return;
  {Guard guard;if(contains(p)){
    Block* b=static_cast<Block*>(p)-1;
    if(b->info.available)return;
    live-=b->info.size;b->info.available=true;
    // Coalesce both directions, keeping subsequent loans independent of the
    // order in which TLS, document and renderer allocations were released.
    for(Block* at=reinterpret_cast<Block*>(arena);at;){Block* after=next(at);if(after && at->info.available && after->info.available)at->info.size+=sizeof(Block)+after->info.size;else at=after;}
    return;
  }}free(p);
}
extern "C" void* browser_memory_realloc(void* p,size_t bytes){
  if(!p)return browser_memory_malloc(bytes);
  if(!bytes){browser_memory_free(p);return nullptr;}
  size_t old=0;bool pooled=false;
  {Guard guard;pooled=contains(p);if(pooled)old=(static_cast<Block*>(p)-1)->info.size;}
  if(!pooled)return realloc(p,bytes);
  if(bytes<=old)return p;
  void* moved=browser_memory_malloc(bytes);if(!moved)return nullptr;
  memcpy(moved,p,old);browser_memory_free(p);return moved;
}
