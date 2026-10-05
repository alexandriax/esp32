#include "../firmware/sloth_pet/browser_memory.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <algorithm>
#include <random>
int main(){
 alignas(max_align_t) static uint8_t canvas[115200];
 assert(!browser_memory_begin(canvas+1,sizeof(canvas)-1));
 std::mt19937 random(7);
 for(int round=0;round<100;++round){
  assert(browser_memory_begin(canvas,sizeof(canvas)));assert(!browser_memory_begin(canvas,sizeof(canvas)));
  std::vector<void*> allocations;
  for(int i=0;i<200;++i){const size_t n=1+random()%1400;void* p=browser_memory_calloc(1,n);assert(p);for(size_t j=0;j<n;++j)assert(static_cast<uint8_t*>(p)[j]==0);memset(p,0x55,n);allocations.push_back(p);}
  assert(browser_memory_live()>0 && !browser_memory_end());
  std::shuffle(allocations.begin(),allocations.end(),random);
  for(void* p:allocations)browser_memory_free(p);
  void* p=browser_memory_malloc(100);memset(p,0xaa,100);p=browser_memory_realloc(p,8000);assert(p);for(int i=0;i<100;++i)assert(static_cast<uint8_t*>(p)[i]==0xaa);browser_memory_free(p);
  std::thread a([]{for(int i=0;i<1000;++i){void* q=browser_memory_calloc(1,64);assert(q);browser_memory_free(q);}});
  std::thread b([]{for(int i=0;i<1000;++i){void* q=browser_memory_malloc(300);assert(q);browser_memory_free(q);}});a.join();b.join();
  // Simulate SDK allocations surviving every browser exit. They cannot occupy
  // any part of the protected canvas, regardless of external fragmentation.
  void* persistent=malloc(256);assert(persistent);
  assert(!browser_memory_live() && browser_memory_peak()>0 && browser_memory_end()==canvas);free(persistent);
 }
 assert(!browser_memory_calloc(SIZE_MAX,2));
 puts("Browser arena: 100 fragmented/reordered lifetimes, fallback, alignment, overflow and concurrent allocations restore identical canvas");
}
