#pragma once
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Lend the allocated pet canvas to transient browser allocations, never to the
// system heap. Main-loop begin/end require all previous consumers to be idle.
bool browser_memory_begin(void* memory,size_t bytes);
void* browser_memory_end(void); // Returns the identical canvas only when empty.
void* browser_memory_malloc(size_t bytes);
void* browser_memory_calloc(size_t count,size_t bytes);
void* browser_memory_realloc(void* pointer,size_t bytes);
void browser_memory_free(void* pointer);
size_t browser_memory_live(void);
size_t browser_memory_peak(void);
size_t browser_memory_capacity(void);
#ifdef __cplusplus
}
#endif
